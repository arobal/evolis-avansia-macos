/*
 * usb.c - USB-Transport fuer die Evolis Avansia (libusb).
 *
 * Protokoll (ermittelt aus libevolis-Ausgaben und dem MIT-lizenzierten
 * Projekt github.com/qianxueye/avansia-macos-driver):
 *
 *   Anfrage:  0x1C <cmd> <laenge 32 bit BE> <nutzdaten>
 *   Antwort:  0x1C <cmd> <laenge 32 bit BE> <daten>
 *
 *   cmd 'P'  Druckrahmen (Antwort = 6-Byte-Quittung)
 *   cmd 'T'  Status   (Laenge = gewuenschte Antwortgroesse, 60 Byte)
 *   cmd 'Y'  Geraeteinfo (52 Byte)
 *
 * Vor jedem Bulk-IN-Lesevorgang erwartet der Drucker eine
 * Class-Control-Anfrage (bmRequestType 0x21, bRequest 1).
 *
 * SPDX-License-Identifier: MIT
 */
#include "usb.h"

#include <libusb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct ava_dev {
  libusb_context *ctx;
  libusb_device_handle *h;
  int iface;
  uint8_t ep_out, ep_in;
  uint8_t *buf;     /* Lesepuffer */
  size_t buflen;    /* gueltige Bytes */
};

#define RX_CHUNK 65536
#define TX_CHUNK 65536

static void seterr(char *err, size_t errlen, const char *fmt, ...) {
  if (!err || !errlen) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, errlen, fmt, ap);
  va_end(ap);
}

static double now_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static uint16_t wanted_pid(void) {
  const char *e = getenv("AVANSIA_USB_PID");
  if (e && *e) return (uint16_t)strtoul(e, NULL, 0);
  return AVA_PID;
}

static void read_serial(libusb_device_handle *h, const struct libusb_device_descriptor *d, char *out,
                        size_t len) {
  out[0] = 0;
  if (d->iSerialNumber && h) {
    int n = libusb_get_string_descriptor_ascii(h, d->iSerialNumber, (unsigned char *)out, (int)len);
    if (n < 0) out[0] = 0;
  }
}

int ava_list(ava_list_cb cb, void *cbctx) {
  libusb_context *ctx = NULL;
  if (libusb_init(&ctx)) return 0;
  libusb_device **list = NULL;
  ssize_t n = libusb_get_device_list(ctx, &list);
  int found = 0;
  for (ssize_t i = 0; i < n; i++) {
    struct libusb_device_descriptor d;
    if (libusb_get_device_descriptor(list[i], &d) || d.idVendor != AVA_VID) continue;
    char serial[64] = "";
    libusb_device_handle *h = NULL;
    if (!libusb_open(list[i], &h)) {
      read_serial(h, &d, serial, sizeof(serial));
      libusb_close(h);
    }
    if (cb) cb(d.idVendor, d.idProduct, serial, cbctx);
    found++;
  }
  if (list) libusb_free_device_list(list, 1);
  libusb_exit(ctx);
  return found;
}

static int find_endpoints(libusb_device *dev, ava_dev_t *a) {
  struct libusb_config_descriptor *cfg = NULL;
  a->iface = 0;
  a->ep_out = 0x01;
  a->ep_in = 0x81;
  if (libusb_get_active_config_descriptor(dev, &cfg) || !cfg) return 0;
  if (cfg->bNumInterfaces > 0 && cfg->interface[0].num_altsetting > 0) {
    const struct libusb_interface_descriptor *id = &cfg->interface[0].altsetting[0];
    a->iface = id->bInterfaceNumber;
    uint8_t out = 0, in = 0;
    for (int e = 0; e < id->bNumEndpoints; e++) {
      const struct libusb_endpoint_descriptor *ep = &id->endpoint[e];
      if ((ep->bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_BULK) continue;
      if (ep->bEndpointAddress & 0x80) {
        if (!in) in = ep->bEndpointAddress;
      } else if (!out) {
        out = ep->bEndpointAddress;
      }
    }
    if (out) a->ep_out = out;
    if (in) a->ep_in = in;
  }
  libusb_free_config_descriptor(cfg);
  return 0;
}

int ava_open(ava_dev_t **out, const char *serial, char *err, size_t errlen) {
  *out = NULL;
  ava_dev_t *a = calloc(1, sizeof(*a));
  if (!a) return -3;
  a->buf = malloc(RX_CHUNK * 2);
  if (!a->buf || libusb_init(&a->ctx)) {
    seterr(err, errlen, "libusb konnte nicht initialisiert werden");
    free(a->buf);
    free(a);
    return -3;
  }
  libusb_device **list = NULL;
  ssize_t n = libusb_get_device_list(a->ctx, &list);
  int rc = -1;
  uint16_t pid = wanted_pid();
  for (ssize_t i = 0; i < n && !a->h; i++) {
    struct libusb_device_descriptor d;
    if (libusb_get_device_descriptor(list[i], &d) || d.idVendor != AVA_VID || d.idProduct != pid) continue;
    libusb_device_handle *h = NULL;
    int orc = libusb_open(list[i], &h);
    if (orc) {
      rc = -2;
      seterr(err, errlen, "Avansia gefunden, aber nicht zu oeffnen: %s", libusb_error_name(orc));
      continue;
    }
    if (serial && *serial) {
      char s[64];
      read_serial(h, &d, s, sizeof(s));
      if (strcmp(s, serial)) {
        libusb_close(h);
        continue;
      }
    }
    find_endpoints(list[i], a);
    int crc = libusb_claim_interface(h, a->iface);
    if (crc) {
      rc = -2;
      seterr(err, errlen, "USB-Schnittstelle der Avansia ist belegt (%s) - laeuft noch ein anderes Programm?",
             libusb_error_name(crc));
      libusb_close(h);
      continue;
    }
    a->h = h;
    rc = 0;
  }
  if (list) libusb_free_device_list(list, 1);
  if (rc) {
    if (rc == -1) seterr(err, errlen, "Keine Avansia (USB %04x:%04x) gefunden", AVA_VID, pid);
    libusb_exit(a->ctx);
    free(a->buf);
    free(a);
    return rc;
  }
  *out = a;
  return 0;
}

void ava_close(ava_dev_t *a) {
  if (!a) return;
  if (a->h) {
    libusb_release_interface(a->h, a->iface);
    libusb_close(a->h);
  }
  if (a->ctx) libusb_exit(a->ctx);
  free(a->buf);
  free(a);
}

/* ---- Rohtransport ------------------------------------------------------ */

static int usb_write(ava_dev_t *a, const uint8_t *data, size_t len, unsigned timeout_ms, char *err,
                     size_t errlen) {
  size_t off = 0;
  while (off < len) {
    int chunk = (int)((len - off) > TX_CHUNK ? TX_CHUNK : (len - off));
    int sent = 0;
    int rc = libusb_bulk_transfer(a->h, a->ep_out, (unsigned char *)data + off, chunk, &sent, timeout_ms);
    if (rc || sent != chunk) {
      seterr(err, errlen, "USB-Schreibfehler: %s (%d/%d Bytes)", libusb_error_name(rc), sent, chunk);
      return -1;
    }
    off += (size_t)sent;
  }
  return 0;
}

static int usb_read(ava_dev_t *a, uint8_t *out, size_t want, unsigned timeout_ms, char *err, size_t errlen) {
  while (a->buflen < want) {
    /* Class-Request "Daten anfordern" vor jedem Lesen */
    libusb_control_transfer(a->h, 0x21, 1, 0, (uint16_t)a->iface, NULL, 0, timeout_ms);
    int got = 0;
    int rc = libusb_bulk_transfer(a->h, a->ep_in, a->buf + a->buflen, RX_CHUNK, &got, timeout_ms);
    if (!((rc == 0 || rc == LIBUSB_ERROR_TIMEOUT) && got > 0)) {
      seterr(err, errlen, "USB-Lesefehler: %s", rc ? libusb_error_name(rc) : "keine Daten");
      return -1;
    }
    a->buflen += (size_t)got;
  }
  memcpy(out, a->buf, want);
  memmove(a->buf, a->buf + want, a->buflen - want);
  a->buflen -= want;
  return 0;
}

static void put_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static int query(ava_dev_t *a, uint8_t cmd, uint32_t expected, uint8_t *out, size_t cap, size_t *outlen,
                 char *err, size_t errlen) {
  uint8_t req[6] = {0x1C, cmd};
  put_be32(req + 2, expected);
  if (usb_write(a, req, sizeof(req), 5000, err, errlen)) return -1;
  uint8_t hdr[6];
  if (usb_read(a, hdr, 6, 5000, err, errlen)) return -1;
  uint32_t size = ((uint32_t)hdr[2] << 24) | ((uint32_t)hdr[3] << 16) | ((uint32_t)hdr[4] << 8) | hdr[5];
  if (hdr[0] != 0x1C || hdr[1] != cmd || size > 4096) {
    seterr(err, errlen, "Unerwartete Antwort des Druckers (%02x %02x, %u Bytes)", hdr[0], hdr[1], size);
    a->buflen = 0;
    return -1;
  }
  uint8_t tmp[4096];
  if (size && usb_read(a, tmp, size, 5000, err, errlen)) return -1;
  size_t n = size < cap ? size : cap;
  memcpy(out, tmp, n);
  *outlen = size;
  return 0;
}

static void ascii_field(const uint8_t *src, size_t n, char *dst, size_t cap) {
  size_t j = 0;
  for (size_t i = 0; i < n && j + 1 < cap; i++)
    if (src[i] >= 0x20 && src[i] < 0x7f) dst[j++] = (char)src[i];
  while (j && dst[j - 1] == ' ') j--;
  dst[j] = 0;
}

static void decode_flags(ava_status_t *st, uint8_t f) {
  st->flags = f;
  st->ready = (f & 0x11) == 0x11 && !(f & 0x04);
  st->printing = (f & 0x08) != 0;
  st->error = (f & 0x04) != 0;
  st->mech_busy = (f & 0x02) != 0;
  st->if_busy = (f & 0x20) != 0;
}

int ava_get_status(ava_dev_t *a, ava_status_t *st, char *err, size_t errlen) {
  memset(st, 0, sizeof(*st));
  st->ribbon_percent = st->film_percent = -1;
  uint8_t info[64], s[64];
  size_t ni = 0, ns = 0;
  if (query(a, 'Y', 52, info, sizeof(info), &ni, err, errlen)) return -1;
  if (query(a, 'T', 60, s, sizeof(s), &ns, err, errlen)) return -1;
  if (ni < 51 || ns < 60) {
    seterr(err, errlen, "Statusantwort unvollstaendig (%zu/%zu Bytes)", ni, ns);
    return -1;
  }
  ascii_field(info + 38, 4, st->firmware, sizeof(st->firmware));
  ascii_field(info + 34, 4, st->base_revision, sizeof(st->base_revision));
  ascii_field(info + 42, 8, st->serial, sizeof(st->serial));
  st->ribbon_type_code = info[2];
  st->ribbon_panel_code = info[3];
  decode_flags(st, s[0]);
  st->card_position = s[1];
  st->printer_status = s[2];
  st->standby = s[41];
  st->ribbon_percent = s[42] <= 100 ? s[42] : -1;
  st->film_percent = s[43] <= 100 ? s[43] : -1;
  memcpy(st->raw_status, s, 60);
  return 0;
}

void ava_status_describe(const ava_status_t *st, char *buf, size_t len) {
  snprintf(buf, len,
           "Seriennr. %s, Firmware %s, %s%s%s%s, Farbband %d%%, Film %d%%, Flags 0x%02x, Kartenposition %d, "
           "Standby %d",
           st->serial[0] ? st->serial : "?", st->firmware[0] ? st->firmware : "?",
           st->error ? "FEHLER" : st->ready ? "bereit" : "nicht bereit", st->printing ? ", druckt" : "",
           st->mech_busy ? ", Mechanik belegt" : "", st->if_busy ? ", Schnittstelle belegt" : "",
           st->ribbon_percent, st->film_percent, st->flags, st->card_position, st->standby);
}

/* ---- Karte drucken ------------------------------------------------------ */

static int wait_card_begin(ava_dev_t *a, char *err, size_t errlen) {
  double deadline = now_s() + 180;
  for (;;) {
    uint8_t s[64];
    size_t n = 0;
    if (query(a, 'T', 1, s, sizeof(s), &n, err, errlen)) return -1;
    if (n < 1) {
      seterr(err, errlen, "Statusantwort leer");
      return -1;
    }
    if (s[0] & 0x04) {
      seterr(err, errlen, "Drucker meldet einen Fehler (Flags 0x%02x) - bitte Display pruefen", s[0]);
      return -1;
    }
    if (s[0] & 0x10) return 0;
    if (now_s() > deadline) {
      seterr(err, errlen, "Drucker wurde innerhalb von 180 s nicht bereit fuer die Karte");
      return -1;
    }
    usleep(100000);
  }
}

static int wait_card_end(ava_dev_t *a, char *err, size_t errlen) {
  double deadline = now_s() + 180;
  for (;;) {
    uint8_t s[64];
    size_t n = 0;
    if (query(a, 'T', 60, s, sizeof(s), &n, err, errlen)) return -1;
    if (n < 60) {
      seterr(err, errlen, "Statusantwort unvollstaendig - Ergebnis unbekannt");
      return -1;
    }
    if (s[0] & 0x04) {
      seterr(err, errlen, "Drucker meldet einen Fehler (Flags 0x%02x) - bitte Display pruefen", s[0]);
      return -1;
    }
    if (s[41] == 1 || s[41] == 2) {
      seterr(err, errlen, "Drucker wartet auf manuelles Einlegen/Entnehmen einer Karte - bitte Display pruefen");
      return -1;
    }
    if (!(s[0] & 0x08)) return 0;
    if (now_s() > deadline) {
      seterr(err, errlen, "Kartenende nach 180 s nicht gemeldet - Ergebnis unbekannt");
      return -1;
    }
    usleep(250000);
  }
}

int ava_print_doc(ava_dev_t *a, const prn_doc_t *doc, ava_progress_cb cb, void *cbctx, char *err,
                  size_t errlen) {
  int submitted = 0;
  size_t frames_sent = 0;
  for (size_t i = 0; i < doc->count; i++) {
    const prn_record_t *r = &doc->recs[i];
    if (r->kind == PRN_FRAME) {
      submitted = 1;
      if (usb_write(a, r->frame, r->frame_len, 30000, err, errlen)) return -2;
      uint8_t ack[6];
      if (usb_read(a, ack, 6, 30000, err, errlen)) return -2;
      if (ack[0] != 0x1C || ack[1] != 'P') {
        seterr(err, errlen, "Unerwartete Quittung %02x %02x %02x %02x %02x %02x", ack[0], ack[1], ack[2], ack[3],
               ack[4], ack[5]);
        return -2;
      }
      frames_sent++;
      if (cb && r->frame_len > 1000000) {
        char m[96];
        snprintf(m, sizeof(m), "Sende Druckdaten (%zu/%zu)", frames_sent, doc->frames);
        cb(m, cbctx);
      }
    } else if (!strcmp(r->name, "CARD_BEGIN")) {
      if (cb) cb("Warte auf Kartenbereitschaft", cbctx);
      if (wait_card_begin(a, err, errlen)) return submitted ? -2 : -1;
    } else if (!strcmp(r->name, "CARD_END")) {
      if (cb) cb("Karte wird gedruckt", cbctx);
      if (wait_card_end(a, err, errlen)) return -2;
    }
  }
  return 0;
}
