/*
 * prn.h - Parser und Validator fuer Avansia-Druckauftraege (".prn"),
 *         wie sie das Evolis-SDK mit evolis_print_to_file() erzeugt.
 *
 * Dateiformat (eine Zeile pro Datensatz):
 *   D,<CHECKPOINT>\n             Ablaufmarke (HEADER_BEGIN, CARD_BEGIN, ...)
 *   B,<laenge>,<bytes>\n         Binaerblock = ein Druckerrahmen
 *
 * Druckerrahmen: 0x1C 'P' <laenge 32 bit big-endian> <nutzdaten>
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AVANSIA_PRN_H
#define AVANSIA_PRN_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define AVA_CARD_W 2048
#define AVA_CARD_H 1300
#define AVA_PLANE_BYTES (AVA_CARD_W * AVA_CARD_H)

typedef enum { PRN_CHECKPOINT = 0, PRN_FRAME = 1 } prn_kind_t;

typedef struct {
  prn_kind_t kind;
  char name[32];   /* nur bei PRN_CHECKPOINT */
  uint8_t *frame;  /* nur bei PRN_FRAME: kompletter Rahmen inkl. 6-Byte-Kopf */
  size_t frame_len;
} prn_record_t;

/* Ein Dokument = eine Karte (HEADER_BEGIN ... DOC_END). */
typedef struct {
  prn_record_t *recs;
  size_t count, cap;
  int duplex;      /* 1 = beidseitig */
  int faces;       /* 1 oder 2 */
  size_t frames;   /* Anzahl Binaerrahmen */
  size_t bytes;    /* Summe der Rahmenbytes */
} prn_doc_t;

/* Liest das naechste Dokument aus dem Strom.
 * Rueckgabe: 1 = Dokument gelesen, 0 = sauberes Dateiende, -1 = Fehler. */
int prn_read_doc(FILE *in, prn_doc_t *doc, char *err, size_t errlen);

/* Prueft Struktur und Befehls-Allowlist eines Dokuments (eine Karte,
 * eine oder zwei Seiten, nur bekannte Druckbefehle).
 * Rueckgabe: 0 = gueltig, -1 = ungueltig (Grund in err). */
int prn_validate(prn_doc_t *doc, char *err, size_t errlen);

void prn_free(prn_doc_t *doc);

/* Hilfen */
const uint8_t *prn_payload(const prn_record_t *r, size_t *len);

#endif
