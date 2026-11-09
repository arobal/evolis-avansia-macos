# Evolis Avansia: Treiber für macOS (inoffiziell)

Ein CUPS-Druckertreiber für den Retransfer-Kartendrucker **Evolis Avansia** an
Apple-Silicon-Macs (macOS 14 oder neuer, USB). Nach der Installation erscheint
die Avansia als normaler Drucker in jedem Druckdialog.

> **Status:** Getestet an einer echten Avansia (Firmware 6.1) mit YMCK-Farbband
> an einem Apple-Silicon-Mac: Statusabfrage, einseitiger und beidseitiger Druck,
> Quer- und Hochformat. Farben und schwarzer Text sind korrekt, der Druck geht
> nahezu bis zum Rand.
> Noch nicht an der Hardware getestet: YMCKK-Farbband, „Schwarz aus YMC mischen“
> und „nach oben umklappen“ beim beidseitigen Druck (siehe Tabelle unten).

## Aufbau

```
Programm (Vorschau, Word, ...)
   │ PDF
   ▼
macOS cgpdftoraster ─ rendert mit der PPD auf 600 dpi RGB, Kartenformat CR80
   │ CUPS-Raster
   ▼
rastertoavansia (Filter) ─ Kartenbild 2048×1300 px, Beschnitt, Drehung, Duplex-Paare
   │                       ruft libevolis (offizielles Evolis SDK) offline auf
   │ Avansia-Auftrag (.prn)
   ▼
avansia (CUPS-Backend) ─ prüft jede Karte vollständig und sendet sie per USB
   │                     mit Quittung pro Datenblock und Statusabfrage
   ▼
Evolis Avansia (USB 09c2:0022)
```

| Datei | Zweck |
|---|---|
| `ppd/EvolisAvansia.ppd` | Druckerbeschreibung: Kartenformate, Duplex, Farbband, Optionen (deutsch übersetzt) |
| `src/rastertoavansia.c` | CUPS-Filter, Raster zu Avansia-Auftrag |
| `src/backend.c` | CUPS-Backend `avansia://usb` |
| `src/avansiactl.c` | Hilfswerkzeug: Status, Auftrag prüfen oder erzeugen, Direktdruck |
| `src/prn.c` | Parser und Prüfung der Auftragsdaten (Allowlist der Druckbefehle) |
| `src/usb.c` | USB-Protokoll und Statusabfrage (libusb, statisch gelinkt) |
| `src/sdk.c` | Lädt `libevolis.dylib` per `dlopen` |
| `src/image.c` | Kartengeometrie, BMP-Ausgabe |
| `tools/testcard.swift` | Erzeugt eine zweiseitige Testkarte (PDF) |

Die **Druckersprache selbst** (Farbauszüge Y/M/C/K, Farbumrechnung) erzeugt das
offizielle Evolis-SDK. Evolis veröffentlicht es frei auf PyPI (`evolis-sdk`) und
liefert darin eine native macOS-Bibliothek für arm64 und x86_64 mit.
Dieses Projekt enthält das SDK **nicht**. Das Installationsskript lädt es direkt
von PyPI und prüft es per SHA-256.

## Voraussetzungen

- Mac mit Apple Silicon, macOS 14 oder neuer
- Xcode oder die Command Line Tools (`xcode-select --install`)
- Homebrew-Paket `libusb` zum Bauen (wird statisch eingebunden):

```bash
brew install libusb
```

## Bauen und testen (ohne Drucker)

```bash
make
```

```bash
make test
```

`make test` prüft Parser, Geometrie und PPD. Außerdem schickt es eine Testkarte
durch die echte macOS-Filterkette. Die erzeugten Kartenbilder liegen danach in
`tests/out/simplex` und `tests/out/duplex`.

## Installation

```bash
sudo ./scripts/install.sh
```

Das Skript:
1. lädt `evolis-sdk` 9.4.1 von PyPI, prüft den SHA-256 und entnimmt `libevolis.dylib`
2. installiert Filter, SDK und `avansiactl` nach `/Library/Printers/Evolis/Avansia/`
3. installiert das Backend nach `/usr/libexec/cups/backend/avansia`
4. legt die PPD nach `/Library/Printers/PPDs/Contents/Resources/` und
5. richtet den Drucker `Evolis_Avansia` mit der Adresse `avansia://usb` ein

Entfernen geht mit `sudo ./scripts/uninstall.sh`.

## Erster Test an der Avansia

1. Avansia per USB anschließen, einschalten, Farbband, Film und **eine** Karte einlegen.
2. Nur lesend prüfen, ob der Mac den Drucker sieht:

```bash
avansiactl list
```

```bash
avansiactl status
```

3. Testkarte erzeugen und **einseitig** drucken:

```bash
swift tools/testcard.swift testkarte.pdf
```

```bash
lp -d Evolis_Avansia -o PageSize=CR80 -o page-ranges=1 testkarte.pdf
```

4. Danach beidseitig. Der Pfeil auf der Rückseite muss in dieselbe Richtung
   zeigen wie vorne, wenn man die Karte seitlich umdreht (wie eine Buchseite):

```bash
lp -d Evolis_Avansia -o PageSize=CR80 -o Duplex=DuplexTumble testkarte.pdf
```

**Welche Kante?** Die Einstellung folgt dem CUPS-Standard und bezieht sich auf
die Kante, um die man die Karte wendet. „Seitlich umdrehen“ wurde an der Avansia mit
Quer- und Hochformat geprüft, „nach oben umklappen“ ist daraus abgeleitet:

| Karte | seitlich umdrehen (wie Buchseite) | nach oben umklappen (wie Kalender) |
|---|---|---|
| Querformat | **Kurze Kante** | Lange Kante |
| Hochformat | **Lange Kante** | Kurze Kante |

Passt es trotzdem einmal nicht, hilft die Option **„Rückseite zusätzlich um
180° drehen“**, im Druckdialog oder dauerhaft:

```bash
lpadmin -p Evolis_Avansia -o AvansiaBackRotate=True
```

## Drucken aus Programmen

Im Druckdialog den Drucker „Evolis Avansia“ wählen und als Papierformat eine
der CR80-Größen einstellen. Am besten passt ein Dokument, das bereits Kartengröße
hat (85,6 × 54 mm).

| Option | Werte |
|---|---|
| Kartenformat | CR80 quer/hoch (85,6 × 54 mm) oder mit Beschnitt (86,7 × 55 mm = 2048 × 1300 px) |
| Beidseitig drucken | Aus / Lange Kante / Kurze Kante (siehe Tabelle oben). Je zwei Seiten ergeben eine Karte |
| Farbband | YMCK (Standard) oder YMCKK. **Muss zum eingelegten Band passen** |
| Schwarz (K-Panel) | Schwarze Pixel mit K-Panel (scharfer Text) oder Schwarz aus YMC mischen |
| Leere Rückseite nicht drucken | spart Farbband, wenn die Rückseite weiß ist |
| Vorderseite / Rückseite drehen | Ausrichtung korrigieren |

Hochformatige Karten dreht der Treiber automatisch um 90°. Dokumente in
Kartengröße werden in Originalgröße gedruckt; die Randpixel werden dabei in den
Beschnitt verlängert, damit kein weißer Rand bleibt. Andere Formate werden
formatfüllend skaliert.

## Sicherheit

- Jede Karte wird erst **vollständig erzeugt und geprüft**, bevor ein Byte zum
  Drucker geht. Erlaubt sind nur die bekannten Druckbefehle: Karte starten,
  Farbauszüge, Wenden, Auswerfen. Wartungs-, Einstellungs- und Firmwarebefehle
  lässt der Treiber nicht durch.
- Eine Karte wird **nie automatisch wiederholt**. Bricht die Übertragung mitten
  in einer Karte ab, beendet der Treiber den Auftrag mit einer Meldung. Dann bitte
  Drucker und Karte prüfen.
- Ein Abbruch im Druckdialog wirkt erst **zwischen** zwei Karten. Eine Karte,
  deren Druck schon läuft, wird fertig gedruckt.

## Fehlersuche

```bash
cupsctl --debug-logging
```

```bash
tail -f /var/log/cups/error_log
```

Für Tests ohne CUPS kann `avansiactl` einen Auftrag auch direkt erzeugen und senden:

```bash
avansiactl make-prn vorne.bmp karte.prn --back hinten.bmp --ribbon YMCK
```

```bash
avansiactl print karte.prn --yes
```

(BMP: 2048 × 1300 px, 24 Bit, unkomprimiert.)

## Einschränkungen

- Nur USB, kein Netzwerk.
- Nur YMCK- und YMCKK-Farbbänder. YMCKI, YMCKH und YMCFK sind im SDK vorhanden,
  hier aber noch nicht freigeschaltet.
- Keine Magnetstreifen- oder Chipkodierung, keine Laminierung, kein UV-Panel.
- Der Farbbandtyp wird nicht automatisch erkannt und muss in den Optionen stimmen.
- Kein Farbmanagement über das hinaus, was SDK und macOS liefern.

## Eigene Bildkarten

`tools/bildkarte.swift` macht aus einem beliebigen Bild eine beidseitige
Hochformat-Karte: vorne das Bild randlos, hinten verkleinert mit Titel und Text.

```bash
swift tools/bildkarte.swift MEIN-BILD.jpg karte.pdf "Titel"
```

```bash
lp -d Evolis_Avansia -o PageSize=CR80Portrait -o Duplex=DuplexNoTumble karte.pdf
```

Eigene Motive und Drucke gehören in den Ordner `beispiele/`. Git ignoriert ihn.

## Vergleich mit avansia-macos-driver

Es gibt ein weiteres, unabhängiges Mac-Projekt für die Avansia:
[qianxueye/avansia-macos-driver](https://github.com/qianxueye/avansia-macos-driver).
Es bringt mehr Funktionen mit, unter anderem eine Begleit-App, Farbprofile,
Farbbanderkennung per RFID und Druckereinstellungen. Dieses Projekt ist bewusst
schlanker:

| | dieses Projekt | avansia-macos-driver |
|---|---|---|
| Aufbau | klassischer CUPS-Treiber (PPD, Filter, Backend) | App mit eigenem IPP-Druckdienst je Benutzer |
| Oberfläche | Deutsch, normaler macOS-Druckdialog | App auf Chinesisch, dazu der Druckdialog |
| Abhängigkeiten | libusb (statisch), Evolis SDK | eigenes CUPS und libusb, Evolis SDK |
| Farbband | in den Druckoptionen wählen | wird automatisch erkannt |
| Getestet | Avansia mit Firmware 6.1, YMCK | laut Projekt mit echten Karten |

## Herkunft und Lizenz

- Der eigene Code steht unter MIT-Lizenz (siehe [LICENSE](LICENSE)). Hinweise zu
  libusb, Evolis SDK und weiteren Quellen stehen in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- `libevolis` gehört Evolis und wird nicht mitgeliefert. Es wird zur Installation
  aus dem öffentlichen PyPI-Paket `evolis-sdk` geladen.
- Das USB-Protokoll (Rahmenformat, Quittungen, Statusbits, Kartenstart und
  -ende) und die Avansia-Einstellungen für das SDK stammen aus dem MIT-lizenzierten
  Projekt [qianxueye/avansia-macos-driver](https://github.com/qianxueye/avansia-macos-driver).
  Dessen Entwickler hat damit echte Karten gedruckt. Dieses Projekt ist eine
  unabhängige, schlanke Neuimplementierung als klassischer CUPS-Treiber mit deutscher
  Oberfläche.
- Kein offizielles Evolis-Produkt. Nutzung auf eigenes Risiko; eigene Software
  am Gerät kann bei Garantiefragen eine Rolle spielen.
