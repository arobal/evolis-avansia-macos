/*
 * image.c - Kartengeometrie und BMP-Ausgabe.
 *
 * SPDX-License-Identifier: MIT
 */
#include "image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W AVA_CARD_W
#define H AVA_CARD_H

int card_alloc(card_image_t *c) {
  c->rgb = malloc((size_t)W * H * 3);
  if (!c->rgb) return -1;
  memset(c->rgb, 255, (size_t)W * H * 3);
  return 0;
}

void card_free(card_image_t *c) {
  free(c->rgb);
  c->rgb = NULL;
}

static inline void src_px(const uint8_t *src, int w, int bpp, int x, int y, int rot, int sw_rot, uint8_t out[3]) {
  /* (x,y) in gedrehten Koordinaten -> Originalkoordinaten */
  int ox = x, oy = y;
  if (rot) { /* 90 Grad im Uhrzeigersinn: gedreht(x,y) = original(y, H0-1-x) */
    ox = y;
    oy = sw_rot - 1 - x;
  }
  const uint8_t *p = src + ((size_t)oy * w + ox) * bpp;
  if (bpp >= 3) {
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
  } else {
    out[0] = out[1] = out[2] = p[0];
  }
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

fit_mode_t card_from_raster(card_image_t *c, const uint8_t *src, int w, int h, int bpp, int xdpi, int ydpi) {
  int rot = h > w;
  int rw = rot ? h : w, rh = rot ? w : h; /* Groesse nach Drehung */
  double rx = rot ? ydpi : xdpi, ry = rot ? xdpi : ydpi;
  if (rx <= 0) rx = 600;
  if (ry <= 0) ry = 600;
  /* Physikalische Groesse in 600-dpi-Pixeln */
  double pw = rw * 600.0 / rx, ph = rh * 600.0 / ry;
  fit_mode_t mode;
  double sx, sy;
  /* Kartengroesse CR80 = 2022 x 1275 px; mit Beschnitt bis ~2100 x 1360 px */
  if (pw >= 1950 && pw <= 2120 && ph >= 1220 && ph <= 1370) {
    mode = FIT_PHYSICAL;
    sx = 600.0 / rx;
    sy = 600.0 / ry;
  } else {
    mode = FIT_COVER;
    double s = fmax((double)W / rw, (double)H / rh);
    sx = sy = s;
  }
  /* Zielpixel (X,Y) -> Quellkoordinate, zentriert; ausserhalb = Randpixel. */
  int exact = fabs(sx - 1.0) < 1e-9 && fabs(sy - 1.0) < 1e-9;
  int offx = (W - rw) / 2, offy = (H - rh) / 2;
  for (int Y = 0; Y < H; Y++) {
    uint8_t *dst = c->rgb + (size_t)Y * W * 3;
    for (int X = 0; X < W; X++, dst += 3) {
      if (exact) {
        int x = clampi(X - offx, 0, rw - 1), y = clampi(Y - offy, 0, rh - 1);
        src_px(src, w, bpp, x, y, rot, h, dst);
        continue;
      }
      /* bilineare Interpolation */
      double fx = (X + 0.5 - W / 2.0) / sx + rw / 2.0 - 0.5;
      double fy = (Y + 0.5 - H / 2.0) / sy + rh / 2.0 - 0.5;
      int x0 = (int)floor(fx), y0 = (int)floor(fy);
      double ax = fx - x0, ay = fy - y0;
      int xa = clampi(x0, 0, rw - 1), xb = clampi(x0 + 1, 0, rw - 1);
      int ya = clampi(y0, 0, rh - 1), yb = clampi(y0 + 1, 0, rh - 1);
      uint8_t p00[3], p10[3], p01[3], p11[3];
      src_px(src, w, bpp, xa, ya, rot, h, p00);
      src_px(src, w, bpp, xb, ya, rot, h, p10);
      src_px(src, w, bpp, xa, yb, rot, h, p01);
      src_px(src, w, bpp, xb, yb, rot, h, p11);
      for (int k = 0; k < 3; k++) {
        double v = (p00[k] * (1 - ax) + p10[k] * ax) * (1 - ay) + (p01[k] * (1 - ax) + p11[k] * ax) * ay;
        dst[k] = (uint8_t)clampi((int)lround(v), 0, 255);
      }
    }
  }
  return mode;
}

void card_rotate180(card_image_t *c) {
  size_t n = (size_t)W * H;
  uint8_t *a = c->rgb, *b = c->rgb + (n - 1) * 3;
  for (size_t i = 0; i < n / 2; i++, a += 3, b -= 3) {
    uint8_t t[3];
    memcpy(t, a, 3);
    memcpy(a, b, 3);
    memcpy(b, t, 3);
  }
}

int card_is_blank(const card_image_t *c) {
  size_t n = (size_t)W * H * 3;
  for (size_t i = 0; i < n; i++)
    if (c->rgb[i] < 250) return 0;
  return 1;
}

static void le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

/* 24 Bit, unkomprimiert, bottom-up - so verlangt es das Evolis-SDK. */
int card_write_bmp(const card_image_t *c, const char *path) {
  const uint32_t row = W * 3; /* 6144, bereits 4-Byte-ausgerichtet */
  const uint32_t data = row * H;
  uint8_t hdr[54] = {'B', 'M'};
  le32(hdr + 2, 54 + data);
  le32(hdr + 10, 54);
  le32(hdr + 14, 40);
  le32(hdr + 18, W);
  le32(hdr + 22, H);
  hdr[26] = 1;  /* Ebenen */
  hdr[28] = 24; /* Bit pro Pixel */
  le32(hdr + 34, data);
  le32(hdr + 38, 23622); /* 600 dpi in Pixel/Meter */
  le32(hdr + 42, 23622);
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  int ok = fwrite(hdr, 1, 54, f) == 54;
  uint8_t *line = malloc(row);
  if (!line) ok = 0;
  for (int y = H - 1; ok && y >= 0; y--) {
    const uint8_t *s = c->rgb + (size_t)y * row;
    for (int x = 0; x < W; x++) {
      line[x * 3 + 0] = s[x * 3 + 2];
      line[x * 3 + 1] = s[x * 3 + 1];
      line[x * 3 + 2] = s[x * 3 + 0];
    }
    ok = fwrite(line, 1, row, f) == row;
  }
  free(line);
  if (fclose(f)) ok = 0;
  return ok ? 0 : -1;
}
