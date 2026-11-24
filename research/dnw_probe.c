#include <stdio.h>
#include <stdint.h>
#include <libusb-1.0/libusb.h>

int main(void) {
    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;
    struct libusb_config_descriptor *cfg = NULL;
    int rc = libusb_init(&ctx), result = 1;
    if (rc < 0) { fprintf(stderr, "%s\n", libusb_error_name(rc)); return 1; }
    h = libusb_open_device_with_vid_pid(ctx, 0x04e8, 0x1234);
    if (!h) { fprintf(stderr, "04e8:1234 not accessible\n"); goto done; }
    libusb_device *dev = libusb_get_device(h);
    int active = 0;
    rc = libusb_get_configuration(h, &active);
    if (rc < 0) { fprintf(stderr, "%s\n", libusb_error_name(rc)); goto done; }
    printf("Bus %u device %u speed enum %d configuration %d\n",
           libusb_get_bus_number(dev), libusb_get_device_address(dev),
           libusb_get_device_speed(dev), active);
    rc = libusb_get_active_config_descriptor(dev, &cfg);
    if (rc < 0) { fprintf(stderr, "%s\n", libusb_error_name(rc)); goto done; }
    for (int i=0; i<cfg->bNumInterfaces; ++i) {
        const struct libusb_interface *it = &cfg->interface[i];
        for (int j=0; j<it->num_altsetting; ++j) {
            const struct libusb_interface_descriptor *alt = &it->altsetting[j];
            printf("Interface %u alt %u class 0x%02x\n",
                   alt->bInterfaceNumber, alt->bAlternateSetting, alt->bInterfaceClass);
            for (int k=0; k<alt->bNumEndpoints; ++k) {
                const struct libusb_endpoint_descriptor *ep = &alt->endpoint[k];
                printf("  EP 0x%02x %s type %u max packet %u\n",
                       ep->bEndpointAddress, ep->bEndpointAddress & 128 ? "IN" : "OUT",
                       ep->bmAttributes & 3, ep->wMaxPacketSize & 2047);
            }
        }
    }
    result = 0;
done:
    if (cfg) libusb_free_config_descriptor(cfg);
    if (h) libusb_close(h);
    libusb_exit(ctx);
    return result;
}
