#include "dnw.hpp"
int main(int argc, char** argv)
{
    try
    {
        if (argc == 4 && std::string(argv[1]) == "--dma-boot")
        {
            dma_boot(argv[2], read_image(argv[3]), true);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--dma-test")
        {
            dma_boot(argv[2], dma_test_payload(), false);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--handoff-test")
        {
            handoff_test(argv[2]);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--boot")
        {
            auto_boot(argv[2], argv[3]);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--load")
        {
            bulk_load(argv[2]);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--bulk-test")
        {
            bulk_test();
            return 0;
        }
        bool dry = false, full = false, join = false, pad = false, reconfig = true;
        std::string path;
        for (int i = 1; i<argc; i++)
        {
            std::string a = argv[i];
            if (a == "--selftest")
            {
                selftest();
                return 0;
            }
            else if (a == "--dry-run")
            {
                dry = true;
            }
            else if (a == "--full-checksum")
            {
                full = true;
            }
            else if (a == "--join-tail")
            {
                join = true;
            }
            else if (a == "--pad-vendor-size")
            {
                pad = true;
            }
            else if (a == "--no-reconfigure")
            {
                reconfig = false;
            }
            else if (!a.empty() && a[0] != '-' && path.empty())
            {
                path = a;
            }
            else
            {
                throw std::runtime_error("unknown argument: "+a);
            }
        }
        if (path.empty())
        {
            std::cout << "Usage: tq210-dnw IMAGE [--dry-run] [--full-checksum] [--join-tail] [--pad-vendor-size] [--no-reconfigure]\n       tq210-dnw --selftest\n       tq210-dnw --bulk-test\n       tq210-dnw --load DDR_IMAGE\n       tq210-dnw --boot SRAM_LOADER DDR_IMAGE\n       tq210-dnw --handoff-test SRAM_LOADER\n       tq210-dnw --dma-boot SRAM_LOADER DDR_IMAGE\n       tq210-dnw --dma-test SRAM_LOADER\n";
            return 1;
        }
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            throw std::runtime_error("cannot open image");
        }
        Bytes b((std::istreambuf_iterator<char>(f)), {});
        if (b.empty() || b.size()>16368)
        {
            throw std::runtime_error("invalid image length");
        }
        if (pad)
        {
            if (b.size()>11168)
            {
                throw std::runtime_error("cannot pad to vendor size");
            }
            b.resize(11168, 0);
        }
        auto p = packets(b, 0xd0020010, full, join);
        auto& t = p.back();
        unsigned sum = unsigned(t[t.size()-2]) | (unsigned(t.back()) << 8);
        std::printf("SRAM address=0xd0020010 payload=%zu wire=%zu checksum=0x%04x\n", b.size(), b.size()+10, sum);
        std::cout << "Transfers:";
        for (auto& x:p)
        {
            std::cout << " " << x.size();
        }
        std::cout << "\n";
        if (!dry)
        {
            send(p, reconfig);
        }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
