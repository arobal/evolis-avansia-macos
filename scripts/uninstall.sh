#!/bin/bash
# Entfernt den inoffiziellen Evolis-Avansia-Treiber wieder vollstaendig.
#   sudo ./scripts/uninstall.sh
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "Bitte mit sudo ausfuehren: sudo $0"; exit 1; }

lpadmin -x Evolis_Avansia 2>/dev/null && echo "Drucker Evolis_Avansia entfernt" || true
rm -f /usr/libexec/cups/backend/avansia
rm -f "/Library/Printers/PPDs/Contents/Resources/Evolis Avansia.ppd"
[ -L /usr/local/bin/avansiactl ] && rm -f /usr/local/bin/avansiactl
rm -rf /Library/Printers/Evolis/Avansia
rmdir /Library/Printers/Evolis 2>/dev/null || true
echo "Treiber entfernt."
