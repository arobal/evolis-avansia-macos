/*
 * sdk.h - Duenne Schicht ueber libevolis (offizielles Evolis SDK), die
 *         offline (ohne Druckerverbindung) Avansia-Auftraege erzeugt.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AVANSIA_SDK_H
#define AVANSIA_SDK_H

#include <stddef.h>

#define AVANSIA_DEFAULT_SDK "/Library/Printers/Evolis/Avansia/lib/libevolis.dylib"

typedef struct {
  const char *ribbon;        /* "YMCK" oder "YMCKK" */
  const char *black_front;   /* ALLBLACKPOINT | NOBLACKPOINT */
  const char *black_back;
  int back_rotate180;        /* Rueckseite um 180 Grad drehen */
} ava_sdk_opts_t;

/* Laedt die Bibliothek (Pfad NULL = $AVANSIA_SDK oder Standardpfad). */
int ava_sdk_load(const char *path, char *err, size_t errlen);
const char *ava_sdk_version(void);

/* Erzeugt aus 24-Bit-BMPs (2048 x 1300) eine .prn-Datei.
 * back_bmp darf NULL sein (einseitig). */
int ava_sdk_make_prn(const char *front_bmp, const char *back_bmp, const ava_sdk_opts_t *o, const char *out_prn,
                     char *err, size_t errlen);

#endif
