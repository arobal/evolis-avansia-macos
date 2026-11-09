#!/bin/bash
# Offline-Tests: kein Drucker und kein Evolis-SDK noetig.
#   make test
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=tests/out
rm -rf "$OUT"; mkdir -p "$OUT"
fail=0

echo "== Unit-Tests"
build/test_prn "$OUT" || fail=1
build/test_image "$OUT" || fail=1

echo "== avansiactl validate (synthetischer Auftrag)"
if build/avansiactl validate "$OUT/synthetic-2cards.prn"; then echo "  ok"; else echo "  FEHLER"; fail=1; fi
if build/avansiactl validate "$OUT/bad-cmd.prn" 2>/dev/null; then echo "  FEHLER: ungueltiger Auftrag akzeptiert"; fail=1; else echo "  ok    ungueltiger Auftrag abgelehnt"; fi

echo "== PPD"
cupstestppd -W all build/EvolisAvansia.ppd 2>&1 | grep -E "PASS|FAIL" | grep -v "Missing cupsFilter2" || true

echo "== Testkarte -> macOS-Filterkette -> rastertoavansia (ohne SDK)"
swift tools/testcard.swift "$OUT/testkarte.pdf" >/dev/null
for mode in simplex duplex; do
  opts="PageSize=CR80"
  [ "$mode" = duplex ] && opts="$opts,Duplex=DuplexNoTumble"
  cupsfilter -p build/EvolisAvansia.ppd -m application/vnd.cups-raster -o "${opts//,/ -o }" \
    "$OUT/testkarte.pdf" > "$OUT/testkarte-$mode.ras" 2> "$OUT/cupsfilter-$mode.log" || { echo "  FEHLER cupsfilter ($mode)"; cat "$OUT/cupsfilter-$mode.log"; fail=1; continue; }
  mkdir -p "$OUT/$mode"
  AVANSIA_NO_SDK=1 AVANSIA_DUMP_DIR="$OUT/$mode" PPD=build/EvolisAvansia.ppd \
    build/rastertoavansia 1 test test 1 "$opts" "$OUT/testkarte-$mode.ras" > /dev/null 2> "$OUT/filter-$mode.log" \
    || { echo "  FEHLER Filter ($mode)"; cat "$OUT/filter-$mode.log"; fail=1; continue; }
  n=$(ls "$OUT/$mode"/*.bmp 2>/dev/null | wc -l | tr -d ' ')
  grep -E "^(DEBUG: Seite|DEBUG: Karte|INFO: [0-9])" "$OUT/filter-$mode.log" | sed 's/^/    /'
  echo "  $mode: $n Kartenbild(er) in $OUT/$mode"
done
[ -f "$OUT/simplex/karte-001-vorne.bmp" ] && [ -f "$OUT/simplex/karte-002-vorne.bmp" ] || { echo "  FEHLER: einseitig sollten 2 Karten entstehen"; fail=1; }
[ -f "$OUT/duplex/karte-001-vorne.bmp" ] && [ -f "$OUT/duplex/karte-001-hinten.bmp" ] || { echo "  FEHLER: beidseitig sollte 1 Karte mit Rueckseite entstehen"; fail=1; }

echo
[ $fail = 0 ] && echo "ALLE OFFLINE-TESTS BESTANDEN" || { echo "TESTS FEHLGESCHLAGEN"; exit 1; }
