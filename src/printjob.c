/*
 * printjob.c - Auftragsstrom an die Avansia senden (fuer Backend und avansiactl).
 *
 * Sicherheitsregeln:
 *  - Jede Karte wird vollstaendig gelesen und geprueft, bevor ein Byte
 *    an den Drucker geht.
 *  - Eine Karte wird nie automatisch wiederholt. Faellt etwas waehrend
 *    der Uebertragung aus, wird der Auftrag mit klarer Meldung beendet.
 *  - Abbruch (SIGTERM) wird erst zwischen zwei Karten wirksam.
 *
 * SPDX-License-Identifier: MIT
 */
#include "printjob.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "prn.h"
#include "usb.h"

static void msg(pj_ctx_t *c, const char *level, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (c->cups) {
    fprintf(stderr, "%s: ", level);
  } else if (!strcmp(level, "DEBUG") && !getenv("AVANSIA_DEBUG")) {
    va_end(ap);
    return;
  } else if (!strcmp(level, "ERROR")) {
    fputs("FEHLER: ", stderr);
  } else if (!strcmp(level, "STATE") || !strcmp(level, "ATTR")) {
    va_end(ap);
    return;
  }
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  fflush(stderr);
  va_end(ap);
}

static void progress(const char *m, void *ctx) { msg((pj_ctx_t *)ctx, "INFO", "%s", m); }

static void report_levels(pj_ctx_t *c, const ava_status_t *st) {
  msg(c, "ATTR",
      "marker-names=Farbband,Retransferfilm marker-types=ink-ribbon,transfer-unit "
      "marker-colors=#C0308A,#A0A0A0 marker-levels=%d,%d",
      st->ribbon_percent, st->film_percent);
}

static int canceled(pj_ctx_t *c) { return c->cancel && *c->cancel; }

static int open_device(pj_ctx_t *c, ava_dev_t **dev) {
  char err[256];
  int announced = 0;
  for (;;) {
    int r = ava_open(dev, c->serial, err, sizeof(err));
    if (r == 0) {
      if (announced) msg(c, "STATE", "-connecting-to-device");
      return 0;
    }
    if (!c->wait_for_device) {
      msg(c, "ERROR", "%s", err);
      return -1;
    }
    if (!announced) {
      msg(c, "STATE", "+connecting-to-device");
      msg(c, "INFO", "Warte auf Avansia: %s", err);
      announced = 1;
    }
    for (int i = 0; i < 50; i++) {
      if (canceled(c)) return -1;
      usleep(100000);
    }
  }
}

static int wait_ready(pj_ctx_t *c, ava_dev_t *dev) {
  char err[256], desc[256], last[256] = "";
  int waited = 0;
  for (;;) {
    ava_status_t st;
    if (ava_get_status(dev, &st, err, sizeof(err))) {
      msg(c, "ERROR", "Status nicht lesbar: %s", err);
      return -1;
    }
    report_levels(c, &st);
    ava_status_describe(&st, desc, sizeof(desc));
    if (st.ready && !st.printing && !st.error) {
      if (waited) msg(c, "STATE", "-other");
      msg(c, "DEBUG", "Drucker bereit: %s", desc);
      return 0;
    }
    if (strcmp(desc, last)) {
      msg(c, "INFO", "Drucker nicht bereit - bitte Display pruefen (%s)", desc);
      msg(c, "STATE", "+other");
      snprintf(last, sizeof(last), "%s", desc);
    }
    if (!c->wait_for_device && waited > 60) {
      msg(c, "ERROR", "Drucker wurde nicht bereit");
      return -1;
    }
    for (int i = 0; i < 20; i++) {
      if (canceled(c)) return -2;
      usleep(100000);
    }
    waited += 2;
  }
}

pj_result_t pj_run(FILE *in, pj_ctx_t *c) {
  ava_dev_t *dev = NULL;
  pj_result_t result = PJ_OK;
  char err[256];
  c->cards_done = 0;

  for (;;) {
    prn_doc_t doc;
    int r = prn_read_doc(in, &doc, err, sizeof(err));
    if (r == 0) break;
    if (r < 0 || prn_validate(&doc, err, sizeof(err))) {
      msg(c, "ERROR", "Ungueltige Auftragsdaten fuer Karte %d: %s - es wurde nichts an den Drucker gesendet",
          c->cards_done + 1, err);
      if (r >= 0) prn_free(&doc);
      result = PJ_INVALID_JOB;
      break;
    }
    msg(c, "DEBUG", "Karte %d: %s, %zu Rahmen, %zu Bytes", c->cards_done + 1,
        doc.duplex ? "beidseitig" : "einseitig", doc.frames, doc.bytes);

    if (c->dry_run) {
      c->cards_done++;
      prn_free(&doc);
      continue;
    }
    if (canceled(c)) {
      prn_free(&doc);
      result = PJ_CANCELED;
      break;
    }
    if (!dev && open_device(c, &dev)) {
      prn_free(&doc);
      result = canceled(c) ? PJ_CANCELED : PJ_FAILED_BEFORE_PRINT;
      break;
    }
    int w = wait_ready(c, dev);
    if (w) {
      prn_free(&doc);
      result = w == -2 ? PJ_CANCELED : PJ_FAILED_BEFORE_PRINT;
      break;
    }
    msg(c, "INFO", "Drucke Karte %d", c->cards_done + 1);
    int p = ava_print_doc(dev, &doc, progress, c, err, sizeof(err));
    prn_free(&doc);
    if (p == -1) {
      msg(c, "ERROR", "Karte %d nicht gestartet: %s", c->cards_done + 1, err);
      result = PJ_FAILED_BEFORE_PRINT;
      break;
    }
    if (p == -2) {
      msg(c, "ERROR",
          "Karte %d: %s. Die Karte wurde moeglicherweise teilweise gedruckt. Bitte Drucker und Karte "
          "pruefen; der Auftrag wird NICHT automatisch wiederholt.",
          c->cards_done + 1, err);
      result = PJ_FAILED_UNCERTAIN;
      break;
    }
    c->cards_done++;
    msg(c, "INFO", "Karte %d fertig", c->cards_done);
  }

  if (dev) {
    ava_status_t st;
    if (!ava_get_status(dev, &st, err, sizeof(err))) report_levels(c, &st);
    ava_close(dev);
  }
  if (result == PJ_OK && c->cards_done == 0) {
    msg(c, "ERROR", "Der Auftrag enthielt keine Karte");
    result = PJ_INVALID_JOB;
  }
  return result;
}
