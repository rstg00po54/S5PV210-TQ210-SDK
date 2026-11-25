#include "dnw.hpp"
void elapsed(const char* label, Clock::time_point start)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
    std::printf("[TIME] %s: %lld ms\n", label, static_cast<long long>(ms));
    std::fflush(stdout);
}
void check(int rc, const char* label)
{
    if (rc<0)
    {
        throw std::runtime_error(std::string(label)+": "+libusb_error_name(rc));
    }
}
Usb::~Usb()
{
    if (h)
    {
        if (iface >= 0)
        {
            libusb_release_interface(h, iface);
        }
        libusb_close(h);
    }
    if (c)
    {
        libusb_exit(c);
    }
}
std::string product_name(libusb_device_handle* h, libusb_device_descriptor& dd)
{
    check(libusb_get_device_descriptor(libusb_get_device(h), &dd), "device descriptor");
    if (!dd.iProduct)
    {
        throw std::runtime_error("no USB product string");
    }
    unsigned char name[128]{};
    int n = libusb_get_string_descriptor_ascii(h, dd.iProduct, name, sizeof(name));
    check(n, "product string");
    return std::string(reinterpret_cast<char*>(name), size_t(n));
}
void send_on(Usb& u, Packets& p, bool reconfig, bool strict_rom, bool handoff)
{
    check(libusb_init(&u.c), "libusb_init");
    u.h = libusb_open_device_with_vid_pid(u.c, 0x04e8, 0x1234);
    if (!u.h)
    {
        throw std::runtime_error("04e8:1234 not accessible; check USB boot/VM attachment/permissions");
    }
    if (strict_rom)
    {
        libusb_device_descriptor dd{};
        const auto name = product_name(u.h, dd);
        if (name != "SEC S5PC110 Test B/D")
        {
            throw std::runtime_error("expected cold Samsung ROM device, got "+name+"; nothing sent");
        }
    }
    if (reconfig)
    {
        check(libusb_set_configuration(u.h, 1), "SET_CONFIGURATION(1)");
    }
    libusb_config_descriptor* cfg = nullptr;
    check(libusb_get_active_config_descriptor(libusb_get_device(u.h), &cfg), "get active config");
    int iface = -1, ep = -1, count = 0;
    bool handoff_pair = false;
    for (int i = 0; i<cfg->bNumInterfaces; i++)
    {
        for (int j = 0; j<cfg->interface[i].num_altsetting; j++)
        {
            auto& a = cfg->interface[i].altsetting[j];
            if (a.bAlternateSetting || a.bInterfaceClass != 255)
            {
                continue;
            }
            bool in1 = false, out2 = false;
            for (int k = 0; k<a.bNumEndpoints; k++)
            {
                auto& e = a.endpoint[k];
                if ((e.bmAttributes&3) == 2 && !(e.bEndpointAddress&128))
                {
                    iface = a.bInterfaceNumber;
                    ep = e.bEndpointAddress;
                    count++;
                }
                if ((e.bmAttributes&3) == 2 && e.wMaxPacketSize == 512)
                {
                    if (e.bEndpointAddress == 0x81)
                    {
                        in1 = true;
                    }
                    if (e.bEndpointAddress == 0x02)
                    {
                        out2 = true;
                    }
                }
            }
            if (in1 && out2)
            {
                handoff_pair = true;
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    if (count != 1)
    {
        throw std::runtime_error("expected exactly one vendor bulk OUT endpoint");
    }
    if (handoff && (!handoff_pair || ep != 2))
    {
        throw std::runtime_error("ROM does not advertise expected HS bulk IN 0x81 / OUT 0x02; no image sent");
    }
    check(libusb_claim_interface(u.h, iface), "claim interface");
    u.iface = iface;
    std::printf("interface=%d OUT=0x%02x\n", iface, ep);
    for (size_t i = 0; i<p.size(); i++)
    {
        int sent = 0;
        int rc = libusb_bulk_transfer(u.h, uint8_t(ep), p[i].data(), int(p[i].size()), &sent, 10000);
        std::printf("transfer %zu: %d/%zu %s\n", i, sent, p[i].size(), libusb_error_name(rc));
        std::fflush(stdout);
        check(rc, "bulk OUT (power-cycle before retry)");
        if (sent != int(p[i].size()))
        {
            throw std::runtime_error("short transfer; power-cycle before retry");
        }
    }
    std::cout << "USB transport complete; CPU execution NOT confirmed. Check UART0.\n";
}
void send(Packets& p, bool reconfig, bool strict_rom)
{
    Usb u;
    send_on(u, p, reconfig, strict_rom);
}
