/*
 * image.h - Kartenbild (2048 x 1300, RGB) und BMP-Ausgabe.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AVANSIA_IMAGE_H
#define AVANSIA_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "prn.h" /* AVA_CARD_W / AVA_CARD_H */

typedef struct {
  uint8_t *rgb; /* AVA_CARD_W * AVA_CARD_H * 3, zeilenweise von oben */
} card_image_t;

typedef enum { FIT_PHYSICAL = 0, FIT_COVER = 1 } fit_mode_t;

/* Uebertraegt ein beliebiges RGB-Seitenraster auf die Karte.
 *  - Hochformat wird um 90 Grad im Uhrzeigersinn gedreht.
 *  - Hat die Seite (bei gegebener Aufloesung) ungefaehr Kartengroesse,
 *    wird sie in Originalgroesse zentriert; der Rand bis 2048 x 1300
 *    (Beschnitt) wird durch Verlaengern der Randpixel gefuellt.
 *  - Sonst wird auf volle Kartenflaeche skaliert (zentriert beschnitten).
 * Rueckgabe: verwendeter Modus. */
fit_mode_t card_from_raster(card_image_t *card, const uint8_t *src, int w, int h, int bpp_bytes, int xdpi,
                            int ydpi);

int card_alloc(card_image_t *card);
void card_free(card_image_t *card);
void card_rotate180(card_image_t *card);
int card_is_blank(const card_image_t *card);
int card_write_bmp(const card_image_t *card, const char *path);

#endif
