/*
 * avansia - CUPS-Backend fuer die Evolis Avansia per USB.
 *
 *   avansia                                    Geraete auflisten (Discovery)
 *   avansia job user title copies options [datei]   Auftrag drucken
 *
 * Geraete-URI:  avansia://usb            (erste gefundene Avansia)
 *               avansia://usb?serial=XYZ (bestimmter Drucker)
 *
 * Erwartet als Eingabe die Ausgabe von rastertoavansia (application/vnd.evolis-avansia).
 *
 * SPDX-License-Identifier: MIT
 */
#include <cups/backend.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "printjob.h"
#include "usb.h"

static volatile sig_atomic_t g_cancel = 0;
static void on_term(int sig) {
  (void)sig;
  g_cancel = 1;
}

static void list_cb(uint16_t vid, uint16_t pid, const char *serial, void *ctx) {
  (void)ctx;
  (void)vid;
  if (pid != AVA_PID) {
    fprintf(stderr, "DEBUG: Unbekanntes NiSCA/Evolis-Geraet %04x:%04x ignoriert\n", vid, pid);
    return;
  }
  if (serial && *serial)
    printf("direct avansia://usb?serial=%s \"Evolis Avansia\" \"Evolis Avansia (USB, %s)\" "
           "\"MFG:Evolis;MDL:Avansia;CMD:AVANSIA;\" \"\"\n",
           serial, serial);
  else
    printf("direct avansia://usb \"Evolis Avansia\" \"Evolis Avansia (USB)\" "
           "\"MFG:Evolis;MDL:Avansia;CMD:AVANSIA;\" \"\"\n");
  fflush(stdout);
}

static void serial_from_uri(const char *uri, char *out, size_t len) {
  out[0] = 0;
  if (!uri) return;
  const char *p = strstr(uri, "serial=");
  if (!p) return;
  p += 7;
  size_t n = strcspn(p, "&");
  if (n >= len) n = len - 1;
  memcpy(out, p, n);
  out[n] = 0;
}

int main(int argc, char *argv[]) {
  setbuf(stderr, NULL);
  if (argc == 1) {
    ava_list(list_cb, NULL);
    return CUPS_BACKEND_OK;
  }
  if (argc < 6 || argc > 7) {
    fprintf(stderr, "Aufruf: %s job user title copies options [datei]\n", argv[0]);
    return CUPS_BACKEND_FAILED;
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_term;
  sigaction(SIGTERM, &sa, NULL);
  signal(SIGPIPE, SIG_IGN);

  char serial[64];
  serial_from_uri(getenv("DEVICE_URI"), serial, sizeof(serial));

  FILE *in = stdin;
  if (argc == 7 && !(in = fopen(argv[6], "rb"))) {
    fprintf(stderr, "ERROR: Datei %s nicht lesbar\n", argv[6]);
    return CUPS_BACKEND_FAILED;
  }

  pj_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.cups = 1;
  ctx.serial = serial[0] ? serial : NULL;
  ctx.wait_for_device = 1;
  ctx.cancel = &g_cancel;

  pj_result_t r = pj_run(in, &ctx);
  if (in != stdin) fclose(in);

  switch (r) {
    case PJ_OK:
      fprintf(stderr, "INFO: %d Karte(n) gedruckt\n", ctx.cards_done);
      return CUPS_BACKEND_OK;
    case PJ_CANCELED:
      fprintf(stderr, "INFO: Abgebrochen nach %d Karte(n)\n", ctx.cards_done);
      return CUPS_BACKEND_OK;
    case PJ_FAILED_BEFORE_PRINT:
      /* Nichts von der aktuellen Karte gedruckt. Wenn noch keine Karte
       * fertig ist, darf CUPS spaeter wiederholen; sonst abbrechen, damit
       * fertige Karten nicht doppelt gedruckt werden. */
      return ctx.cards_done == 0 ? CUPS_BACKEND_RETRY_CURRENT : CUPS_BACKEND_CANCEL;
    case PJ_FAILED_UNCERTAIN:
    case PJ_INVALID_JOB:
    default:
      return CUPS_BACKEND_CANCEL;
  }
}
