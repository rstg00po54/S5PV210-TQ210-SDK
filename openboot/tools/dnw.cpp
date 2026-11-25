// Linux native C++17 uploader. libusb-1.0 ABI fallback for runtime-only hosts.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <thread>
#include <chrono>
#if __has_include(<libusb-1.0/libusb.h>)
#include <libusb-1.0/libusb.h>
#else
extern "C" {
struct libusb_context; struct libusb_device; struct libusb_device_handle;
struct libusb_endpoint_descriptor {uint8_t bLength,bDescriptorType,bEndpointAddress,bmAttributes;uint16_t wMaxPacketSize;uint8_t bInterval,bRefresh,bSynchAddress;const unsigned char* extra;int extra_length;};
struct libusb_interface_descriptor {uint8_t bLength,bDescriptorType,bInterfaceNumber,bAlternateSetting,bNumEndpoints,bInterfaceClass,bInterfaceSubClass,bInterfaceProtocol,iInterface;const libusb_endpoint_descriptor* endpoint;const unsigned char* extra;int extra_length;};
struct libusb_interface {const libusb_interface_descriptor* altsetting;int num_altsetting;};
struct libusb_config_descriptor {uint8_t bLength,bDescriptorType;uint16_t wTotalLength;uint8_t bNumInterfaces,bConfigurationValue,iConfiguration,bmAttributes,MaxPower;const libusb_interface* interface;const unsigned char* extra;int extra_length;};
int libusb_init(libusb_context**); void libusb_exit(libusb_context*);
libusb_device_handle* libusb_open_device_with_vid_pid(libusb_context*,uint16_t,uint16_t);
void libusb_close(libusb_device_handle*);libusb_device* libusb_get_device(libusb_device_handle*);
struct libusb_device_descriptor {uint8_t bLength,bDescriptorType;uint16_t bcdUSB;uint8_t bDeviceClass,bDeviceSubClass,bDeviceProtocol,bMaxPacketSize0;uint16_t idVendor,idProduct,bcdDevice;uint8_t iManufacturer,iProduct,iSerialNumber,bNumConfigurations;};
int libusb_get_device_descriptor(libusb_device*,libusb_device_descriptor*);
int libusb_get_string_descriptor_ascii(libusb_device_handle*,uint8_t,unsigned char*,int);
int libusb_control_transfer(libusb_device_handle*,uint8_t,uint8_t,uint16_t,uint16_t,unsigned char*,uint16_t,unsigned int);
int libusb_get_configuration(libusb_device_handle*,int*);
int libusb_set_configuration(libusb_device_handle*,int);
int libusb_get_active_config_descriptor(libusb_device*,libusb_config_descriptor**);
void libusb_free_config_descriptor(libusb_config_descriptor*);
int libusb_claim_interface(libusb_device_handle*,int);int libusb_release_interface(libusb_device_handle*,int);
int libusb_bulk_transfer(libusb_device_handle*,unsigned char,unsigned char*,int,int*,unsigned int);
const char* libusb_error_name(int);
}
#endif
using Bytes=std::vector<uint8_t>; using Packets=std::vector<Bytes>;
using Clock=std::chrono::steady_clock;
static void elapsed(const char* label,Clock::time_point start){
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
    std::printf("[TIME] %s: %lld ms\n",label,static_cast<long long>(ms));std::fflush(stdout);
}
static void append32(Bytes& p,uint32_t n){for(int i=0;i<4;i++)p.push_back(uint8_t(n>>(8*i)));}
static Packets packets(const Bytes& image,uint32_t addr,bool full,bool join){
    if(image.empty()||image.size()>16368) throw std::runtime_error("invalid SRAM image length (1..16368)");
    Bytes first;append32(first,addr);append32(first,uint32_t(image.size()+10));
    auto n=std::min<size_t>(2040,image.size());first.insert(first.end(),image.begin(),image.begin()+n);
    Packets p{first};for(size_t i=n;i<image.size();i+=2048)p.emplace_back(image.begin()+i,image.begin()+std::min(i+2048,image.size()));
    unsigned sum=std::accumulate(image.begin(),image.begin()+(full?image.size():std::min<size_t>(4096,image.size())),0u);
    Bytes tail{uint8_t(sum),uint8_t(sum>>8)};if(join)p.back().insert(p.back().end(),tail.begin(),tail.end());else p.push_back(tail);return p;
}
static void check(int rc,const char* label){if(rc<0)throw std::runtime_error(std::string(label)+": "+libusb_error_name(rc));}
struct Usb {libusb_context* c=nullptr;libusb_device_handle* h=nullptr;int iface=-1;~Usb(){if(h){if(iface>=0)libusb_release_interface(h,iface);libusb_close(h);}if(c)libusb_exit(c);}};
static std::string product_name(libusb_device_handle* h,libusb_device_descriptor& dd){
    check(libusb_get_device_descriptor(libusb_get_device(h),&dd),"device descriptor");
    if(!dd.iProduct)throw std::runtime_error("no USB product string");
    unsigned char name[128]{};
    int n=libusb_get_string_descriptor_ascii(h,dd.iProduct,name,sizeof(name));
    check(n,"product string");
    return std::string(reinterpret_cast<char*>(name),size_t(n));
}
static void send_on(Usb& u,Packets& p,bool reconfig,bool strict_rom=false,bool handoff=false){
    check(libusb_init(&u.c),"libusb_init");u.h=libusb_open_device_with_vid_pid(u.c,0x04e8,0x1234);if(!u.h)throw std::runtime_error("04e8:1234 not accessible; check USB boot/VM attachment/permissions");
    if(strict_rom){
        libusb_device_descriptor dd{};
        const auto name=product_name(u.h,dd);
        if(name!="SEC S5PC110 Test B/D")throw std::runtime_error("expected cold Samsung ROM device, got "+name+"; nothing sent");
    }
    if(reconfig)check(libusb_set_configuration(u.h,1),"SET_CONFIGURATION(1)");
    libusb_config_descriptor* cfg=nullptr;check(libusb_get_active_config_descriptor(libusb_get_device(u.h),&cfg),"get active config");
    int iface=-1,ep=-1,count=0;bool handoff_pair=false;for(int i=0;i<cfg->bNumInterfaces;i++)for(int j=0;j<cfg->interface[i].num_altsetting;j++){
        auto& a=cfg->interface[i].altsetting[j];if(a.bAlternateSetting||a.bInterfaceClass!=255)continue;
        bool in1=false,out2=false;
        for(int k=0;k<a.bNumEndpoints;k++){auto& e=a.endpoint[k];
            if((e.bmAttributes&3)==2&&!(e.bEndpointAddress&128)){iface=a.bInterfaceNumber;ep=e.bEndpointAddress;count++;}
            if((e.bmAttributes&3)==2&&e.wMaxPacketSize==512){
                if(e.bEndpointAddress==0x81)in1=true;
                if(e.bEndpointAddress==0x02)out2=true;
            }
        }
        if(in1&&out2)handoff_pair=true;
    }libusb_free_config_descriptor(cfg);if(count!=1)throw std::runtime_error("expected exactly one vendor bulk OUT endpoint");
    if(handoff&&(!handoff_pair||ep!=2))throw std::runtime_error("ROM does not advertise expected HS bulk IN 0x81 / OUT 0x02; no image sent");
    check(libusb_claim_interface(u.h,iface),"claim interface");u.iface=iface;std::printf("interface=%d OUT=0x%02x\n",iface,ep);
    for(size_t i=0;i<p.size();i++){int sent=0;int rc=libusb_bulk_transfer(u.h,uint8_t(ep),p[i].data(),int(p[i].size()),&sent,10000);std::printf("transfer %zu: %d/%zu %s\n",i,sent,p[i].size(),libusb_error_name(rc));std::fflush(stdout);check(rc,"bulk OUT (power-cycle before retry)");if(sent!=int(p[i].size()))throw std::runtime_error("short transfer; power-cycle before retry");}
    std::cout<<"USB transport complete; CPU execution NOT confirmed. Check UART0.\n";
}
static void send(Packets& p,bool reconfig,bool strict_rom=false){Usb u;send_on(u,p,reconfig,strict_rom);}
static void selftest(){
    Bytes b(5120);for(size_t i=0;i<b.size();i++)b[i]=uint8_t(i);
    auto p=packets(b,0xd0020010,false,false);
    if(p.size()!=4||p[0].size()!=2048||p[1].size()!=2048||p[2].size()!=1032||p[3].size()!=2||p[0][0]!=0x10||p[0][3]!=0xd0||p[0][4]!=0x0a||p[0][5]!=0x14)throw std::runtime_error("packet layout");
    Bytes reconstructed(p[0].begin()+8,p[0].end());for(size_t i=1;i+1<p.size();i++)reconstructed.insert(reconstructed.end(),p[i].begin(),p[i].end());if(reconstructed!=b)throw std::runtime_error("payload mismatch");
    if(p[3]!=Bytes{0x00,0xf8}||packets(b,0xd0020010,true,false).back()!=Bytes{0x00,0xf6})throw std::runtime_error("checksum mismatch");
    auto q=packets(Bytes{1},0xd0020010,true,true);if(q.size()!=1||q[0].size()!=11||q[0][9]!=1||q[0][10]!=0)throw std::runtime_error("joined tail mismatch");
    std::cout<<"Packet selftest PASS\n";
}
static uint32_t crc32(const Bytes& b){
    uint32_t crc=0xffffffffu;
    for(uint8_t x:b){crc^=x;for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}
    return ~crc;
}
static uint32_t get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static Bytes bulk_status(libusb_device_handle* h,unsigned timeout=30000){
    Bytes status(20);
    int n=libusb_control_transfer(h,0xc0,0x5a,0x4f42,0x4c31,status.data(),20,timeout);
    check(n,"OpenBoot protocol status");if(n!=20||get32(status.data())!=0x314c424fu)throw std::runtime_error("not OpenBoot bulk protocol; nothing sent");
    return status;
}
static void bulk_send(const Bytes& payload,bool executable,bool automatic=false){
    if(payload.empty()||payload.size()>0x01000000u||(executable&&(payload.size()<4u||(payload.size()&3u))))throw std::runtime_error("invalid DDR image length (ARM image must be 4-byte aligned; max 16 MiB)");
    const Bytes check_vector{'1','2','3','4','5','6','7','8','9'};
    if(crc32(check_vector)!=0xcbf43926u)throw std::runtime_error("CRC32 selftest");
    Usb u;check(libusb_init(&u.c),"libusb_init");
    if(automatic)std::cout<<"Waiting for TQ210 Auto Boot re-enumeration (reattach in VMware if necessary)...\n"<<std::flush;
    Bytes status;
    const auto ready_start=Clock::now();
    const auto deadline=ready_start+std::chrono::seconds(60);
    for(;;){
        u.h=libusb_open_device_with_vid_pid(u.c,0x04e8,0x1234);
        try{
            if(!u.h)throw std::runtime_error("OpenBoot not accessible");
            libusb_device_descriptor dd{};
            check(libusb_get_device_descriptor(libusb_get_device(u.h),&dd),"device descriptor");
            if(automatic&&dd.bcdDevice!=4)throw std::runtime_error("waiting for stage2 revision");
            const auto name=product_name(u.h,dd);
            const bool valid=(dd.bcdDevice==2&&name=="TQ210 Bulk Test"&&!executable)||
                (dd.bcdDevice==3&&name=="TQ210 Exec Test"&&!automatic)||
                (dd.bcdDevice==4&&name=="TQ210 Auto Boot");
            if(!valid||(automatic&&dd.bcdDevice!=4))
                throw std::runtime_error("wrong device identity; no bulk data sent");
            int cfg=0;check(libusb_get_configuration(u.h,&cfg),"get configuration");
            if(cfg!=1)throw std::runtime_error("configuration 1 not active");
            status=bulk_status(u.h,automatic?1000u:30000u);
            if(get32(status.data()+4)!=0||get32(status.data()+8)!=0)
                throw std::runtime_error("receiver not fresh");
            break;
        }catch(const std::exception&){
            if(!automatic)throw;
            if(u.h){libusb_close(u.h);u.h=nullptr;}
            if(std::chrono::steady_clock::now()>=deadline)
                throw std::runtime_error("Auto Boot not ready within 60s; check UART/VMware; cold reboot before retry");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    elapsed("stage2 ready / USB re-enumeration",ready_start);
    check(libusb_claim_interface(u.h,0),"claim interface");u.iface=0;
    const auto upload_start=Clock::now();
    uint32_t crc=crc32(payload);
    Bytes wire;append32(wire,0x314c424fu);append32(wire,uint32_t(payload.size()));append32(wire,crc);append32(wire,executable?1u:0u);
    wire.insert(wire.end(),payload.begin(),payload.end());
    std::printf("OpenBoot EP2 test: %zu payload bytes CRC32=%08x\n",payload.size(),unsigned(crc));
    for(size_t i=0;i<wire.size();){
        int chunk=int(std::min<size_t>(65536,wire.size()-i)),sent=0;
        int rc=libusb_bulk_transfer(u.h,2,wire.data()+i,chunk,&sent,15000);
        check(rc,"bulk OUT");if(sent!=chunk)throw std::runtime_error("short write; cold reboot before retry");
        i+=size_t(sent);std::printf("Sent %zu/%zu\n",i,wire.size());std::fflush(stdout);
    }
    elapsed("DDR upload (including host CRC)",upload_start);
    const auto verify_start=Clock::now();
    for(unsigned tries=0;tries<30;tries++){
        status=bulk_status(u.h);
        uint32_t state=get32(status.data()+4),got=get32(status.data()+8),len=get32(status.data()+12),actual=get32(status.data()+16);
        if(state==3)throw std::runtime_error("device reported DDR verify failure");
        if(state==2){
            if(got!=payload.size()||len!=payload.size()||actual!=crc)throw std::runtime_error("verification result mismatch");
            std::printf("BULK DDR VERIFY PASS: %u bytes CRC32=%08x\n",unsigned(got),unsigned(actual));
            elapsed("DDR verification status",verify_start);
            if(automatic){
                const auto go_start=Clock::now();
                int rc=libusb_control_transfer(u.h,0x40,0x5b,0x4f42,0x4c31,nullptr,0,5000);
                check(rc,"USB GO (if disconnected, check UART before retrying)");
                if(rc!=0)throw std::runtime_error("unexpected USB GO response");
                elapsed("USB GO acknowledgement",go_start);
                std::cout<<"USB GO acknowledged; CPU execution must be confirmed on UART.\n";
                return;
            }
            if(executable)std::cout<<"DDR image at 0x24000000; send lowercase x on UART to execute (not executed yet).\n";
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("device verification did not complete");
}

static void bulk_test(){
    Bytes payload((1u<<20)+13u);
    uint32_t seed=0x21012345u;
    for(auto& x:payload){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;x=uint8_t(seed);}
    bulk_send(payload,false);
}
static Bytes read_image(const char* path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("cannot open DDR image");
    const auto size=f.tellg();
    if(size<4||size>0x01000000)throw std::runtime_error("invalid DDR image size");
    Bytes payload(static_cast<size_t>(size));
    f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(payload.data()),static_cast<std::streamsize>(size)))throw std::runtime_error("cannot read DDR image");
    if(payload.size()&3u)throw std::runtime_error("ARM image length must be 4-byte aligned");
    return payload;
}
static void bulk_load(const char* path){bulk_send(read_image(path),true);}
static void auto_boot(const char* loader,const char* app){
    Bytes payload=read_image(app);
    std::ifstream f(loader,std::ios::binary);
    if(!f)throw std::runtime_error("cannot open SRAM loader");
    Bytes first((std::istreambuf_iterator<char>(f)),{});
    if(first.empty()||first.size()>11168)throw std::runtime_error("SRAM loader must fit vendor-size 11168-byte frame");
    first.resize(11168,0);
    auto p=packets(first,0xd0020010,false,false);
    std::cout<<"Stage 1: sending SRAM loader to cold ROM\n"<<std::flush;
    const auto total_start=Clock::now();
    send(p,true,true);
    elapsed("stage1 ROM upload",total_start);
    bulk_send(payload,true,true);
    elapsed("total through USB GO ACK",total_start);
}
static void handoff_notice(libusb_device_handle* h,const char* expected,const char* label,unsigned timeout=5000){
    Bytes buf(512);int got=0;
    const int rc=libusb_bulk_transfer(h,0x81,buf.data(),int(buf.size()),&got,timeout);
    check(rc,label);
    if(got!=16||std::string(reinterpret_cast<const char*>(buf.data()),size_t(got))!=expected)
        throw std::runtime_error(std::string(label)+": unexpected response; cold reboot");
    std::cout<<label<<" OK\n"<<std::flush;
}
static void handoff_test(const char* loader){
    std::ifstream f(loader,std::ios::binary);
    if(!f)throw std::runtime_error("cannot open handoff SRAM loader");
    Bytes first((std::istreambuf_iterator<char>(f)),{});
    if(first.empty()||first.size()>11168)throw std::runtime_error("handoff loader size");
    first.resize(11168,0);
    auto p=packets(first,0xd0020010,false,false);
    Usb u;
    const auto start=Clock::now();
    send_on(u,p,true,true,true);
    elapsed("stage1 ROM upload",start);
    const auto ready_start=Clock::now();
    std::cout<<"Waiting for USB EP1 readiness marker on the same handle...\n"<<std::flush;
    handoff_notice(u.h,"OB-HANDOFF-RDY1!","HANDOFF READY");
    elapsed("same-handle readiness",ready_start);
    const auto transfer_start=Clock::now();
    for(unsigned packet=0;packet<2;packet++){
        Bytes data(packet?37u:512u);
        for(size_t i=0;i<data.size();i++)data[i]=uint8_t(i*37u+packet*91u+17u);
        int sent=0;
        int rc=libusb_bulk_transfer(u.h,2,data.data(),int(data.size()),&sent,3000);
        check(rc,"same-handle EP2 transfer");
        if(sent!=int(data.size()))throw std::runtime_error("short transfer; cold reboot");
        std::printf("Same handle packet %u: %d bytes\n",packet+1,sent);
    }
    elapsed("same-handle 549-byte transfer",transfer_start);
    handoff_notice(u.h,"OB-HANDOFF-OK01!","HANDOFF DDR VERIFY PASS");
    elapsed("total same-handle handoff",start);
    std::cout<<"No re-enumeration; both packets verified by device. Cold reboot before next test.\n";
}
static void dma_write(libusb_device_handle* h,unsigned char* data,int size,const char* label){
    int sent=0;
    const int rc=libusb_bulk_transfer(h,2,data,size,&sent,15000);
    check(rc,label);
    if(sent!=size)throw std::runtime_error(std::string(label)+": short transfer; cold reboot");
}
static void dma_boot(const char* loader,Bytes payload,bool executable){
    if(payload.empty()||payload.size()>0x01000000u||
       (executable&&(payload.size()<4u||(payload.size()&3u))))
        throw std::runtime_error("invalid DMA image size");
    std::ifstream f(loader,std::ios::binary);
    if(!f)throw std::runtime_error("cannot open DMA SRAM loader");
    Bytes first((std::istreambuf_iterator<char>(f)),{});
    if(first.empty()||first.size()>11168)throw std::runtime_error("DMA SRAM loader size");
    first.resize(11168,0);
    auto p=packets(first,0xd0020010,false,false);
    const uint32_t crc=crc32(payload);
    Bytes header;
    append32(header,0x324c424fu);
    append32(header,uint32_t(payload.size()));
    append32(header,crc);
    append32(header,executable?1u:0u);
    Usb u;
    const auto start=Clock::now();
    send_on(u,p,true,true,true);
    elapsed("stage1 ROM upload",start);
    const auto ready=Clock::now();
    handoff_notice(u.h,"OB-DMA-BOOT-RDY!","DMA BOOT READY");
    elapsed("same-handle DDR / USB readiness",ready);
    dma_write(u.h,header.data(),int(header.size()),"DMA header");
    handoff_notice(u.h,"OB-DMA-HEADEROK!","DMA HEADER");
    std::printf("DMA image: %zu bytes CRC32=%08x\n",payload.size(),unsigned(crc));
    const auto upload=Clock::now();
    for(size_t offset=0;offset<payload.size();){
        int n=int(std::min<size_t>(65536,payload.size()-offset));
        dma_write(u.h,payload.data()+offset,n,"DMA EP2 image");
        offset+=size_t(n);
        if(offset==payload.size()||(offset&0xfffffu)==0){
            std::printf("DMA sent %zu/%zu\n",offset,payload.size());std::fflush(stdout);
        }
    }
    elapsed("DMA image upload",upload);
    const double upload_seconds=std::chrono::duration<double>(Clock::now()-upload).count();
    if(upload_seconds>0.0)std::printf("[RATE] DMA payload: %.2f MiB/s\n",double(payload.size())/(1048576.0*upload_seconds));
    const auto verify=Clock::now();
    handoff_notice(u.h,"OB-DMA-VERIFYOK!","DMA DDR VERIFY PASS",30000);
    elapsed("DDR CRC32 verification",verify);
    const double verify_seconds=std::chrono::duration<double>(Clock::now()-verify).count();
    if(verify_seconds>0.0)std::printf("[RATE] DDR verify including status: %.2f MiB/s\n",double(payload.size())/(1048576.0*verify_seconds));
    if(executable){
        Bytes go{'O','B','-','D','M','A','-','E','X','E','C','-','G','O','1','!'};
        const auto launch=Clock::now();
        dma_write(u.h,go.data(),int(go.size()),"DMA GO");
        handoff_notice(u.h,"OB-DMA-GO-ACK01!","DMA GO ACK",30000);
        elapsed("GO and second DDR CRC32",launch);
        std::cout<<"CPU execution must be confirmed on UART.\n";
    }
    elapsed("total same-handle boot / verify",start);
}
static Bytes dma_test_payload(){
    Bytes payload((1u<<20)+13u);
    uint32_t seed=0x21012345u;
    for(auto& x:payload){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;x=uint8_t(seed);}
    return payload;
}
int main(int argc,char** argv){try{
    if(argc==4&&std::string(argv[1])=="--dma-boot"){dma_boot(argv[2],read_image(argv[3]),true);return 0;}
    if(argc==3&&std::string(argv[1])=="--dma-test"){dma_boot(argv[2],dma_test_payload(),false);return 0;}
    if(argc==3&&std::string(argv[1])=="--handoff-test"){handoff_test(argv[2]);return 0;}
    if(argc==4&&std::string(argv[1])=="--boot"){auto_boot(argv[2],argv[3]);return 0;}
    if(argc==3&&std::string(argv[1])=="--load"){bulk_load(argv[2]);return 0;}
    if(argc==2&&std::string(argv[1])=="--bulk-test"){bulk_test();return 0;}
    bool dry=false,full=false,join=false,pad=false,reconfig=true;std::string path;
    for(int i=1;i<argc;i++){std::string a=argv[i];if(a=="--selftest"){selftest();return 0;}else if(a=="--dry-run")dry=true;else if(a=="--full-checksum")full=true;else if(a=="--join-tail")join=true;else if(a=="--pad-vendor-size")pad=true;else if(a=="--no-reconfigure")reconfig=false;else if(!a.empty()&&a[0]!='-'&&path.empty())path=a;else throw std::runtime_error("unknown argument: "+a);}
    if(path.empty()){std::cout<<"Usage: tq210-dnw IMAGE [--dry-run] [--full-checksum] [--join-tail] [--pad-vendor-size] [--no-reconfigure]\n       tq210-dnw --selftest\n       tq210-dnw --bulk-test\n       tq210-dnw --load DDR_IMAGE\n       tq210-dnw --boot SRAM_LOADER DDR_IMAGE\n       tq210-dnw --handoff-test SRAM_LOADER\n       tq210-dnw --dma-boot SRAM_LOADER DDR_IMAGE\n       tq210-dnw --dma-test SRAM_LOADER\n";return 1;}
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot open image");Bytes b((std::istreambuf_iterator<char>(f)),{});if(b.empty()||b.size()>16368)throw std::runtime_error("invalid image length");
    if(pad){if(b.size()>11168)throw std::runtime_error("cannot pad to vendor size");b.resize(11168,0);}
    auto p=packets(b,0xd0020010,full,join);auto& t=p.back();unsigned sum=unsigned(t[t.size()-2])|(unsigned(t.back())<<8);
    std::printf("SRAM address=0xd0020010 payload=%zu wire=%zu checksum=0x%04x\n",b.size(),b.size()+10,sum);std::cout<<"Transfers:";for(auto& x:p)std::cout<<" "<<x.size();std::cout<<"\n";
    if(!dry) send(p,reconfig);
    return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
