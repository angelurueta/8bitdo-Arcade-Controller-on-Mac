// gip2key.c — versión de terminal del driver.
// Uso: ./gip2key [-v] [-c keymap.conf]
// Por defecto usa el mismo mapa que la app: ~/Library/Application Support/8BitDo Keys/keymap.conf
#include "gip_core.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void on_button(int b, int pressed, void *ctx) {
    (void)ctx;
    printf("%-6s %s\n", gip_button_name(b), pressed ? "down" : "up");
}

static void on_status(int connected, void *ctx) {
    (void)ctx;
    if (!connected) { printf("Esperando el control...\n"); return; }
    static const char *links[] = {"tipo desconocido", "por cable", "por transmisor inalámbrico"};
    printf("Control conectado %s (serie %s). Ctrl-C para salir.\n", links[gip_link_type()], gip_serial());
}

static void on_signal(int sig) { (void)sig; gip_stop(); }

int main(int argc, char **argv) {
    char default_config[1024];
    snprintf(default_config, sizeof default_config,
             "%s/Library/Application Support/8BitDo Keys/keymap.conf", getenv("HOME"));
    const char *config = default_config;
    int verbose = 0, opt;
    while ((opt = getopt(argc, argv, "vc:")) != -1) {
        if (opt == 'v') verbose = 1;
        else if (opt == 'c') config = optarg;
        else { fprintf(stderr, "Uso: %s [-v] [-c keymap.conf]\n", argv[0]); return 1; }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (gip_load_keymap(config)) {
        if (config != default_config) { fprintf(stderr, "No se pudo abrir %s\n", config); return 1; }
        printf("Sin %s: usando el mapa predeterminado.\n", config);
    }

    if (gip_relay_available())
        printf("Relay activo: las teclas salen por el teclado virtual de Karabiner.\n");
    else if (!gip_accessibility_trusted(1))
        fprintf(stderr, "Aviso: sin permiso de Accesibilidad las teclas no llegarán a las apps.\n"
                        "Actívalo en Ajustes del Sistema > Privacidad y seguridad > Accesibilidad.\n");

    gip_set_callbacks(verbose ? on_button : NULL, on_status, NULL);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    printf("Esperando el control...\n");
    gip_run();
    return 0;
}
