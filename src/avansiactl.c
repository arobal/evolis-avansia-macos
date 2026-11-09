/*
 * avansiactl - Hilfswerkzeug fuer die Evolis Avansia.
 *
 *   avansiactl list                         angeschlossene Geraete anzeigen
 *   avansiactl status                       Druckerstatus lesen (nur lesend)
 *   avansiactl validate AUFTRAG.prn         Auftragsdatei pruefen (offline)
 *   avansiactl make-prn VORNE.bmp AUSGABE.prn [--back HINTEN.bmp]
 *              [--ribbon YMCK|YMCKK] [--black ALLBLACKPOINT|NOBLACKPOINT]
 *              [--rotate-back]              Auftrag mit dem Evolis-SDK erzeugen
 *   avansiactl print AUFTRAG.prn --yes      Auftrag direkt drucken (ohne CUPS)
 *
 * SPDX-License-Identifier: MIT
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "printjob.h"
#include "sdk.h"
#include "usb.h"

static volatile sig_atomic_t g_cancel = 0;
static void on_int(int sig) {
  (void)sig;
  g_cancel = 1;
}

static int usage(void) {
  fprintf(stderr,
          "Aufruf:\n"
          "  avansiactl list\n"
          "  avansiactl status\n"
          "  avansiactl validate AUFTRAG.prn\n"
          "  avansiactl make-prn VORNE.bmp AUSGABE.prn [--back HINTEN.bmp] [--ribbon YMCK|YMCKK]\n"
          "                      [--black ALLBLACKPOINT|NOBLACKPOINT] [--rotate-back]\n"
          "  avansiactl print AUFTRAG.prn --yes\n");
  return 2;
}

static void list_cb(uint16_t vid, uint16_t pid, const char *serial, void *ctx) {
  (void)ctx;
  printf("%04x:%04x  Seriennr. %s%s\n", vid, pid, serial && *serial ? serial : "?",
         pid == AVA_PID ? "  (Evolis Avansia)" : "  (unbekanntes Modell)");
}

static int cmd_status(void) {
  ava_dev_t *dev;
  char err[256];
  if (ava_open(&dev, NULL, err, sizeof(err))) {
    fprintf(stderr, "FEHLER: %s\n", err);
    return 1;
  }
  ava_status_t st;
  int rc = ava_get_status(dev, &st, err, sizeof(err));
  ava_close(dev);
  if (rc) {
    fprintf(stderr, "FEHLER: %s\n", err);
    return 1;
  }
  printf("Modell:          Evolis Avansia\n");
  printf("Seriennummer:    %s\n", st.serial);
  printf("Firmware:        %s (Basis %s)\n", st.firmware, st.base_revision);
  printf("Zustand:         %s%s%s%s\n", st.error ? "FEHLER" : st.ready ? "bereit" : "nicht bereit",
         st.printing ? ", druckt" : "", st.mech_busy ? ", Mechanik belegt" : "",
         st.if_busy ? ", Schnittstelle belegt" : "");
  if (st.ribbon_percent >= 0)
    printf("Farbband:        %d %%\n", st.ribbon_percent);
  else
    printf("Farbband:        unbekannt\n");
  if (st.film_percent >= 0)
    printf("Retransferfilm:  %d %%\n", st.film_percent);
  else
    printf("Retransferfilm:  unbekannt\n");
  printf("Farbband-Code:   %d / Panel %d\n", st.ribbon_type_code, st.ribbon_panel_code);
  printf("Kartenposition:  %d, Druckerstatus %d, Standby %d, Flags 0x%02x\n", st.card_position,
         st.printer_status, st.standby, st.flags);
  printf("Rohstatus:      ");
  for (int i = 0; i < 60; i++) printf(" %02x", st.raw_status[i]);
  printf("\n");
  return 0;
}

static int cmd_stream(const char *path, int dry) {
  FILE *in = fopen(path, "rb");
  if (!in) {
    fprintf(stderr, "FEHLER: %s nicht lesbar\n", path);
    return 1;
  }
  pj_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.dry_run = dry;
  ctx.cancel = &g_cancel;
  pj_result_t r = pj_run(in, &ctx);
  fclose(in);
  if (dry && r == PJ_OK) printf("Gueltig: %d Karte(n)\n", ctx.cards_done);
  if (!dry && r == PJ_OK) printf("%d Karte(n) gedruckt\n", ctx.cards_done);
  return r == PJ_OK ? 0 : 1;
}

static int cmd_make(int argc, char **argv) {
  const char *front = NULL, *out = NULL, *back = NULL, *ribbon = "YMCK", *black = "ALLBLACKPOINT";
  int rot = 0;
  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "--back") && i + 1 < argc)
      back = argv[++i];
    else if (!strcmp(argv[i], "--ribbon") && i + 1 < argc)
      ribbon = argv[++i];
    else if (!strcmp(argv[i], "--black") && i + 1 < argc)
      black = argv[++i];
    else if (!strcmp(argv[i], "--rotate-back"))
      rot = 1;
    else if (!front)
      front = argv[i];
    else if (!out)
      out = argv[i];
    else
      return usage();
  }
  if (!front || !out) return usage();
  char err[512];
  if (ava_sdk_load(NULL, err, sizeof(err))) {
    fprintf(stderr, "FEHLER: %s\n", err);
    return 1;
  }
  ava_sdk_opts_t o = {ribbon, black, black, rot};
  if (ava_sdk_make_prn(front, back, &o, out, err, sizeof(err))) {
    fprintf(stderr, "FEHLER: %s\n", err);
    return 1;
  }
  printf("Auftrag erzeugt: %s (Evolis SDK %s)\n", out, ava_sdk_version());
  return cmd_stream(out, 1);
}

int main(int argc, char **argv) {
  if (argc < 2) return usage();
  signal(SIGINT, on_int);
  signal(SIGTERM, on_int);
  const char *cmd = argv[1];
  if (!strcmp(cmd, "list")) {
    if (!ava_list(list_cb, NULL)) printf("Keine Avansia gefunden.\n");
    return 0;
  }
  if (!strcmp(cmd, "status")) return cmd_status();
  if (!strcmp(cmd, "validate") && argc == 3) return cmd_stream(argv[2], 1);
  if (!strcmp(cmd, "make-prn")) return cmd_make(argc - 2, argv + 2);
  if (!strcmp(cmd, "print")) {
    if (argc != 4 || strcmp(argv[3], "--yes")) {
      fprintf(stderr, "Direktdruck verbraucht Material. Zum Bestaetigen: avansiactl print DATEI --yes\n");
      return 2;
    }
    return cmd_stream(argv[2], 0);
  }
  return usage();
}
