// probe.c — inspecciona el 8BitDo Arcade Controller for Xbox (GIP) y vuelca paquetes.
// Compilar: make   (genera build/probe)
#include <libusb.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define VID 0x2DC8
#define PID 0x202C

static unsigned char seq = 1;

static int send(libusb_device_handle *h, unsigned char ep, unsigned char *buf, int len) {
    int sent = 0;
    int r = libusb_interrupt_transfer(h, ep, buf, len, &sent, 1000);
    printf("  -> OUT %d bytes (r=%s):", len, libusb_error_name(r));
    for (int i = 0; i < len; i++) printf(" %02x", buf[i]);
    printf("\n");
    return r;
}

int main(void) {
    libusb_init_context(NULL, NULL, 0);
    libusb_device_handle *h = libusb_open_device_with_vid_pid(NULL, VID, PID);
    if (!h) { fprintf(stderr, "No se encontró el control\n"); return 1; }

    libusb_device *dev = libusb_get_device(h);
    struct libusb_config_descriptor *cfg;
    libusb_get_config_descriptor(dev, 0, &cfg);
    unsigned char ep_in = 0, ep_out = 0;
    for (int i = 0; i < cfg->bNumInterfaces; i++) {
        for (int a = 0; a < cfg->interface[i].num_altsetting; a++) {
            const struct libusb_interface_descriptor *d = &cfg->interface[i].altsetting[a];
            printf("iface %d alt %d class %02x/%02x/%02x eps %d\n", d->bInterfaceNumber,
                   d->bAlternateSetting, d->bInterfaceClass, d->bInterfaceSubClass,
                   d->bInterfaceProtocol, d->bNumEndpoints);
            for (int e = 0; e < d->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &d->endpoint[e];
                printf("   ep %02x attr %02x maxpkt %d interval %d\n", ep->bEndpointAddress,
                       ep->bmAttributes, ep->wMaxPacketSize, ep->bInterval);
                if (d->bInterfaceNumber == 0 && a == 0 && (ep->bmAttributes & 3) == 3) {
                    if (ep->bEndpointAddress & 0x80) ep_in = ep->bEndpointAddress;
                    else ep_out = ep->bEndpointAddress;
                }
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    printf("Usando ep_in=%02x ep_out=%02x\n", ep_in, ep_out);

    int r = libusb_set_configuration(h, 1);
    printf("set_configuration: %s\n", libusb_error_name(r));
    r = libusb_claim_interface(h, 0);
    printf("claim_interface(0): %s\n", libusb_error_name(r));
    if (r) return 1;

    // GIP power on
    unsigned char power_on[] = {0x05, 0x20, seq++, 0x01, 0x00};
    send(h, ep_out, power_on, sizeof power_on);

    printf("Leyendo 20 s — presiona botones y mueve la palanca...\n");
    time_t end = time(NULL) + 20;
    unsigned char buf[64];
    unsigned char last[64] = {0};
    int lastlen = 0;
    while (time(NULL) < end) {
        int n = 0;
        r = libusb_interrupt_transfer(h, ep_in, buf, sizeof buf, &n, 200);
        if (r == LIBUSB_ERROR_TIMEOUT) continue;
        if (r) { printf("read error: %s\n", libusb_error_name(r)); break; }
        if (n == lastlen && !memcmp(buf, last, n)) continue;
        memcpy(last, buf, n); lastlen = n;
        printf("<- IN %2d:", n);
        for (int i = 0; i < n; i++) printf(" %02x", buf[i]);
        printf("\n");
        fflush(stdout);
        // Responder ACK si el paquete lo pide (flag 0x10)
        if (buf[1] & 0x10) {
            unsigned char ack[] = {0x01, 0x20, buf[2], 0x09, 0x00, buf[0], 0x20, buf[3], 0, 0, 0, 0, 0};
            send(h, ep_out, ack, sizeof ack);
        }
        // Si el control se (re)anuncia, enviar power on otra vez
        if (buf[0] == 0x02) {
            unsigned char p[] = {0x05, 0x20, seq++, 0x01, 0x00};
            send(h, ep_out, p, sizeof p);
        }
    }
    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(NULL);
    return 0;
}
