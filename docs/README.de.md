# NightKite Link Dokumentation

## Projektuebersicht

NightKite Link ist ein kompaktes Handheld zum Konfigurieren und Warten von
NightKite-Multi-Controllern. Das Projekt ist fuer den M5Stack Cardputer-Adv /
StampS3 ausgelegt und kommuniziert per USB-Host-Modus mit dem Controller.

Ziel ist, einen NightKite-Controller ohne Laptop konfigurieren und warten zu
koennen. NightKite Link nutzt dafuer soweit moeglich die vorhandene
NightKite-USB-CLI. Fuer den normalen Konfigurationsablauf sind keine Aenderungen
an der NightKite-Multi-Firmware erforderlich, solange die erwarteten CLI-Befehle
vorhanden sind.

Die Oberflaeche ist kartenbasiert, weil das Display des Cardputer-Adv sehr klein
ist. Eine Card zeigt jeweils eine Hauptfunktion oder eine kleine Gruppe
zusammenhaengender Einstellungen.

## Funktionen

Im aktuellen Code vorhanden:

- Cardputer-Akkuanzeige inklusive Ladestatus
- USB-Verbindungsstatus
- Controller-/CLI-Verbindungsstatus mit Timeout-Erkennung
- Helligkeit anzeigen und direkt beim Aendern senden
- Striplänge anzeigen und als Entwurfswert konfigurieren
- Aktives Pattern anzeigen und direkt beim Aendern senden
- Smoothing anzeigen und als Entwurfswert konfigurieren
- Accelerometer- und Gyro-Range konfigurieren
- Motion-Service-Card mit FPS-Anzeige und Kalibrieraktionen
- Autoplay-Status und Autoplay-Intervall konfigurieren
- Pattern-Liste mit Cycle- und Invert-Status
- Pattern-Detailansicht mit Cycle- und Invert-Toggles
- Bulk-Aktionen fuer Patterns:
  - aktuelle Pattern-Zustaende speichern
  - alle Patterns fuer Cycle aktivieren
  - alle Patterns fuer Cycle deaktivieren
  - alle Patterns invertieren
  - alle Patterns auf normal setzen
- Profile auf SD-Karte:
  - aktuelle Controller-Settings speichern
  - JSON-Profile auflisten
  - ausgewaehltes Profil laden
  - geladenes Profil auf den Controller anwenden
  - ausgewaehlte Profile loeschen
- Firmware-Bereich:
  - scannt `.uf2`-Dateien aus `/firmware/`
  - Zielauswahl fuer `RP2040` und `RP2350`
  - eigener grafischer Flash-Workflow
  - UF2-Validierung vor dem Flashen
  - Fortschrittsanzeige beim Kopieren auf USB Mass Storage

Der UF2-Mass-Storage-Flasher ist aktuell **experimental / work in progress**.
Er ist als Service- oder Recovery-Workflow gedacht, nicht als normaler
CLI-Befehl.

## Hardware-Anforderungen

- M5Stack Cardputer-Adv / StampS3
- microSD-Karte
- USB-C-Kabel und passendes USB-OTG-Setup
- NightKite-Multi-Controller, zum Beispiel auf Basis eines Pimoroni Pico LiPo 2 /
  RP2350
- UF2-Firmwaredateien fuer den Firmware-Flasher

Fuer UF2-Flashing muss der Controller manuell in den BOOTSEL- /
USB-Mass-Storage-Modus gebracht werden. Der aktuelle Code setzt keinen
automatischen `reboot_bootsel`-Befehl voraus.

## Software-Anforderungen

- PlatformIO
- VS Code mit PlatformIO-Erweiterung oder PlatformIO CLI
- Git
- USB-Treiber, falls das Betriebssystem sie benoetigt

Die normale CLI-Konfiguration nutzt die vorhandene NightKite-USB-CLI. Dafuer
sind keine Aenderungen an der NightKite-Multi-Firmware noetig, sofern die
erwarteten CLI-Befehle bereitstehen.

## Bauen und Flashen von NightKite Link

Repository klonen:

```sh
git clone https://github.com/SunnyCodes86/nightkite-link.git
cd nightkite-link
```

Danach den Ordner in VS Code / PlatformIO oeffnen oder direkt die PlatformIO CLI
verwenden.

Das konfigurierte PlatformIO-Environment heisst `cardputer`.

Build:

```sh
pio run
```

Upload:

```sh
pio run -t upload
```

Serial Monitor:

```sh
pio device monitor
```

Aktuelles Target aus `platformio.ini`:

- platform: `pioarduino/platform-espressif32` 51.03.03 Paket-URL
- board: `m5stack-stamps3`
- framework: `arduino`
- monitor speed: `115200`

## SD-Kartenstruktur

Der Code erwartet diese Verzeichnisse auf der microSD-Karte. Sie werden bei
erfolgreicher SD-Initialisierung angelegt:

```text
/firmware/
  nightkite_multi_rp2350_v3.uf2

/profiles/
  profile_001.json
  profile_002.json
```

Firmwaredateien:

- liegen unter `/firmware/`
- muessen die Endung `.uf2` haben
- werden auf der Firmware-Card ausgewaehlt

Profile:

- liegen unter `/profiles/`
- verwenden `.json`
- neue Profile werden als `profile_001.json` bis `profile_999.json` benannt

## Profilformat

Profile werden als JSON geschrieben, aber ohne ArduinoJson verarbeitet. Das
Laden ist bewusst einfach gehalten und liest die Schluessel, die der aktuelle
Code nutzt.

Aktuell gespeicherte Struktur:

```json
{
  "profile_version": 1,
  "project": "NightKite Link",
  "target": "NightKite Multi",
  "settings": {
    "brightness": 159,
    "strip_length": 50,
    "active_pattern": 7,
    "smoothing": 45,
    "accel_range": 4,
    "gyro_range": 500,
    "enabled_pattern_mask": 4194303,
    "inverted_pattern_mask": 0,
    "autoplay": {
      "enabled": true,
      "interval_seconds": 30
    },
    "patterns": [
      {
        "id": 1,
        "name": "Rainbow",
        "cycle_enabled": true,
        "inverted": false
      }
    ]
  }
}
```

Beim Anwenden eines geladenen Profils sendet NightKite Link die einzelnen
Settings, aktiviert/deaktiviert Patterns anhand der Masken, setzt alle Patterns
zuerst auf normal und setzt danach die invertierte Pattern-Liste erneut.

## Bedienung

Die aktuelle Tastaturbehandlung verarbeitet diese Eingaben:

| Taste | Aktion |
| --- | --- |
| `A` / `D` | Vorherige / naechste Card |
| `W` / `S` | Wert aendern oder Auswahl bewegen |
| `Enter` | Anwenden, oeffnen, bestaetigen oder im Flash-Workflow fortfahren |
| `Backspace` / `DEL` | Zurueck oder abbrechen, wo unterstuetzt |
| `Tab` | Naechste Card |
| `R` | Aktuelle Card bzw. Controllerdaten neu lesen, wo implementiert |
| `C` | Config-Feld waehlen, Firmware-Ziel umschalten oder Pattern-Cycle toggeln |
| `I` | Pattern-Invert toggeln oder ausgewaehltes Profil auf der Profiles-Card loeschen |
| `,` / `<` | Vorherige Card |
| `/` / `?` | Naechste Card |
| `;` / `:` | Gleiche Richtung wie `W` |
| `.` / `>` | Gleiche Richtung wie `S` |

Waehrend kritischer Firmware-Kopierzustände ist die normale Card-Navigation
gesperrt. Abbrechen ist nur in sicheren Flash-Zustaenden moeglich.

## UI-Konzept

NightKite Link nutzt ein Card-based Interface statt eines grossen klassischen
Menues, weil das Display nur 240 x 135 px gross ist. Jede Card steht fuer eine
Hauptaufgabe: Status, Brightness, Config, Calibration, Active Pattern,
Pattern-Liste, Bulk-Aktionen, Firmware oder Profiles.

Die Statusleiste oben zeigt USB-Status, Controller-Status, Command-Queue,
Seitennummer und Cardputer-Akku. Der Firmware-Flasher verwendet eigene
Workflow-Screens fuer Bestaetigung, BOOTSEL-Anweisung, Warten, Fortschritt,
Reboot und Fehler.

## Controller-Kommunikation

NightKite Link nutzt `USBHostSerial` im USB-Host-Modus, wenn
`NIGHTKITE_USB_HOST=1` aktiviert ist. Fuer Builds ohne USB-Host existiert ein
Debug-Serial-Transport.

Der Parser verarbeitet:

- `OK key=value ...`
- `ERR ...`
- `INFO ...`
- `[NightKite CLI] ...`

Der Code aktualisiert Controllerdaten aus Schluesseln wie:

- `pattern`
- `brightness`
- `strip_length`
- `smoothing`
- `accel_range`
- `gyro_range`
- `autoplay`
- `autoplay_interval`
- `enabled_patterns`
- `inverted_patterns`
- `battery_voltage`
- `boot_calibration`
- `fps`

Vom Code aktuell gesendete Befehle:

- `show`
- `patterns`
- `get inverted_patterns`
- `set brightness <value>`
- `set strip_length <value>`
- `set pattern <id>`
- `set smoothing <value>`
- `set accel_range <value>`
- `set gyro_range <value>`
- `set autoplay on|off`
- `set autoplay_interval <seconds>`
- `enable_pattern <id or comma-list>`
- `disable_pattern <id or comma-list>`
- `invert_pattern <id or comma-list>`
- `normal_pattern <id or comma-list>`
- `timing`
- `calibrate quick`
- `calibrate precise`
- `set boot_calibration quick|off`
- `save`

Bulk-Invert wird aktuell ueber kommaseparierte `invert_pattern`- bzw.
`normal_pattern`-Befehle umgesetzt. Im Code ist ein zukuenftiger dedizierter
Befehl im Stil von `set all_patterns_invert` als TODO markiert, falls die
Controller-Firmware so etwas spaeter anbietet.

## Firmware-Flasher

Der Firmware-Flasher arbeitet mit UF2-Dateien auf der SD-Karte und dem
USB-Mass-Storage-Modus des RP2040/RP2350-Controllers.

Aktueller Ablauf:

1. Eine `.uf2`-Firmwaredatei nach `/firmware/` auf der SD-Karte kopieren.
2. Firmware-Card oeffnen.
3. UF2-Datei mit `W` / `S` auswaehlen.
4. Ziellabel mit `C` waehlen (`RP2040` oder `RP2350`).
5. `Enter` druecken.
6. Ausgewaehlte Datei bestaetigen.
7. Controller manuell in BOOTSEL bringen.
8. USB neu verbinden, sodass der Controller als USB-Mass-Storage-Geraet
   erscheint.
9. Mit `Enter` fortfahren.
10. NightKite Link wartet auf Mass Storage, mountet es unter `/usb` und kopiert
    die UF2-Datei als `/usb/FIRMWARE.UF2`.
11. Die UI zeigt Fortschritt, kopierte KB und Prozent.
12. Nach dem Kopieren wird das VFS unmounted und auf Reboot/Disconnect gewartet.
13. Erfolg oder Fehler wird auf einem eigenen Screen angezeigt.

UF2-Validierung:

- Datei existiert
- Dateigroesse ist groesser als null
- Dateigroesse ist durch 512 teilbar
- Magic-Werte im ersten UF2-Block sind gueltig

Family-ID-Erkennung ist vorhanden, aber eine harte zielabhängige
Family-ID-Pruefung ist im Code als TODO markiert.

Warnungen:

- Waehrend des Kopierens nicht trennen.
- Nur UF2-Dateien verwenden, die zum Controller passen.
- Der Flasher ist experimental / work in progress.
- Der Flasher ist ein Service-/Recovery-Workflow und kein normaler
  NightKite-CLI-Befehl.

## Fehlerbehebung

### Cardputer wird nicht geflasht

- Pruefen, ob der richtige USB-Port in PlatformIO gewaehlt ist.
- `pio run -t upload` verwenden.
- Falls der Upload fehlschlaegt, den Cardputer-Adv neu verbinden.

### Controller wird nicht erkannt

- USB-OTG-Kabel und Stromversorgung pruefen.
- Die Statusleiste zeigt den USB-Verbindungsstatus.
- Auf passenden Cards mit `R` neu lesen.

### `USB disconnected`

- Der USB-Host-Transport hat den Controller verloren.
- Controller neu verbinden.
- Ausstehende Kommandos werden vom Code geloescht.

### `Controller timeout`

- USB kann physisch noch verbunden sein, aber die CLI hat innerhalb des
  konfigurierten Timeouts nicht geantwortet.
- Controller neu verbinden oder Refresh senden.

### SD card not ready

- Pruefen, ob die microSD-Karte steckt und mit einem von der Arduino-SD-Library
  unterstuetzten Dateisystem formatiert ist.
- Die App legt `/profiles/` und `/firmware/` an, wenn moeglich.

### No UF2 file found

- `.uf2`-Dateien unter `/firmware/` ablegen.
- Firmware-Card mit `R` neu scannen.

### Invalid UF2

- Die ausgewaehlte Datei hat die grundlegende UF2-Validierung nicht bestanden.
- Pruefen, ob es eine echte UF2-Datei ist und keine umbenannte Binary.

### Mass Storage timeout

- Der Controller ist nicht rechtzeitig als USB-Mass-Storage-Geraet erschienen.
- RP2040/RP2350 manuell in BOOTSEL bringen und USB neu verbinden.

### Mount failed

- Das Geraet wurde erkannt, aber FAT/VFS-Mount ist fehlgeschlagen.
- Controller erneut in BOOTSEL verbinden und noch einmal versuchen.

### Write failed

- Das Kopieren der UF2-Datei ist fehlgeschlagen.
- Waehrend des Kopierens nicht trennen. Mit bekannter guter UF2-Datei und Kabel
  erneut versuchen.

### Nach dem Firmware-Flash

- Controller rebooten lassen.
- Danach wieder normal verbinden, damit die NightKite-USB-CLI verfuegbar ist.

## Entwicklungsnotizen

- Das Projekt ist bewusst kompakt und auf ein kleines Handheld-Display
  zugeschnitten.
- Die UI soll nicht blockieren; `M5Cardputer.update()` muss regelmaessig laufen.
- USB-CLI-Kommunikation und UF2-Mass-Storage-Flasher sollen klar getrennt
  bleiben.
- Die aktuelle Command-Queue sendet CLI-Befehle mit kurzem Abstand.
- Der Firmware-Flasher pausiert normales CLI-Polling, solange er aktiv ist.
- `scripts/patch_m5cardputer.py` patcht beim Build bei Bedarf die
  M5Cardputer-Abhaengigkeit und fuegt dort einen fehlenden GPIO-Include hinzu.

Moegliche spaetere Erweiterungen:

- Robusteres Verhalten des UF2-Flashers in Randfaellen
- Optionales `reboot_bootsel`, falls NightKite Multi das spaeter anbietet
- Optional spaeter BIN/ELF/Picoboot; aktuell kein Kernziel
- Bessere Profilvalidierung und robusteres Profilparsing
- Release-Workflow mit fertigen Firmware-Binaries

## Roadmap

- UI-Polish fuer bessere Lesbarkeit auf dem kleinen Display
- Robustere Profilverwaltung
- Firmware-Flasher auf echten RP2040/RP2350-Boards weiter stabilisieren
- NightKite-CLI-Kompatibilitaet formaler dokumentieren
- Release-Workflow mit fertigen Builds
- Optional automatische Firmware-Versionserkennung
- Optional weitere Controller-Ziele

## Lizenz

Es wurde noch keine Lizenzdatei hinzugefuegt.

Die vendored Espressif-Komponente `usb_host_msc` enthaelt ihre eigene Lizenzdatei
unter `lib/usb_host_msc/LICENCE`.

## Ressourcen

- NightKite Multi: https://github.com/SunnyCodes86/nightkite-multi
- NightKite Link: https://github.com/SunnyCodes86/nightkite-link
- M5Cardputer Library: https://github.com/m5stack/M5Cardputer
- Cardputer-Adv Documentation: https://docs.m5stack.com/en/core/Cardputer-Adv
- ESP USB MSC Host Component: https://components.espressif.com/components/espressif/usb_host_msc
- ESP-IDF USB Host: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html
- PlatformIO: https://platformio.org/
