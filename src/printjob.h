/*
 * printjob.h - Auftragsstrom (eine oder mehrere Karten) an die Avansia senden.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AVANSIA_PRINTJOB_H
#define AVANSIA_PRINTJOB_H

#include <signal.h>
#include <stdio.h>

typedef enum {
  PJ_OK = 0,
  PJ_FAILED_BEFORE_PRINT = 1, /* nichts gedruckt -> Wiederholung moeglich */
  PJ_FAILED_UNCERTAIN = 2,    /* Karte evtl. teilweise gedruckt -> pruefen! */
  PJ_INVALID_JOB = 3,         /* Auftragsdaten ungueltig, nichts gesendet */
  PJ_CANCELED = 4
} pj_result_t;

typedef struct {
  int cups;                     /* 1 = Meldungen im CUPS-Format */
  int dry_run;                  /* 1 = nur pruefen, nichts senden */
  const char *serial;           /* optional */
  int wait_for_device;          /* auf Drucker warten statt abbrechen */
  volatile sig_atomic_t *cancel;
  int cards_done;               /* Ausgabe */
} pj_ctx_t;

pj_result_t pj_run(FILE *in, pj_ctx_t *ctx);

#endif
