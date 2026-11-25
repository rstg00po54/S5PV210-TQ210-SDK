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
static void send(Packets& p,bool reconfig){
    Usb u;check(libusb_init(&u.c),"libusb_init");u.h=libusb_open_device_with_vid_pid(u.c,0x04e8,0x1234);if(!u.h)throw std::runtime_error("04e8:1234 not accessible; check USB boot/VM attachment/permissions");
    if(reconfig)check(libusb_set_configuration(u.h,1),"SET_CONFIGURATION(1)");
    libusb_config_descriptor* cfg=nullptr;check(libusb_get_active_config_descriptor(libusb_get_device(u.h),&cfg),"get active config");
    int iface=-1,ep=-1,count=0;for(int i=0;i<cfg->bNumInterfaces;i++)for(int j=0;j<cfg->interface[i].num_altsetting;j++){
        auto& a=cfg->interface[i].altsetting[j];if(a.bAlternateSetting||a.bInterfaceClass!=255)continue;
        for(int k=0;k<a.bNumEndpoints;k++){auto& e=a.endpoint[k];if((e.bmAttributes&3)==2&&!(e.bEndpointAddress&128)){iface=a.bInterfaceNumber;ep=e.bEndpointAddress;count++;}}
    }libusb_free_config_descriptor(cfg);if(count!=1)throw std::runtime_error("expected exactly one vendor bulk OUT endpoint");
    check(libusb_claim_interface(u.h,iface),"claim interface");u.iface=iface;std::printf("interface=%d OUT=0x%02x\n",iface,ep);
    for(size_t i=0;i<p.size();i++){int sent=0;int rc=libusb_bulk_transfer(u.h,uint8_t(ep),p[i].data(),int(p[i].size()),&sent,10000);std::printf("transfer %zu: %d/%zu %s\n",i,sent,p[i].size(),libusb_error_name(rc));std::fflush(stdout);check(rc,"bulk OUT (power-cycle before retry)");if(sent!=int(p[i].size()))throw std::runtime_error("short transfer; power-cycle before retry");}
    std::cout<<"USB transport complete; CPU execution NOT confirmed. Check UART0.\n";
}
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
static Bytes bulk_status(libusb_device_handle* h){
    Bytes status(20);
    int n=libusb_control_transfer(h,0xc0,0x5a,0x4f42,0x4c31,status.data(),20,30000);
    check(n,"OpenBoot protocol status");if(n!=20||get32(status.data())!=0x314c424fu)throw std::runtime_error("not OpenBoot bulk protocol; nothing sent");
    return status;
}
static void bulk_send(const Bytes& payload,bool executable){
    if(payload.empty()||payload.size()>0x01000000u||(executable&&(payload.size()<4u||(payload.size()&3u))))throw std::runtime_error("invalid DDR image length (ARM image must be 4-byte aligned; max 16 MiB)");
    const Bytes check_vector{'1','2','3','4','5','6','7','8','9'};
    if(crc32(check_vector)!=0xcbf43926u)throw std::runtime_error("CRC32 selftest");
    Usb u;check(libusb_init(&u.c),"libusb_init");
    u.h=libusb_open_device_with_vid_pid(u.c,0x04e8,0x1234);
    if(!u.h)throw std::runtime_error("attach OpenBoot Bulk Test to Ubuntu");
    libusb_device_descriptor dd{};
    check(libusb_get_device_descriptor(libusb_get_device(u.h),&dd),"device descriptor");
    if((dd.bcdDevice!=2&&dd.bcdDevice!=3)||!dd.iProduct||(executable&&dd.bcdDevice!=3))throw std::runtime_error("wrong device revision; do not send to iROM or non-executable receiver");
    unsigned char name[128]{};
    int n=libusb_get_string_descriptor_ascii(u.h,dd.iProduct,name,sizeof(name));
    check(n,"product string");const std::string expected=dd.bcdDevice==3?"TQ210 Exec Test":"TQ210 Bulk Test";if(std::string(reinterpret_cast<char*>(name),size_t(n))!=expected)throw std::runtime_error("wrong product; nothing sent");
    int cfg=0;check(libusb_get_configuration(u.h,&cfg),"get configuration");
    if(cfg!=1)throw std::runtime_error("configuration 1 not active; attach device again");
    auto status=bulk_status(u.h);
    if(get32(status.data()+4)!=0||get32(status.data()+8)!=0)throw std::runtime_error("receiver not fresh; cold reboot before retry");
    check(libusb_claim_interface(u.h,0),"claim interface");u.iface=0;
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
    for(unsigned tries=0;tries<30;tries++){
        status=bulk_status(u.h);
        uint32_t state=get32(status.data()+4),got=get32(status.data()+8),len=get32(status.data()+12),actual=get32(status.data()+16);
        if(state==3)throw std::runtime_error("device reported DDR verify failure");
        if(state==2){
            if(got!=payload.size()||len!=payload.size()||actual!=crc)throw std::runtime_error("verification result mismatch");
            std::printf("BULK DDR VERIFY PASS: %u bytes CRC32=%08x\n",unsigned(got),unsigned(actual));
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
static void bulk_load(const char* path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("cannot open DDR image");
    const auto size=f.tellg();
    if(size<4||size>0x01000000)throw std::runtime_error("invalid DDR image size");
    Bytes payload(static_cast<size_t>(size));
    f.seekg(0);
    if(!f.read(reinterpret_cast<char*>(payload.data()),static_cast<std::streamsize>(size)))throw std::runtime_error("cannot read DDR image");
    bulk_send(payload,true);
}
int main(int argc,char** argv){try{
    if(argc==3&&std::string(argv[1])=="--load"){bulk_load(argv[2]);return 0;}
    if(argc==2&&std::string(argv[1])=="--bulk-test"){bulk_test();return 0;}
    bool dry=false,full=false,join=false,pad=false,reconfig=true;std::string path;
    for(int i=1;i<argc;i++){std::string a=argv[i];if(a=="--selftest"){selftest();return 0;}else if(a=="--dry-run")dry=true;else if(a=="--full-checksum")full=true;else if(a=="--join-tail")join=true;else if(a=="--pad-vendor-size")pad=true;else if(a=="--no-reconfigure")reconfig=false;else if(!a.empty()&&a[0]!='-'&&path.empty())path=a;else throw std::runtime_error("unknown argument: "+a);}
    if(path.empty()){std::cout<<"Usage: tq210-dnw IMAGE [--dry-run] [--full-checksum] [--join-tail] [--pad-vendor-size] [--no-reconfigure]\n       tq210-dnw --selftest\n       tq210-dnw --bulk-test\n       tq210-dnw --load DDR_IMAGE\n";return 1;}
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot open image");Bytes b((std::istreambuf_iterator<char>(f)),{});if(b.empty()||b.size()>16368)throw std::runtime_error("invalid image length");
    if(pad){if(b.size()>11168)throw std::runtime_error("cannot pad to vendor size");b.resize(11168,0);}
    auto p=packets(b,0xd0020010,full,join);auto& t=p.back();unsigned sum=unsigned(t[t.size()-2])|(unsigned(t.back())<<8);
    std::printf("SRAM address=0xd0020010 payload=%zu wire=%zu checksum=0x%04x\n",b.size(),b.size()+10,sum);std::cout<<"Transfers:";for(auto& x:p)std::cout<<" "<<x.size();std::cout<<"\n";
    if(!dry) send(p,reconfig);
    return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
