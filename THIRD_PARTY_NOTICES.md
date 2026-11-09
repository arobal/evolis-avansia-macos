# Hinweise zu Fremdsoftware

Der eigene Code dieses Projekts steht unter der MIT-Lizenz (siehe [LICENSE](LICENSE)).
Für die folgenden Bestandteile gelten deren eigene Bedingungen.

## libusb (LGPL-2.1)

libusb wird beim Bauen statisch in das Backend `avansia` und in `avansiactl`
eingebunden. Der Quellcode ist unter <https://libusb.info/> erhältlich. Mit dem
mitgelieferten `Makefile` lassen sich die Programme gegen eine veränderte
libusb-Version neu linken (`LIBUSB_PREFIX=…`).

## Evolis SDK (libevolis, proprietär)

`libevolis` gehört Evolis und ist **nicht** Teil dieses Projekts. Das
Installationsskript lädt sie aus dem offiziellen PyPI-Paket
[`evolis-sdk`](https://pypi.org/project/evolis-sdk/) 9.4.1 und prüft das Paket
per SHA-256. Das Paket enthält keine ausdrückliche Lizenzangabe; die Nutzung
liegt in der Verantwortung der Anwendenden.

## avansia-macos-driver (MIT)

Details zum USB-Protokoll der Avansia (Rahmenformat, Quittungen, Statusbits,
Kartenstart und -ende) und die SDK-Einstellungen für die Avansia stammen aus
[qianxueye/avansia-macos-driver](https://github.com/qianxueye/avansia-macos-driver),
lizenziert unter der MIT-Lizenz.

## Marken

„Evolis“ und „Avansia“ sind Marken der Evolis SA. Dieses Projekt ist
inoffiziell und steht in keiner Verbindung zu Evolis.
