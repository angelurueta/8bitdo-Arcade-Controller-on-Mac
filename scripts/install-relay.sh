#!/bin/bash
# Instala (o desinstala con --uninstall) el servicio gip2key-relay como LaunchDaemon.
# Uso: sudo ./install-relay.sh [--uninstall]
set -euo pipefail

LABEL=local.8bitdo.gip2key-relay
BIN=/usr/local/libexec/gip2key-relay
PLIST=/Library/LaunchDaemons/$LABEL.plist
LOG=/var/log/gip2key-relay.log

if [[ $EUID -ne 0 ]]; then
    echo "Ejecuta con sudo: sudo $0 $*" >&2
    exit 1
fi

launchctl bootout system/$LABEL 2>/dev/null || true

if [[ "${1:-}" == "--uninstall" ]]; then
    rm -f "$PLIST" "$BIN"
    echo "gip2key-relay desinstalado."
    exit 0
fi

OWNER_UID=${SUDO_UID:?Ejecuta con sudo desde tu usuario}
cd "$(dirname "$0")/.."
if [[ ! -x build/gip2key-relay ]]; then
    echo "No existe build/gip2key-relay. Compila primero con: make" >&2
    exit 1
fi
install -d /usr/local/libexec
install -m 755 build/gip2key-relay "$BIN"

cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$BIN</string>
        <string>-u</string>
        <string>$OWNER_UID</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>$LOG</string>
    <key>StandardErrorPath</key>
    <string>$LOG</string>
</dict>
</plist>
EOF

launchctl bootstrap system "$PLIST"
echo "gip2key-relay instalado. Log: $LOG"
sleep 2
tail -5 "$LOG" || true
