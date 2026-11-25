#pragma once
#include <cstdint>
#if __has_include(<libusb-1.0/libusb.h>)
#include <libusb-1.0/libusb.h>
#else
extern "C"
{
    struct libusb_context;
    struct libusb_device;
    struct libusb_device_handle;
    struct libusb_endpoint_descriptor
    {
        uint8_t bLength, bDescriptorType, bEndpointAddress, bmAttributes;
        uint16_t wMaxPacketSize;
        uint8_t bInterval, bRefresh, bSynchAddress;
        const unsigned char* extra;
        int extra_length;
    };
    struct libusb_interface_descriptor
    {
        uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
        const libusb_endpoint_descriptor* endpoint;
        const unsigned char* extra;
        int extra_length;
    };
    struct libusb_interface
    {
        const libusb_interface_descriptor* altsetting;
        int num_altsetting;
    };
    struct libusb_config_descriptor
    {
        uint8_t bLength, bDescriptorType;
        uint16_t wTotalLength;
        uint8_t bNumInterfaces, bConfigurationValue, iConfiguration, bmAttributes, MaxPower;
        const libusb_interface* interface;
        const unsigned char* extra;
        int extra_length;
    };
    int libusb_init(libusb_context**);
    void libusb_exit(libusb_context*);
    libusb_device_handle* libusb_open_device_with_vid_pid(libusb_context*, uint16_t, uint16_t);
    void libusb_close(libusb_device_handle*);
    libusb_device* libusb_get_device(libusb_device_handle*);
    struct libusb_device_descriptor
    {
        uint8_t bLength, bDescriptorType;
        uint16_t bcdUSB;
        uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
        uint16_t idVendor, idProduct, bcdDevice;
        uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
    };
    int libusb_get_device_descriptor(libusb_device*, libusb_device_descriptor*);
    int libusb_get_string_descriptor_ascii(libusb_device_handle*, uint8_t, unsigned char*, int);
    int libusb_control_transfer(libusb_device_handle*, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char*, uint16_t, unsigned int);
    int libusb_get_configuration(libusb_device_handle*, int*);
    int libusb_set_configuration(libusb_device_handle*, int);
    int libusb_get_active_config_descriptor(libusb_device*, libusb_config_descriptor**);
    void libusb_free_config_descriptor(libusb_config_descriptor*);
    int libusb_claim_interface(libusb_device_handle*, int);
    int libusb_release_interface(libusb_device_handle*, int);
    int libusb_bulk_transfer(libusb_device_handle*, unsigned char, unsigned char*, int, int*, unsigned int);
    const char* libusb_error_name(int);
}
#endif
