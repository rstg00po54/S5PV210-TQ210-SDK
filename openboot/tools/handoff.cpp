#include "dnw.hpp"
static void handoff_notice(libusb_device_handle* h, const char* expected, const char* label, unsigned timeout = 5000)
{
    Bytes buf(512);
    int got = 0;
    const int rc = libusb_bulk_transfer(h, 0x81, buf.data(), int(buf.size()), &got, timeout);
    check(rc, label);
    if (got != 16 || std::string(reinterpret_cast<const char*>(buf.data()), size_t(got)) != expected)
    {
        throw std::runtime_error(std::string(label)+": unexpected response; cold reboot");
    }
    std::cout << label << " OK\n" << std::flush;
}
void handoff_test(const char* loader)
{
    std::ifstream f(loader, std::ios::binary);
    if (!f)
    {
        throw std::runtime_error("cannot open handoff SRAM loader");
    }
    Bytes first((std::istreambuf_iterator<char>(f)), {});
    if (first.empty() || first.size()>11168)
    {
        throw std::runtime_error("handoff loader size");
    }
    first.resize(11168, 0);
    auto p = packets(first, 0xd0020010, false, false);
    Usb u;
    const auto start = Clock::now();
    send_on(u, p, true, true, true);
    elapsed("stage1 ROM upload", start);
    const auto ready_start = Clock::now();
    std::cout << "Waiting for USB EP1 readiness marker on the same handle...\n" << std::flush;
    handoff_notice(u.h, "OB-HANDOFF-RDY1!", "HANDOFF READY");
    elapsed("same-handle readiness", ready_start);
    const auto transfer_start = Clock::now();
    for (unsigned packet = 0; packet<2; packet++)
    {
        Bytes data(packet ? 37u:512u);
        for (size_t i = 0; i<data.size(); i++)
        {
            data[i] = uint8_t(i*37u+packet*91u+17u);
        }
        int sent = 0;
        int rc = libusb_bulk_transfer(u.h, 2, data.data(), int(data.size()), &sent, 3000);
        check(rc, "same-handle EP2 transfer");
        if (sent != int(data.size()))
        {
            throw std::runtime_error("short transfer; cold reboot");
        }
        std::printf("Same handle packet %u: %d bytes\n", packet+1, sent);
    }
    elapsed("same-handle 549-byte transfer", transfer_start);
    handoff_notice(u.h, "OB-HANDOFF-OK01!", "HANDOFF DDR VERIFY PASS");
    elapsed("total same-handle handoff", start);
    std::cout << "No re-enumeration; both packets verified by device. Cold reboot before next test.\n";
}
static void dma_write(libusb_device_handle* h, unsigned char* data, int size, const char* label)
{
    int sent = 0;
    const int rc = libusb_bulk_transfer(h, 2, data, size, &sent, 15000);
    check(rc, label);
    if (sent != size)
    {
        throw std::runtime_error(std::string(label)+": short transfer; cold reboot");
    }
}
void dma_boot(const char* loader, Bytes payload, bool executable)
{
    if (payload.empty() || payload.size()>0x01000000u ||
        (executable && (payload.size()<4u || (payload.size()&3u))))
    {
        throw std::runtime_error("invalid DMA image size");
    }
    std::ifstream f(loader, std::ios::binary);
    if (!f)
    {
        throw std::runtime_error("cannot open DMA SRAM loader");
    }
    Bytes first((std::istreambuf_iterator<char>(f)), {});
    if (first.empty() || first.size()>11168)
    {
        throw std::runtime_error("DMA SRAM loader size");
    }
    first.resize(11168, 0);
    auto p = packets(first, 0xd0020010, false, false);
    const uint32_t crc = crc32(payload);
    Bytes header;
    append32(header, 0x324c424fu);
    append32(header, uint32_t(payload.size()));
    append32(header, crc);
    append32(header, executable ? 1u:0u);
    Usb u;
    const auto start = Clock::now();
    send_on(u, p, true, true, true);
    elapsed("stage1 ROM upload", start);
    const auto ready = Clock::now();
    handoff_notice(u.h, "OB-DMA-BOOT-RDY!", "DMA BOOT READY");
    elapsed("same-handle DDR / USB readiness", ready);
    dma_write(u.h, header.data(), int(header.size()), "DMA header");
    handoff_notice(u.h, "OB-DMA-HEADEROK!", "DMA HEADER");
    std::printf("DMA image: %zu bytes CRC32=%08x\n", payload.size(), unsigned(crc));
    const auto upload = Clock::now();
    for (size_t offset = 0; offset<payload.size();)
    {
        int n = int(std::min<size_t>(65536, payload.size()-offset));
        dma_write(u.h, payload.data()+offset, n, "DMA EP2 image");
        offset += size_t(n);
        if (offset == payload.size() || (offset&0xfffffu) == 0)
        {
            std::printf("DMA sent %zu/%zu\n", offset, payload.size());
            std::fflush(stdout);
        }
    }
    elapsed("DMA image upload", upload);
    const double upload_seconds = std::chrono::duration<double>(Clock::now()-upload).count();
    if (upload_seconds>0.0)
    {
        std::printf("[RATE] DMA payload: %.2f MiB/s\n", double(payload.size())/(1048576.0*upload_seconds));
    }
    const auto verify = Clock::now();
    handoff_notice(u.h, "OB-DMA-VERIFYOK!", "DMA DDR VERIFY PASS", 30000);
    elapsed("DDR CRC32 verification", verify);
    const double verify_seconds = std::chrono::duration<double>(Clock::now()-verify).count();
    if (verify_seconds>0.0)
    {
        std::printf("[RATE] DDR verify including status: %.2f MiB/s\n", double(payload.size())/(1048576.0*verify_seconds));
    }
    if (executable)
    {
        Bytes go{'O', 'B', '-', 'D', 'M', 'A', '-', 'E', 'X', 'E', 'C', '-', 'G', 'O', '1', '!'};
        const auto launch = Clock::now();
        dma_write(u.h, go.data(), int(go.size()), "DMA GO");
        handoff_notice(u.h, "OB-DMA-GO-ACK01!", "DMA GO ACK", 30000);
        elapsed("GO and second DDR CRC32", launch);
        std::cout << "CPU execution must be confirmed on UART.\n";
    }
    elapsed("total same-handle boot / verify", start);
}
Bytes dma_test_payload()
{
    Bytes payload((1u << 20)+13u);
    uint32_t seed = 0x21012345u;
    for (auto& x:payload)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        x = uint8_t(seed);
    }
    return payload;
}
