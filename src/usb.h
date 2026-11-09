/*
 * usb.h - USB-Transport und Statusabfrage fuer die Evolis Avansia.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AVANSIA_USB_H
#define AVANSIA_USB_H

#include <stddef.h>
#include <stdint.h>

#include "prn.h"

#define AVA_VID 0x09C2     /* NiSCA Corporation (Hersteller der Avansia-Mechanik) */
#define AVA_PID 0x0022     /* Evolis Avansia */

typedef struct ava_dev ava_dev_t;

typedef struct {
  uint8_t flags;           /* Byte 0 der T-Antwort */
  int ready;               /* bereit zum Drucken */
  int printing;
  int error;
  int mech_busy;
  int if_busy;
  int card_position;
  int printer_status;
  int standby;             /* Byte 41: 1/2 = Karte manuell einlegen/entnehmen */
  int ribbon_percent;      /* -1 = unbekannt */
  int film_percent;        /* -1 = unbekannt */
  int ribbon_type_code;
  int ribbon_panel_code;
  char serial[16];
  char firmware[8];
  char base_revision[8];
  uint8_t raw_status[60];
} ava_status_t;

typedef void (*ava_list_cb)(uint16_t vid, uint16_t pid, const char *serial, void *ctx);

/* Listet alle USB-Geraete mit Hersteller-ID 0x09C2. Rueckgabe: Anzahl. */
int ava_list(ava_list_cb cb, void *ctx);

/* Oeffnet die Avansia (optional mit Seriennummer) und belegt das
 * USB-Interface exklusiv. Rueckgabe: 0 = ok, -1 = nicht gefunden,
 * -2 = belegt/keine Berechtigung, -3 = sonstiger Fehler. */
int ava_open(ava_dev_t **dev, const char *serial, char *err, size_t errlen);
void ava_close(ava_dev_t *dev);

int ava_get_status(ava_dev_t *dev, ava_status_t *st, char *err, size_t errlen);

/* Sendet eine Karte (validiertes Dokument). Rueckgabe:
 *  0 = Drucker meldet Karte fertig
 * -1 = Fehler, BEVOR Druckdaten gesendet wurden (Wiederholung unkritisch)
 * -2 = Fehler NACHDEM Druckdaten gesendet wurden (Ergebnis ungewiss!) */
typedef void (*ava_progress_cb)(const char *msg, void *ctx);
int ava_print_doc(ava_dev_t *dev, const prn_doc_t *doc, ava_progress_cb cb, void *cbctx, char *err,
                  size_t errlen);

void ava_status_describe(const ava_status_t *st, char *buf, size_t len);

#endif
