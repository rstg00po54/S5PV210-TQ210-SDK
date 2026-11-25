#pragma once
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
#include "usb_abi.hpp"

using Bytes = std::vector<uint8_t>;
using Packets = std::vector<Bytes>;
using Clock = std::chrono::steady_clock;

struct Usb {
    libusb_context *c = nullptr;
    libusb_device_handle *h = nullptr;
    int iface = -1;

    Usb() = default;
    Usb(const Usb &) = delete;
    Usb &operator=(const Usb &) = delete;
    ~Usb();
};

void elapsed(const char *label, Clock::time_point start);
void append32(Bytes &p, uint32_t n);
Packets packets(const Bytes &image, uint32_t addr, bool full, bool join);
void check(int rc, const char *label);
std::string product_name(libusb_device_handle *h, libusb_device_descriptor &dd);
void send_on(Usb &u, Packets &p, bool reconfig, bool strict_rom = false, bool handoff = false);
void send(Packets &p, bool reconfig, bool strict_rom = false);
void selftest();
uint32_t crc32(const Bytes &b);
uint32_t get32(const uint8_t *p);
Bytes read_image(const char *path);
void bulk_test();
void bulk_load(const char *path);
void auto_boot(const char *loader, const char *app);
void handoff_test(const char *loader);
void dma_boot(const char *loader, Bytes payload, bool executable);
Bytes dma_test_payload();
