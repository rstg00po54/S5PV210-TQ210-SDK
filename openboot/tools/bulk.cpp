#include "dnw.hpp"
static Bytes bulk_status(libusb_device_handle* h, unsigned timeout = 30000)
{
    Bytes status(20);
    int n = libusb_control_transfer(h, 0xc0, 0x5a, 0x4f42, 0x4c31, status.data(), 20, timeout);
    check(n, "OpenBoot protocol status");
    if (n != 20 || get32(status.data()) != 0x314c424fu)
    {
        throw std::runtime_error("not OpenBoot bulk protocol; nothing sent");
    }
    return status;
}
static void bulk_send(const Bytes& payload, bool executable, bool automatic = false)
{
    if (payload.empty() || payload.size()>0x01000000u || (executable && (payload.size()<4u || (payload.size()&3u))))
    {
        throw std::runtime_error("invalid DDR image length (ARM image must be 4-byte aligned; max 16 MiB)");
    }
    const Bytes check_vector{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    if (crc32(check_vector) != 0xcbf43926u)
    {
        throw std::runtime_error("CRC32 selftest");
    }
    Usb u;
    check(libusb_init(&u.c), "libusb_init");
    if (automatic)
    {
        std::cout << "Waiting for TQ210 Auto Boot re-enumeration (reattach in VMware if necessary)...\n" << std::flush;
    }
    Bytes status;
    const auto ready_start = Clock::now();
    const auto deadline = ready_start+std::chrono::seconds(60);
    for (;;)
    {
        u.h = libusb_open_device_with_vid_pid(u.c, 0x04e8, 0x1234);
        try
        {
            if (!u.h)
            {
                throw std::runtime_error("OpenBoot not accessible");
            }
            libusb_device_descriptor dd{};
            check(libusb_get_device_descriptor(libusb_get_device(u.h), &dd), "device descriptor");
            if (automatic && dd.bcdDevice != 4)
            {
                throw std::runtime_error("waiting for stage2 revision");
            }
            const auto name = product_name(u.h, dd);
            const bool valid = (dd.bcdDevice == 2 && name == "TQ210 Bulk Test" && !executable) ||
            (dd.bcdDevice == 3 && name == "TQ210 Exec Test" && !automatic) ||
            (dd.bcdDevice == 4 && name == "TQ210 Auto Boot");
            if (!valid || (automatic && dd.bcdDevice != 4))
            {
                throw std::runtime_error("wrong device identity; no bulk data sent");
            }
            int cfg = 0;
            check(libusb_get_configuration(u.h, &cfg), "get configuration");
            if (cfg != 1)
            {
                throw std::runtime_error("configuration 1 not active");
            }
            status = bulk_status(u.h, automatic ? 1000u:30000u);
            if (get32(status.data()+4) != 0 || get32(status.data()+8) != 0)
            {
                throw std::runtime_error("receiver not fresh");
            }
            break;
        }
        catch (const std::exception&)
        {
            if (!automatic)
            {
                throw;
            }
            if (u.h)
            {
                libusb_close(u.h);
                u.h = nullptr;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                throw std::runtime_error("Auto Boot not ready within 60s; check UART/VMware; cold reboot before retry");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    elapsed("stage2 ready / USB re-enumeration", ready_start);
    check(libusb_claim_interface(u.h, 0), "claim interface");
    u.iface = 0;
    const auto upload_start = Clock::now();
    uint32_t crc = crc32(payload);
    Bytes wire;
    append32(wire, 0x314c424fu);
    append32(wire, uint32_t(payload.size()));
    append32(wire, crc);
    append32(wire, executable ? 1u:0u);
    wire.insert(wire.end(), payload.begin(), payload.end());
    std::printf("OpenBoot EP2 test: %zu payload bytes CRC32=%08x\n", payload.size(), unsigned(crc));
    for (size_t i = 0; i<wire.size();)
    {
        int chunk = int(std::min<size_t>(65536, wire.size()-i)), sent = 0;
        int rc = libusb_bulk_transfer(u.h, 2, wire.data()+i, chunk, &sent, 15000);
        check(rc, "bulk OUT");
        if (sent != chunk)
        {
            throw std::runtime_error("short write; cold reboot before retry");
        }
        i += size_t(sent);
        std::printf("Sent %zu/%zu\n", i, wire.size());
        std::fflush(stdout);
    }
    elapsed("DDR upload (including host CRC)", upload_start);
    const auto verify_start = Clock::now();
    for (unsigned tries = 0; tries<30; tries++)
    {
        status = bulk_status(u.h);
        uint32_t state = get32(status.data()+4), got = get32(status.data()+8), len = get32(status.data()+12), actual = get32(status.data()+16);
        if (state == 3)
        {
            throw std::runtime_error("device reported DDR verify failure");
        }
        if (state == 2)
        {
            if (got != payload.size() || len != payload.size() || actual != crc)
            {
                throw std::runtime_error("verification result mismatch");
            }
            std::printf("BULK DDR VERIFY PASS: %u bytes CRC32=%08x\n", unsigned(got), unsigned(actual));
            elapsed("DDR verification status", verify_start);
            if (automatic)
            {
                const auto go_start = Clock::now();
                int rc = libusb_control_transfer(u.h, 0x40, 0x5b, 0x4f42, 0x4c31, nullptr, 0, 5000);
                check(rc, "USB GO (if disconnected, check UART before retrying)");
                if (rc != 0)
                {
                    throw std::runtime_error("unexpected USB GO response");
                }
                elapsed("USB GO acknowledgement", go_start);
                std::cout << "USB GO acknowledged; CPU execution must be confirmed on UART.\n";
                return;
            }
            if (executable)
            {
                std::cout << "DDR image at 0x24000000; send lowercase x on UART to execute (not executed yet).\n";
            }
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("device verification did not complete");
}
void bulk_test()
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
    bulk_send(payload, false);
}
void bulk_load(const char* path)
{
    bulk_send(read_image(path), true);
}
void auto_boot(const char* loader, const char* app)
{
    Bytes payload = read_image(app);
    std::ifstream f(loader, std::ios::binary);
    if (!f)
    {
        throw std::runtime_error("cannot open SRAM loader");
    }
    Bytes first((std::istreambuf_iterator<char>(f)), {});
    if (first.empty() || first.size()>11168)
    {
        throw std::runtime_error("SRAM loader must fit vendor-size 11168-byte frame");
    }
    first.resize(11168, 0);
    auto p = packets(first, 0xd0020010, false, false);
    std::cout << "Stage 1: sending SRAM loader to cold ROM\n" << std::flush;
    const auto total_start = Clock::now();
    send(p, true, true);
    elapsed("stage1 ROM upload", total_start);
    bulk_send(payload, true, true);
    elapsed("total through USB GO ACK", total_start);
}
