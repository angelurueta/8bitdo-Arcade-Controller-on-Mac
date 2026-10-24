// gip2key-relay.cpp — servicio (root) que recibe teclas de gip2key / 8BitDo Keys por un socket
// UNIX y las escribe en el teclado virtual HID de Karabiner. Así las apps que leen el teclado
// directamente del hardware (OpenEmu) también las ven.
//
// Protocolo: datagramas de 2 bytes {usage HID (página 0x07), 1=down / 0=up}. {0,0} suelta todo.
// Uso: gip2key-relay -u <uid dueño del socket>
#include "gip_core.h"

#include <pqrs/karabiner/driverkit/virtual_hid_device_driver.hpp>
#include <pqrs/karabiner/driverkit/virtual_hid_device_service.hpp>

#include <atomic>
#include <csignal>
#include <iostream>
#include <mutex>
#include <set>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace driver = pqrs::karabiner::driverkit::virtual_hid_device_driver;
namespace service = pqrs::karabiner::driverkit::virtual_hid_device_service;

static const char *socket_path = GIP_RELAY_SOCKET;
static std::atomic<bool> exit_flag(false);

int main(int argc, char **argv) {
    uid_t owner = 0;
    int opt;
    while ((opt = getopt(argc, argv, "u:")) != -1) {
        if (opt == 'u') owner = static_cast<uid_t>(atoi(optarg));
    }
    std::signal(SIGINT, [](int) { exit_flag = true; });
    std::signal(SIGTERM, [](int) { exit_flag = true; });

    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strlcpy(addr.sun_path, socket_path, sizeof addr.sun_path);
    unlink(socket_path);
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
        perror("bind");
        return 1;
    }
    chown(socket_path, owner, 0);
    chmod(socket_path, 0600);
    timeval tv{0, 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    pqrs::dispatcher::extra::initialize_shared_dispatcher();
    std::mutex mutex;
    std::atomic<bool> ready(false);
    auto client = std::make_unique<service::client>();

    client->connected.connect([&client] {
        std::cout << "conectado al servicio de Karabiner" << std::endl;
        service::virtual_hid_keyboard_parameters parameters;
        parameters.set_country_code(pqrs::hid::country_code::us);
        client->async_virtual_hid_keyboard_initialize(parameters);
    });
    client->connect_failed.connect([](auto &&error_code) {
        std::cout << "no se pudo conectar a Karabiner: " << error_code << std::endl;
    });
    client->closed.connect([&ready] {
        std::cout << "conexión con Karabiner cerrada" << std::endl;
        ready = false;
    });
    client->driver_version_mismatched.connect([](auto &&mismatched) {
        if (mismatched) std::cout << "aviso: la versión del driver de Karabiner no coincide" << std::endl;
    });
    client->virtual_hid_keyboard_ready.connect([&ready](auto &&is_ready) {
        if (ready != is_ready) std::cout << "teclado virtual " << (is_ready ? "listo" : "no disponible") << std::endl;
        ready = is_ready;
    });
    client->async_start();

    std::set<uint8_t> modifiers;  // usages 0xE0-0xE7
    std::set<uint8_t> keys;
    auto post = [&] {
        std::lock_guard<std::mutex> lock(mutex);
        if (!ready) return;
        driver::hid_report::keyboard_input report;
        for (auto m : modifiers) report.modifiers.insert(static_cast<driver::hid_report::modifier>(1 << (m - 0xE0)));
        for (auto k : keys) report.keys.insert(k);
        client->async_post_report(report);
    };

    std::cout << "escuchando en " << socket_path << std::endl;
    while (!exit_flag) {
        uint8_t msg[2];
        ssize_t n = recv(fd, msg, sizeof msg, 0);
        if (n != 2) continue;
        uint8_t usage = msg[0];
        bool down = msg[1] != 0;
        if (usage == 0) {
            modifiers.clear();
            keys.clear();
        } else {
            auto &target = (usage >= 0xE0 && usage <= 0xE7) ? modifiers : keys;
            if (down) target.insert(usage);
            else target.erase(usage);
        }
        post();
    }

    modifiers.clear();
    keys.clear();
    post();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        std::lock_guard<std::mutex> lock(mutex);
        client = nullptr;
    }
    pqrs::dispatcher::extra::terminate_shared_dispatcher();
    close(fd);
    unlink(socket_path);
    return 0;
}
