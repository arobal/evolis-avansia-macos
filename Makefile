# Evolis Avansia - inoffizieller macOS-Treiber
#
#   make            alles bauen (nach build/)
#   make test       Offline-Tests (ohne Drucker, ohne Evolis-SDK)
#   make clean
#
# Installation: sudo ./scripts/install.sh

CC      ?= clang
ARCH    ?= $(shell uname -m)
CFLAGS  ?= -O2 -g
CFLAGS  += -arch $(ARCH) -mmacosx-version-min=14.0 -std=c11 -Wall -Wextra -Wno-unused-parameter
LIBUSB_PREFIX ?= $(shell brew --prefix libusb 2>/dev/null || echo /opt/homebrew/opt/libusb)
LIBUSB_CFLAGS  = -I$(LIBUSB_PREFIX)/include/libusb-1.0
# libusb statisch einbinden: das Backend laeuft im CUPS-Sandkasten und soll
# nichts aus /opt/homebrew nachladen muessen.
LIBUSB_LIBS    = $(LIBUSB_PREFIX)/lib/libusb-1.0.a -framework IOKit -framework CoreFoundation -framework Security

B = build

all: $(B)/rastertoavansia $(B)/avansia $(B)/avansiactl $(B)/EvolisAvansia.ppd

$(B):
	mkdir -p $(B)

$(B)/rastertoavansia: src/rastertoavansia.c src/image.c src/sdk.c src/image.h src/sdk.h src/prn.h | $(B)
	$(CC) $(CFLAGS) -o $@ src/rastertoavansia.c src/image.c src/sdk.c -lcups

$(B)/avansia: src/backend.c src/printjob.c src/prn.c src/usb.c src/*.h | $(B)
	$(CC) $(CFLAGS) $(LIBUSB_CFLAGS) -o $@ src/backend.c src/printjob.c src/prn.c src/usb.c $(LIBUSB_LIBS)

$(B)/avansiactl: src/avansiactl.c src/printjob.c src/prn.c src/usb.c src/sdk.c src/*.h | $(B)
	$(CC) $(CFLAGS) $(LIBUSB_CFLAGS) -o $@ src/avansiactl.c src/printjob.c src/prn.c src/usb.c src/sdk.c $(LIBUSB_LIBS)

# PPD kopieren (Basistexte ASCII, deutsche Uebersetzungen UTF-8)
$(B)/EvolisAvansia.ppd: ppd/EvolisAvansia.ppd | $(B)
	cp $< $@

# ---- Tests ---------------------------------------------------------------
$(B)/test_prn: tests/test_prn.c src/prn.c src/prn.h | $(B)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_prn.c src/prn.c

$(B)/test_image: tests/test_image.c src/image.c src/image.h | $(B)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_image.c src/image.c

test: all $(B)/test_prn $(B)/test_image
	./tests/run-tests.sh

clean:
	rm -rf $(B)

.PHONY: all test clean
