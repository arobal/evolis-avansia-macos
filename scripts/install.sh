#!/bin/bash
# Installiert den inoffiziellen Evolis-Avansia-Treiber.
#
#   make                              # vorher als normaler Benutzer bauen
#   sudo ./scripts/install.sh [--sdk-wheel DATEI.whl] [--no-queue]
#
# Das Evolis-SDK (libevolis.dylib) wird nicht mitgeliefert, sondern aus dem
# offiziellen PyPI-Paket "evolis-sdk" 9.4.1 geladen und per SHA-256 geprueft.
set -euo pipefail

SDK_URL="https://files.pythonhosted.org/packages/19/02/b4eca9226cdd1ca1b89f0d9c674e0856ab5d411f5ecf251f4c9c63d79bb0/evolis_sdk-9.4.1-py3-none-any.whl"
SDK_SHA256="9228bac974dd19609bda2e923e0a9b12ff8fdfed06ea45f34c0eee8ca0999f0e"

PREFIX="/Library/Printers/Evolis/Avansia"
BACKEND_DIR="/usr/libexec/cups/backend"
PPD_DIR="/Library/Printers/PPDs/Contents/Resources"
QUEUE="Evolis_Avansia"

cd "$(dirname "$0")/.."
WHEEL=""
MAKE_QUEUE=1
while [ $# -gt 0 ]; do
  case "$1" in
    --sdk-wheel) WHEEL="$2"; shift 2 ;;
    --no-queue) MAKE_QUEUE=0; shift ;;
    *) echo "Unbekannte Option: $1"; exit 2 ;;
  esac
done

[ "$(id -u)" = 0 ] || { echo "Bitte mit sudo ausfuehren: sudo $0"; exit 1; }
for f in build/rastertoavansia build/avansia build/avansiactl build/EvolisAvansia.ppd; do
  [ -f "$f" ] || { echo "$f fehlt - bitte zuerst 'make' (ohne sudo) ausfuehren."; exit 1; }
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "==> Evolis SDK holen"
if [ -z "$WHEEL" ]; then
  WHEEL="$TMP/evolis_sdk-9.4.1-py3-none-any.whl"
  curl -fsSL -o "$WHEEL" "$SDK_URL"
fi
ACTUAL=$(shasum -a 256 "$WHEEL" | awk '{print $1}')
if [ "$ACTUAL" != "$SDK_SHA256" ]; then
  echo "SHA-256 des SDK-Pakets stimmt nicht (erwartet $SDK_SHA256, erhalten $ACTUAL). Abbruch."
  exit 1
fi
unzip -p "$WHEEL" evolis/lib/macos/libevolis.dylib > "$TMP/libevolis.dylib"
file "$TMP/libevolis.dylib" | grep -q "Mach-O" || { echo "libevolis.dylib im Paket nicht gefunden"; exit 1; }

echo "==> Dateien installieren"
install -d -o root -g wheel -m 755 "$PREFIX" "$PREFIX/Filter" "$PREFIX/lib" "$PREFIX/bin"
install -o root -g wheel -m 755 build/rastertoavansia "$PREFIX/Filter/rastertoavansia"
install -o root -g wheel -m 755 "$TMP/libevolis.dylib" "$PREFIX/lib/libevolis.dylib"
install -o root -g wheel -m 755 build/avansiactl "$PREFIX/bin/avansiactl"
# 0700 + root: CUPS startet das Backend als root (USB-Zugriff ohne Rechteprobleme)
install -o root -g wheel -m 700 build/avansia "$BACKEND_DIR/avansia"
install -d -o root -g admin -m 775 "$PPD_DIR"
install -o root -g admin -m 644 build/EvolisAvansia.ppd "$PPD_DIR/Evolis Avansia.ppd"
install -d -m 755 /usr/local/bin && ln -sf "$PREFIX/bin/avansiactl" /usr/local/bin/avansiactl
xattr -dr com.apple.quarantine "$PREFIX" 2>/dev/null || true

if [ "$MAKE_QUEUE" = 1 ]; then
  echo "==> Drucker '$QUEUE' anlegen"
  lpadmin -p "$QUEUE" -E -v "avansia://usb" -P "$PPD_DIR/Evolis Avansia.ppd" \
    -D "Evolis Avansia" -L "USB" -o printer-is-shared=false
  cupsenable "$QUEUE"
  cupsaccept "$QUEUE"
fi

echo
echo "Fertig. Pruefen mit:"
echo "  avansiactl status                 (Drucker per USB angeschlossen und eingeschaltet)"
echo "  lpstat -p $QUEUE"
