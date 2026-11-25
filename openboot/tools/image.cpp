#include "dnw.hpp"
void append32(Bytes& p, uint32_t n)
{
    for (int i = 0; i<4; i++)p.push_back(uint8_t(n>>(8*i)));
}
Packets packets(const Bytes& image, uint32_t addr, bool full, bool join)
{
    if (image.empty() || image.size()>16368) throw std::runtime_error("invalid SRAM image length (1..16368)");
    Bytes first;
    append32(first, addr);
    append32(first, uint32_t(image.size()+10));
    auto n = std::min<size_t>(2040, image.size());
    first.insert(first.end(), image.begin(), image.begin()+n);
    Packets p{first};
    for (size_t i = n; i<image.size(); i += 2048)p.emplace_back(image.begin()+i, image.begin()+std::min(i+2048, image.size()));
    unsigned sum = std::accumulate(image.begin(), image.begin()+(full ? image.size():std::min<size_t>(4096, image.size())), 0u);
    Bytes tail{uint8_t(sum), uint8_t(sum>>8)};
    if (join)p.back().insert(p.back().end(), tail.begin(), tail.end());
    else p.push_back(tail);
    return p;
}
void selftest()
{
    Bytes b(5120);
    for (size_t i = 0; i<b.size(); i++)b[i] = uint8_t(i);
    auto p = packets(b, 0xd0020010, false, false);
    if (p.size() != 4 || p[0].size() != 2048 || p[1].size() != 2048 || p[2].size() != 1032 || p[3].size() != 2 || p[0][0] != 0x10 || p[0][3] != 0xd0 || p[0][4] != 0x0a || p[0][5] != 0x14)throw std::runtime_error("packet layout");
    Bytes reconstructed(p[0].begin()+8, p[0].end());
    for (size_t i = 1; i+1<p.size(); i++)reconstructed.insert(reconstructed.end(), p[i].begin(), p[i].end());
    if (reconstructed != b)throw std::runtime_error("payload mismatch");
    if (p[3] != Bytes{0x00, 0xf8} || packets(b, 0xd0020010, true, false).back() != Bytes{0x00, 0xf6})throw std::runtime_error("checksum mismatch");
    auto q = packets(Bytes{1}, 0xd0020010, true, true);
    if (q.size() != 1 || q[0].size() != 11 || q[0][9] != 1 || q[0][10] != 0)throw std::runtime_error("joined tail mismatch");
    std::cout<<"Packet selftest PASS\n";
}
uint32_t crc32(const Bytes& b)
{
    uint32_t crc = 0xffffffffu;
    for (uint8_t x:b)
    {
        crc ^= x;
        for (unsigned bit = 0; bit<8; bit++)crc =(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return ~crc;
}
uint32_t get32(const uint8_t* p)
{
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
Bytes read_image(const char* path)
{
    std::ifstream f(path, std::ios::binary|std::ios::ate);
    if (!f)throw std::runtime_error("cannot open DDR image");
    const auto size = f.tellg();
    if (size<4 || size>0x01000000)throw std::runtime_error("invalid DDR image size");
    Bytes payload(static_cast<size_t>(size));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(size)))throw std::runtime_error("cannot read DDR image");
    if (payload.size()&3u)throw std::runtime_error("ARM image length must be 4-byte aligned");
    return payload;
}
