// gip_core.h — núcleo del driver 8BitDo Arcade Controller for Xbox (GIP) -> teclado macOS.
// Lo usan tanto la herramienta de terminal (gip2key) como la app (8BitDo Keys).
#ifndef GIP_CORE_H
#define GIP_CORE_H

// Entradas lógicas del control
enum {
    GIP_A, GIP_B, GIP_X, GIP_Y, GIP_LB, GIP_RB, GIP_LT, GIP_RT, GIP_MENU, GIP_VIEW, GIP_HOME,
    GIP_LS, GIP_RS, GIP_UP, GIP_DOWN, GIP_LEFT, GIP_RIGHT, GIP_RUP, GIP_RDOWN, GIP_RLEFT,
    GIP_RRIGHT, GIP_BUTTON_COUNT
};

typedef void (*gip_button_cb)(int button, int pressed, void *ctx);
typedef void (*gip_status_cb)(int connected, void *ctx);

const char *gip_button_name(int button);

// Teclas: lista canónica (para menús) y conversión nombre <-> keycode de macOS.
// Un keycode sin nombre se escribe como "kNN".
int gip_key_count(void);
int gip_key_code_at(int i);
int gip_lookup_key(const char *name);      // -1 si no existe
void gip_key_label(int code, char *out, int outlen);

// Mapa de botones -> keycode (-1 = sin tecla)
void gip_reset_keymap(void);
void gip_set_key(int button, int code);
int gip_get_key(int button);
int gip_load_keymap(const char *path);     // 0 = ok
int gip_save_keymap(const char *path);     // 0 = ok

void gip_set_callbacks(gip_button_cb on_button, gip_status_cb on_status, void *ctx);
void gip_set_output_enabled(int enabled);  // 0 = no generar teclas (solo callbacks)
int gip_accessibility_trusted(int prompt);

// Relay al teclado virtual de Karabiner (ver relay/). Si está activo las teclas salen por ahí.
#define GIP_RELAY_SOCKET "/var/run/gip2key-relay.sock"
int gip_relay_available(void);

// Tipo de conexión del dispositivo abierto (válido desde el callback de estado con connected=1)
enum { GIP_LINK_UNKNOWN, GIP_LINK_WIRED, GIP_LINK_WIRELESS };
int gip_link_type(void);
const char *gip_serial(void);

// Bucle principal: bloquea hasta gip_stop(). Se reconecta solo si el control se desconecta.
void gip_run(void);
void gip_stop(void);

#endif
