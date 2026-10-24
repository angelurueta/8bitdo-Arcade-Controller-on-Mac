BUILD      = build
APP        = $(BUILD)/8BitDo Keys.app
CORE       = src/core

LIBUSB     = $(shell pkg-config --variable=libdir libusb-1.0)/libusb-1.0.a
CFLAGS     = -O2 -Wall -I$(CORE) $(shell pkg-config --cflags libusb-1.0)
FRAMEWORKS = -framework ApplicationServices -framework IOKit -framework CoreFoundation -framework Security

# Biblioteca cliente del teclado virtual de Karabiner. Debe coincidir con la versión de
# Karabiner-Elements instalada (ver README).
KARABINER_TAG = v8.7.0
KARABINER     = vendor/Karabiner-DriverKit-VirtualHIDDevice

all: $(BUILD)/gip2key $(BUILD)/probe app $(BUILD)/gip2key-relay

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/gip_core.o: $(CORE)/gip_core.c $(CORE)/gip_core.h | $(BUILD)
	clang $(CFLAGS) -c $< -o $@

$(BUILD)/gip2key: src/cli/gip2key.c $(BUILD)/gip_core.o
	clang $(CFLAGS) $^ -o $@ $(LIBUSB) $(FRAMEWORKS)

$(BUILD)/probe: src/tools/probe.c | $(BUILD)
	clang $(CFLAGS) $< -o $@ $(LIBUSB) $(FRAMEWORKS)

app: $(BUILD)/gip_core.o src/app/main.swift src/app/Info.plist
	mkdir -p "$(APP)/Contents/MacOS"
	cp src/app/Info.plist "$(APP)/Contents/Info.plist"
	swiftc -O -swift-version 5 -import-objc-header $(CORE)/gip_core.h \
		src/app/main.swift $(BUILD)/gip_core.o \
		$(LIBUSB) $(FRAMEWORKS) -o "$(APP)/Contents/MacOS/8BitDoKeys"
	codesign --force --sign - --identifier local.8bitdo.keys "$(APP)"

$(KARABINER):
	git clone --depth 1 --branch $(KARABINER_TAG) \
		https://github.com/pqrs-org/Karabiner-DriverKit-VirtualHIDDevice.git $(KARABINER)

$(BUILD)/gip2key-relay: src/relay/gip2key-relay.cpp $(CORE)/gip_core.h | $(KARABINER) $(BUILD)
	clang++ -std=c++23 -O2 -Wall -I$(CORE) -isystem $(KARABINER)/vendor/vendor/include \
		-isystem $(KARABINER)/include $< -o $@

install: app
	rm -rf "/Applications/8BitDo Keys.app"
	cp -R "$(APP)" /Applications/
	@echo "Instalado en /Applications/8BitDo Keys.app"

install-relay: $(BUILD)/gip2key-relay
	sudo scripts/install-relay.sh

uninstall-relay:
	sudo scripts/install-relay.sh --uninstall

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf vendor

.PHONY: all app install install-relay uninstall-relay clean distclean
