// gip_core.c — ver gip_core.h
#include "gip_core.h"

#include <ApplicationServices/ApplicationServices.h>
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define VID 0x2DC8
#define PID 0x202C
#define EP_IN 0x81
#define EP_OUT 0x01

#define STICK_DEADZONE 12000   // de 32767
#define TRIGGER_THRESHOLD 300  // de 1023

static const char *button_names[GIP_BUTTON_COUNT] = {
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "MENU", "VIEW", "HOME", "LS", "RS",
    "UP", "DOWN", "LEFT", "RIGHT", "RUP", "RDOWN", "RLEFT", "RRIGHT"
};

// Nombres canónicos -> keycode virtual de macOS (layout ANSI) y usage HID (página 0x07)
static const struct { const char *name; int code; unsigned char usage; } key_names[] = {
    {"up", 126, 0x52}, {"down", 125, 0x51}, {"left", 123, 0x50}, {"right", 124, 0x4F},
    {"a", 0, 0x04}, {"b", 11, 0x05}, {"c", 8, 0x06}, {"d", 2, 0x07}, {"e", 14, 0x08},
    {"f", 3, 0x09}, {"g", 5, 0x0A}, {"h", 4, 0x0B}, {"i", 34, 0x0C}, {"j", 38, 0x0D},
    {"k", 40, 0x0E}, {"l", 37, 0x0F}, {"m", 46, 0x10}, {"n", 45, 0x11}, {"o", 31, 0x12},
    {"p", 35, 0x13}, {"q", 12, 0x14}, {"r", 15, 0x15}, {"s", 1, 0x16}, {"t", 17, 0x17},
    {"u", 32, 0x18}, {"v", 9, 0x19}, {"w", 13, 0x1A}, {"x", 7, 0x1B}, {"y", 16, 0x1C},
    {"z", 6, 0x1D},
    {"1", 18, 0x1E}, {"2", 19, 0x1F}, {"3", 20, 0x20}, {"4", 21, 0x21}, {"5", 23, 0x22},
    {"6", 22, 0x23}, {"7", 26, 0x24}, {"8", 28, 0x25}, {"9", 25, 0x26}, {"0", 29, 0x27},
    {"space", 49, 0x2C}, {"return", 36, 0x28}, {"tab", 48, 0x2B}, {"escape", 53, 0x29},
    {"backspace", 51, 0x2A},
    {"shift", 56, 0xE1}, {"control", 59, 0xE0}, {"option", 58, 0xE2}, {"command", 55, 0xE3},
    {"rshift", 60, 0xE5}, {"rcontrol", 62, 0xE4}, {"roption", 61, 0xE6}, {"capslock", 57, 0x39},
    {"minus", 27, 0x2D}, {"equal", 24, 0x2E}, {"leftbracket", 33, 0x2F},
    {"rightbracket", 30, 0x30}, {"backslash", 42, 0x31}, {"semicolon", 41, 0x33},
    {"quote", 39, 0x34}, {"comma", 43, 0x36}, {"period", 47, 0x37}, {"slash", 44, 0x38},
    {"grave", 50, 0x35},
    {"f1", 122, 0x3A}, {"f2", 120, 0x3B}, {"f3", 99, 0x3C}, {"f4", 118, 0x3D},
    {"f5", 96, 0x3E}, {"f6", 97, 0x3F}, {"f7", 98, 0x40}, {"f8", 100, 0x41},
    {"f9", 101, 0x42}, {"f10", 109, 0x43}, {"f11", 103, 0x44}, {"f12", 111, 0x45},
};
#define KEY_COUNT ((int)(sizeof key_names / sizeof *key_names))

static const struct { const char *alias, *name; } key_aliases[] = {
    {"enter", "return"}, {"esc", "escape"}, {"alt", "option"}, {"ctrl", "control"},
    {"ralt", "roption"}, {"rctrl", "rcontrol"}, {"cmd", "command"},
};

static const int default_keymap[GIP_BUTTON_COUNT] = {
    [GIP_A] = 6, [GIP_B] = 7, [GIP_X] = 0, [GIP_Y] = 1,             // z x a s
    [GIP_LB] = 12, [GIP_RB] = 13, [GIP_LT] = 14, [GIP_RT] = 15,     // q w e r
    [GIP_MENU] = 36, [GIP_VIEW] = 48, [GIP_HOME] = 53,              // return tab escape
    [GIP_LS] = -1, [GIP_RS] = -1,
    [GIP_UP] = 126, [GIP_DOWN] = 125, [GIP_LEFT] = 123, [GIP_RIGHT] = 124,
    [GIP_RUP] = 34, [GIP_RDOWN] = 40, [GIP_RLEFT] = 38, [GIP_RRIGHT] = 37,  // i k j l
};

static int keymap[GIP_BUTTON_COUNT];
static int state[GIP_BUTTON_COUNT];
static volatile int running = 0;
static volatile int output_enabled = 1;
static CGEventFlags modifier_flags = 0;
static gip_button_cb button_cb = NULL;
static gip_status_cb status_cb = NULL;
static void *cb_ctx = NULL;

const char *gip_button_name(int b) {
    return b >= 0 && b < GIP_BUTTON_COUNT ? button_names[b] : "?";
}

int gip_key_count(void) { return KEY_COUNT; }
int gip_key_code_at(int i) { return i >= 0 && i < KEY_COUNT ? key_names[i].code : -1; }

int gip_lookup_key(const char *name) {
    for (size_t i = 0; i < sizeof key_aliases / sizeof *key_aliases; i++)
        if (!strcasecmp(key_aliases[i].alias, name)) name = key_aliases[i].name;
    for (int i = 0; i < KEY_COUNT; i++)
        if (!strcasecmp(key_names[i].name, name)) return key_names[i].code;
    if ((name[0] == 'k' || name[0] == 'K') && name[1]) {
        char *end;
        long code = strtol(name + 1, &end, 10);
        if (!*end && code >= 0 && code < 128) return (int)code;
    }
    return -1;
}

void gip_key_label(int code, char *out, int outlen) {
    if (code < 0) { snprintf(out, outlen, "none"); return; }
    for (int i = 0; i < KEY_COUNT; i++)
        if (key_names[i].code == code) { snprintf(out, outlen, "%s", key_names[i].name); return; }
    snprintf(out, outlen, "k%d", code);
}

void gip_reset_keymap(void) { memcpy(keymap, default_keymap, sizeof keymap); }

void gip_set_key(int b, int code) { if (b >= 0 && b < GIP_BUTTON_COUNT) keymap[b] = code; }
int gip_get_key(int b) { return b >= 0 && b < GIP_BUTTON_COUNT ? keymap[b] : -1; }

int gip_load_keymap(const char *path) {
    gip_reset_keymap();
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char btn[32], key[32];
        if (sscanf(line, " %31[A-Za-z] = %31s", btn, key) != 2) continue;
        int b = -1;
        for (int i = 0; i < GIP_BUTTON_COUNT; i++)
            if (!strcasecmp(button_names[i], btn)) b = i;
        int none = !strcasecmp(key, "none");
        int k = none ? -1 : gip_lookup_key(key);
        if (b < 0) fprintf(stderr, "%s:%d: botón desconocido '%s'\n", path, lineno, btn);
        else if (k < 0 && !none) fprintf(stderr, "%s:%d: tecla desconocida '%s'\n", path, lineno, key);
        else keymap[b] = k;
    }
    fclose(f);
    return 0;
}

int gip_save_keymap(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# Mapa de botones -> teclas para 8BitDo Arcade Controller for Xbox\n"
               "# Formato: BOTON = tecla   (usa \"none\" para desactivar un botón)\n\n");
    for (int b = 0; b < GIP_BUTTON_COUNT; b++) {
        char label[32];
        gip_key_label(keymap[b], label, sizeof label);
        fprintf(f, "%-6s = %s\n", button_names[b], label);
    }
    return fclose(f) ? -1 : 0;
}

void gip_set_callbacks(gip_button_cb on_button, gip_status_cb on_status, void *ctx) {
    button_cb = on_button;
    status_cb = on_status;
    cb_ctx = ctx;
}

void gip_set_output_enabled(int enabled) { output_enabled = enabled; }

int gip_accessibility_trusted(int prompt) {
    const void *keys[] = {kAXTrustedCheckOptionPrompt};
    const void *vals[] = {prompt ? kCFBooleanTrue : kCFBooleanFalse};
    CFDictionaryRef opts = CFDictionaryCreate(NULL, keys, vals, 1, NULL, NULL);
    int trusted = AXIsProcessTrustedWithOptions(opts);
    CFRelease(opts);
    return trusted;
}

static CGEventFlags modifier_mask(int code) {
    switch (code) {
    case 55: return kCGEventFlagMaskCommand;
    case 56: case 60: return kCGEventFlagMaskShift;
    case 58: case 61: return kCGEventFlagMaskAlternate;
    case 59: case 62: return kCGEventFlagMaskControl;
    default: return 0;
    }
}

// Salida preferida: el relay (teclado virtual HID de Karabiner), visible para apps como OpenEmu
// que leen el teclado directamente del hardware. Si no está, se simulan teclas con CGEvent.
static int relay_fd = -1;

static int hid_usage(int code) {
    for (int i = 0; i < KEY_COUNT; i++)
        if (key_names[i].code == code) return key_names[i].usage;
    return -1;
}

static int relay_send(int usage, int down) {
    if (relay_fd < 0) {
        relay_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        struct sockaddr_un addr = {.sun_family = AF_UNIX};
        strlcpy(addr.sun_path, GIP_RELAY_SOCKET, sizeof addr.sun_path);
        if (connect(relay_fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
            close(relay_fd);
            relay_fd = -1;
            return -1;
        }
    }
    unsigned char msg[2] = {(unsigned char)usage, (unsigned char)down};
    if (send(relay_fd, msg, sizeof msg, 0) != sizeof msg) {
        close(relay_fd);
        relay_fd = -1;
        return -1;
    }
    return 0;
}

int gip_relay_available(void) {
    if (relay_fd >= 0) return 1;
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strlcpy(addr.sun_path, GIP_RELAY_SOCKET, sizeof addr.sun_path);
    int ok = connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0;
    close(fd);
    return ok;
}

static void post_key(int code, int down) {
    int usage = hid_usage(code);
    if (usage >= 0 && relay_send(usage, down) == 0) return;

    CGEventFlags mod = modifier_mask(code);
    if (mod) modifier_flags = down ? (modifier_flags | mod) : (modifier_flags & ~mod);
    CGEventRef ev = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)code, down);
    if (mod) CGEventSetType(ev, kCGEventFlagsChanged);
    CGEventSetFlags(ev, modifier_flags);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

// pressed: 0/1. La tecla que se suelta es la que se presionó, aunque el mapa cambie entremedio.
static int sent_key[GIP_BUTTON_COUNT];

static void set_button(int b, int pressed) {
    pressed = !!pressed;
    if (state[b] == pressed) return;
    state[b] = pressed;
    if (pressed) {
        sent_key[b] = output_enabled ? keymap[b] : -1;
        if (sent_key[b] >= 0) post_key(sent_key[b], 1);
    } else if (sent_key[b] >= 0) {
        post_key(sent_key[b], 0);
        sent_key[b] = -1;
    }
    if (button_cb) button_cb(b, pressed, cb_ctx);
}

static void release_all(void) {
    for (int b = 0; b < GIP_BUTTON_COUNT; b++) set_button(b, 0);
}

static void handle_input(const unsigned char *p, int n) {
    if (n < 18) return;
    unsigned char b0 = p[4], b1 = p[5];
    set_button(GIP_MENU, b0 & 0x04);
    set_button(GIP_VIEW, b0 & 0x08);
    set_button(GIP_A, b0 & 0x10);
    set_button(GIP_B, b0 & 0x20);
    set_button(GIP_X, b0 & 0x40);
    set_button(GIP_Y, b0 & 0x80);
    set_button(GIP_LB, b1 & 0x10);
    set_button(GIP_RB, b1 & 0x20);
    set_button(GIP_LS, b1 & 0x40);
    set_button(GIP_RS, b1 & 0x80);
    int lt = p[6] | p[7] << 8, rt = p[8] | p[9] << 8;
    set_button(GIP_LT, lt > TRIGGER_THRESHOLD);
    set_button(GIP_RT, rt > TRIGGER_THRESHOLD);
    // Las direcciones pueden llegar como D-pad o como stick izquierdo según el modo del control
    int16_t lx = (int16_t)(p[10] | p[11] << 8), ly = (int16_t)(p[12] | p[13] << 8);
    int16_t rx = (int16_t)(p[14] | p[15] << 8), ry = (int16_t)(p[16] | p[17] << 8);
    set_button(GIP_UP, (b1 & 0x01) || ly > STICK_DEADZONE);  // en GIP, Y positivo = arriba
    set_button(GIP_DOWN, (b1 & 0x02) || ly < -STICK_DEADZONE);
    set_button(GIP_LEFT, (b1 & 0x04) || lx < -STICK_DEADZONE);
    set_button(GIP_RIGHT, (b1 & 0x08) || lx > STICK_DEADZONE);
    set_button(GIP_RUP, ry > STICK_DEADZONE);
    set_button(GIP_RDOWN, ry < -STICK_DEADZONE);
    set_button(GIP_RLEFT, rx < -STICK_DEADZONE);
    set_button(GIP_RRIGHT, rx > STICK_DEADZONE);
}

static unsigned char seq = 1;

static int gip_send(libusb_device_handle *h, unsigned char *buf, int len) {
    int sent;
    return libusb_interrupt_transfer(h, EP_OUT, buf, len, &sent, 1000);
}

static void gip_power_on(libusb_device_handle *h) {
    unsigned char pkt[] = {0x05, 0x20, seq++, 0x01, 0x00};
    gip_send(h, pkt, sizeof pkt);
}

static int link_type = GIP_LINK_UNKNOWN;
static char serial[64] = "";

int gip_link_type(void) { return link_type; }
const char *gip_serial(void) { return serial; }

// El control por cable y el transmisor inalámbrico usan el mismo VID/PID y nombre. Lo que los
// distingue (observado) es el número de serie, que es la MAC: por cable termina en "0000"
// (XXXXXXXXXXXX0000) y en el transmisor aparece invertida y empieza en "0000" (0000XXXXXXXXXXXX).
static void detect_link(libusb_device_handle *h) {
    struct libusb_device_descriptor desc;
    serial[0] = 0;
    link_type = GIP_LINK_UNKNOWN;
    if (libusb_get_device_descriptor(libusb_get_device(h), &desc) || !desc.iSerialNumber) return;
    int n = libusb_get_string_descriptor_ascii(h, desc.iSerialNumber, (unsigned char *)serial, sizeof serial);
    if (n <= 0) { serial[0] = 0; return; }
    int starts = n > 4 && !strncmp(serial, "0000", 4);
    int ends = n > 4 && !strcmp(serial + n - 4, "0000");
    if (starts && !ends) link_type = GIP_LINK_WIRELESS;
    else if (ends && !starts) link_type = GIP_LINK_WIRED;
}

static libusb_device_handle *open_controller(void) {
    libusb_device_handle *h = libusb_open_device_with_vid_pid(NULL, VID, PID);
    if (!h) return NULL;
    detect_link(h);
    libusb_set_configuration(h, 1);
    if (libusb_claim_interface(h, 0)) { libusb_close(h); return NULL; }
    gip_power_on(h);
    return h;
}

static void run_session(libusb_device_handle *h) {
    unsigned char buf[64];
    while (running) {
        int n = 0;
        int r = libusb_interrupt_transfer(h, EP_IN, buf, sizeof buf, &n, 100);
        if (r == LIBUSB_ERROR_TIMEOUT) continue;
        if (r) { fprintf(stderr, "Control desconectado (%s)\n", libusb_error_name(r)); break; }
        if (n < 4) continue;
        if (buf[1] & 0x10) {  // el control pide ACK
            unsigned char ack[] = {0x01, 0x20, buf[2], 0x09, 0x00, buf[0], 0x20, buf[3], 0, 0, 0, 0, 0};
            gip_send(h, ack, sizeof ack);
        }
        switch (buf[0]) {
        case 0x02: gip_power_on(h); break;                                  // anuncio: volver a encender
        case 0x07: if (n >= 5) set_button(GIP_HOME, buf[4] & 0x01); break;  // botón Xbox/Home
        case 0x20: handle_input(buf, n); break;                             // reporte de entrada
        }
    }
    release_all();
    libusb_release_interface(h, 0);
    libusb_close(h);
}

void gip_run(void) {
    running = 1;
    libusb_init_context(NULL, NULL, 0);
    while (running) {
        libusb_device_handle *h = open_controller();
        if (!h) { usleep(500000); continue; }
        if (status_cb) status_cb(1, cb_ctx);
        run_session(h);
        if (status_cb) status_cb(0, cb_ctx);
    }
    libusb_exit(NULL);
}

void gip_stop(void) { running = 0; }
