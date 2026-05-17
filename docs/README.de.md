# NightKite Link Dokumentation

## Projektübersicht

NightKite Link ist ein kompaktes Handheld zum Konfigurieren und Warten von
NightKite-Multi-Controllern. Das Projekt ist für den M5Stack Cardputer-Adv /
StampS3 ausgelegt und kommuniziert per USB-Host-Modus mit dem Controller.

Ziel ist, einen NightKite-Controller ohne Laptop konfigurieren und warten zu
können. NightKite Link nutzt dafür soweit möglich die vorhandene
NightKite-USB-CLI. Für den normalen Konfigurationsablauf sind keine Änderungen
an der NightKite-Multi-Firmware erforderlich, solange die erwarteten CLI-Befehle
vorhanden sind.

Die Oberfläche ist kartenbasiert, weil das Display des Cardputer-Adv sehr klein
ist. Eine Card zeigt jeweils eine Hauptfunktion oder eine kleine Gruppe
zusammenhängender Einstellungen.

## Funktionen

Im aktuellen Code vorhanden:

- Cardputer-Akkuanzeige inklusive Ladestatus
- PCM-basierte Startup-, Tasten-, Navigations-, Confirm-, Cancel-, Success- und Error-Sounds
- USB-Verbindungsstatus
- Controller-/CLI-Verbindungsstatus mit Timeout-Erkennung
- Helligkeit anzeigen und direkt beim Ändern senden
- Striplänge anzeigen und als Entwurfswert konfigurieren
- Aktives Pattern anzeigen und direkt beim Ändern senden
- Smoothing anzeigen und als Entwurfswert konfigurieren
- Accelerometer- und Gyro-Range konfigurieren
- Motion-Service-Card mit FPS-Anzeige und Kalibrieraktionen
- Autoplay-Status und Autoplay-Intervall konfigurieren
- Pattern-Liste mit Cycle- und Invert-Status
- Pattern-Detailansicht mit Cycle- und Invert-Toggles
- Sync-Test-Card zum Vorbereiten von Firmware-4.0-Master/Follower-Beacon-Tests
- Bulk-Aktionen für Patterns:
  - aktuelle Pattern-Zustände speichern
  - alle Patterns für Cycle aktivieren
  - alle Patterns für Cycle deaktivieren
  - alle Patterns invertieren
  - alle Patterns auf normal setzen
- Profile auf SD-Karte:
  - aktuelle Controller-Settings speichern
  - JSON-Profile auflisten
  - ausgewähltes Profil laden
  - geladenes Profil auf den Controller anwenden
  - ausgewählte Profile löschen
- Firmware-Bereich:
  - scannt `.uf2`-Dateien aus `/firmware/`
  - Zielauswahl für `RP2040` und `RP2350`
  - eigener grafischer Flash-Workflow
  - UF2-Validierung vor dem Flashen
  - Fortschrittsanzeige beim Kopieren auf USB Mass Storage

Der UF2-Mass-Storage-Flasher ist aktuell **experimental / work in progress**.
Er ist als Service- oder Recovery-Workflow gedacht, nicht als normaler
CLI-Befehl.

UI-Sounds sind im Code standardmäßig aktiviert und nutzen eingebettete bzw.
generierte PCM-Daten. Es werden keine Sounddateien auf der SD-Karte benötigt.

## Hardware-Anforderungen

- M5Stack Cardputer-Adv / StampS3
- microSD-Karte
- USB-C-Kabel und passendes USB-OTG-Setup
- NightKite-Multi-Controller, zum Beispiel auf Basis eines Pimoroni Pico LiPo 2 /
  RP2350
- UF2-Firmwaredateien für den Firmware-Flasher

Für UF2-Flashing muss der Controller manuell in den BOOTSEL- /
USB-Mass-Storage-Modus gebracht werden. Der aktuelle Code setzt keinen
automatischen `reboot_bootsel`-Befehl voraus.

## Software-Anforderungen

- PlatformIO
- VS Code mit PlatformIO-Erweiterung oder PlatformIO CLI
- Git
- USB-Treiber, falls das Betriebssystem sie benötigt

Die normale CLI-Konfiguration nutzt die vorhandene NightKite-USB-CLI. Dafür
sind keine Änderungen an der NightKite-Multi-Firmware nötig, sofern die
erwarteten CLI-Befehle bereitstehen.

## Bauen und Flashen von NightKite Link

Repository klonen:

```sh
git clone https://github.com/SunnyCodes86/nightkite-link.git
cd nightkite-link
```

Danach den Ordner in VS Code / PlatformIO öffnen oder direkt die PlatformIO CLI
verwenden.

Das konfigurierte PlatformIO-Environment heißt `cardputer`.

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
- müssen die Endung `.uf2` haben
- werden auf der Firmware-Card ausgewählt

Profile:

- liegen unter `/profiles/`
- verwenden `.json`
- neue Profile werden als `profile_001.json` bis `profile_999.json` benannt

## Profilformat

Profile werden als JSON geschrieben, aber ohne ArduinoJson verarbeitet. Das
Laden ist bewusst einfach gehalten und liest die Schlüssel, die der aktuelle
Code nutzt.

Aktuell gespeicherte Struktur. `profile_version: 2` ergänzt optionale
Firmware-4.0-Felder. Ältere Profile bleiben lesbar; fehlende Schlüssel behalten
den aktuellen Wert bzw. den Default.

```json
{
  "profile_version": 2,
  "project": "NightKite Link",
  "target": "NightKite Multi",
  "settings": {
    "device_name": "NK-Test",
    "brightness": 159,
    "strip_length": 50,
    "active_pattern": 7,
    "smoothing": 45,
    "accel_range": 4,
    "gyro_range": 500,
    "play_mode": "manual",
    "boot_mode": "last",
    "sync_enabled": false,
    "sync_group": 1,
    "sync_role": "standalone",
    "sync_master_uid": "",
    "sync_loss_behavior": "continue_local",
    "wireless_enabled": false,
    "wireless_profile": "balanced",
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

Beim Anwenden eines geladenen Profils auf einen Firmware-4.0/NK4-Controller
bevorzugt NightKite Link kompakte NK4-`set`-Befehle, inklusive `enabled_mask`
und `inverted_mask`. Im Legacy-Modus bleibt der Firmware-3.x-Ablauf erhalten:
einzelne Settings senden, Patterns per Listen aktivieren/deaktivieren, alle
Patterns zuerst auf normal setzen und danach die invertierte Pattern-Liste erneut
setzen.

## Bedienung

Die aktuelle Tastaturbehandlung verarbeitet diese Eingaben:

| Taste | Aktion |
| --- | --- |
| `A` / `D` | Vorherige / nächste Card |
| `W` / `S` | Wert editieren oder Auswahl bewegen |
| `Enter` | Anwenden, öffnen, bestätigen oder im Flash-Workflow fortfahren |
| `Backspace` / `DEL` | Zurück oder abbrechen, wo unterstützt |
| `Tab` | Nächste Card |
| `R` | Aktuelle Card bzw. Controllerdaten neu lesen, wo implementiert |
| `C` | Editierbares Feld wählen, Firmware-Ziel umschalten oder Pattern-Cycle toggeln |
| `I` | Pattern-Invert toggeln oder ausgewähltes Profil auf der Profiles-Card löschen |
| `,` / `<` | Vorherige Card |
| `/` / `?` | Nächste Card |
| `;` / `:` | Gleiche Richtung wie `W` |
| `.` / `>` | Gleiche Richtung wie `S` |

Während kritischer Firmware-Kopierzustände ist die normale Card-Navigation
gesperrt. Abbrechen ist nur in sicheren Flash-Zuständen möglich.

Editierbare Cards halten einen lokalen Draft, solange ein Feld pending ist.
Automatische Controller-Refreshes aktualisieren weiter den Controller-State,
überschreiben aber den aktiven Draft nicht. Pending-Felder sind mit `*`
markiert; `Enter` wendet sie an, `Backspace` / `DEL` verwirft die lokale
Änderung.

## UI-Konzept

NightKite Link nutzt ein Card-based Interface statt eines großen klassischen
Menüs, weil das Display nur 240 x 135 px groß ist. Jede Card steht für eine
Hauptaufgabe: Status/Device, Play Mode, Sync, Wireless-Diagnose, Brightness,
Config, Calibration, Active Pattern, Pattern-Liste, Bulk-Aktionen, Firmware oder
Profiles.

Die Statusleiste oben zeigt kompakt Transport/Protokoll (`USB LEG` oder
`USB NK4`), Controller-Name oder Short-ID, Play-/Rollen-Token, Controller-Akku
falls verfügbar und Cardputer-Akku. Der Firmware-Flasher verwendet eigene
Workflow-Screens für Bestätigung, BOOTSEL-Anweisung, Warten, Fortschritt, Reboot
und Fehler.

## Controller-Kommunikation

NightKite Link nutzt `USBHostSerial` im USB-Host-Modus, wenn
`NIGHTKITE_USB_HOST=1` aktiviert ist. Für Builds ohne USB-Host existiert ein
Debug-Serial-Transport.

Beim USB-Verbinden versucht Link zuerst Firmware 4.0/NK4:

1. `protocol machine` senden.
2. `NK4 seq=<id> cmd=hello client=nightkite-link proto_min=4 proto_max=4` senden.
3. Bei einer gültigen NK4-Antwort auf USB NK4 wechseln und `info`, `caps`,
   `status`, `get section=config`, `get section=play`, `get section=sync`,
   `get section=wireless` und `get section=patterns` abfragen.
4. Wenn NK4 in ein Timeout läuft, sauber auf die bestehende USB-Legacy-CLI
   zurückfallen.

Der NK4-Parser verarbeitet `ok`-, `err`- und `event`-Zeilen, gleicht `seq` ab,
toleriert unbekannte Keys und nutzt Timeouts, damit die UI nicht einfriert.

Im USB-NK4-Modus ist automatisches Polling bewusst leichtgewichtig: Link pollt
regelmäßig `status`, nachdem die UI kurz idle war. Vollständige Section-Reads
laufen nur nach Connect, manuellem Refresh und erfolgreichen Apply-Follow-ups.

Der Parser verarbeitet:

- `OK key=value ...`
- `ERR ...`
- `INFO ...`
- `[NightKite CLI] ...`
- `NK4 seq=<id> ok key=value ...`
- `NK4 seq=<id> err code=<code> msg=<message>`
- `NK4 event=<name> key=value ...`

Der Code aktualisiert Controllerdaten aus Schlüsseln wie:

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

Im NK4-Modus werden bestehende UI-Aktionen in NK4-Requests übersetzt, zum
Beispiel `cmd=set brightness=...`, `cmd=set play_mode=manual|autoplay|sync`,
`cmd=set sync_enabled=0|1`, `cmd=set sync_group=...`,
`cmd=set sync_role=standalone|master|follower`,
`cmd=set wireless_enabled=0|1`, `cmd=set wireless_profile=...`,
`cmd=set enabled_mask=...` und `cmd=set inverted_mask=...`.

Der BLE-NK4-Service aus Firmware 4.0 wird von dieser NightKite-Link-Version noch
nicht verwendet. USB bleibt der stabile Pfad. Link ist Konfigurator und
Diagnosegerät; es leitet keine Echtzeit-Sync-Beacons weiter und streamt keine
LED-Frames.

Bulk-Invert wird aktuell über kommaseparierte `invert_pattern`- bzw.
`normal_pattern`-Befehle umgesetzt. Im Code ist ein zukünftiger dedizierter
Befehl im Stil von `set all_patterns_invert` als TODO markiert, falls die
Controller-Firmware so etwas später anbietet.

## Zwei-Controller-Sync-Test

Für Firmware-4.0-Controller mit USB NK4 bietet die Sync-Test-Card einen
kompakten Setup- und Diagnoseablauf für die ersten Master/Follower-Beacon-Tests.
Sie ist nur Konfigurator und USB-Diagnoseansicht. Sie ist kein BLE-Client und
leitet keinen Echtzeit-Sync weiter.

Typischer Master-Ablauf:

1. Controller A per USB verbinden und `USB NK4` prüfen.
2. Sync Test öffnen.
3. Gruppe wählen, meist `Group 1`, und Wireless-Profil wählen, meist
   `balanced`.
4. `Configure Master` ausführen.
5. `Save` ausführen.

`Configure Master` stellt folgende Befehle in die Queue:

- `set name=NK-Master`
- `set play_mode=sync`
- `set sync_enabled=1 sync_group=<group> sync_role=master`
- `set wireless_enabled=1 wireless_profile=<profile>`

Typischer Follower-Ablauf:

1. Controller B per USB verbinden und `USB NK4` prüfen.
2. Sync Test öffnen.
3. Dieselbe Gruppe und dasselbe Wireless-Profil wie beim Master wählen.
4. `Configure Follower` ausführen.
5. `Save` ausführen.

`Configure Follower` stellt folgende Befehle in die Queue:

- `set name=NK-Follower`
- `set play_mode=sync`
- `set sync_enabled=1 sync_group=<group> sync_role=follower`
- `set wireless_enabled=1 wireless_profile=<profile>`

`Refresh Sync` fragt `get section=sync`, `sync_status`,
`get section=wireless` und `status` ab. Solange die Sync-Test-Card geöffnet ist,
pollt Link `sync_status` ungefähr alle 1,8 Sekunden und
`get section=wireless` ungefähr alle 5 Sekunden. Die bestehende
Dirty-/Draft-Logik verhindert weiterhin, dass aktive Eingaben überschrieben
werden.

Die Diagnosefelder sind für das Cardputer-Display bewusst kurz:

- `radio_mode`: erwartet `beacon_master` beim Master oder `beacon_follower` beim
  Follower, wenn Beacon-Sync aktiv ist.
- `beacon_tx_count`: gesendete Beacons; sollte beim Master steigen.
- `beacon_rx_count`: empfangene Beacons; sollte beim Follower steigen.
- `beacon_crc_errors`: fehlerhafte Beacons; sollte niedrig bleiben.
- `beacon_group_mismatch`: ignorierte Beacons mit anderer Gruppe.
- `beacon_age_ms`: Alter des letzten empfangenen Beacons, in der UI als `A...`.

Wenn `radio_mode=gatt` erscheint, ist ein BLE-GATT-Client mit dem Controller
verbunden und Beacon-Sync ist nicht aktiv. Den BLE-Client vor der Bewertung des
Beacon-Tests trennen. USB an NightKite Link darf für Konfiguration und Diagnose
verbunden bleiben.

## Firmware-Flasher

Der Firmware-Flasher arbeitet mit UF2-Dateien auf der SD-Karte und dem
USB-Mass-Storage-Modus des RP2040/RP2350-Controllers.

Aktueller Ablauf:

1. Eine `.uf2`-Firmwaredatei nach `/firmware/` auf der SD-Karte kopieren.
2. Firmware-Card öffnen.
3. UF2-Datei mit `W` / `S` auswählen.
4. Ziellabel mit `C` wählen (`RP2040` oder `RP2350`).
5. `Enter` drücken.
6. Ausgewählte Datei bestätigen.
7. Controller manuell in BOOTSEL bringen.
8. USB neu verbinden, sodass der Controller als USB-Mass-Storage-Gerät
   erscheint.
9. Mit `Enter` fortfahren.
10. NightKite Link wartet auf Mass Storage, mountet es unter `/usb` und kopiert
    die UF2-Datei als `/usb/FIRMWARE.UF2`.
11. Die UI zeigt Fortschritt, kopierte KB und Prozent.
12. Nach dem Kopieren wird das VFS unmounted und auf Reboot/Disconnect gewartet.
13. Erfolg oder Fehler wird auf einem eigenen Screen angezeigt.

UF2-Validierung:

- Datei existiert
- Dateigröße ist größer als null
- Dateigröße ist durch 512 teilbar
- Magic-Werte im ersten UF2-Block sind gültig

Family-ID-Erkennung ist vorhanden, aber eine harte zielabhängige
Family-ID-Prüfung ist im Code als TODO markiert.

Warnungen:

- Während des Kopierens nicht trennen.
- Nur UF2-Dateien verwenden, die zum Controller passen.
- Der Flasher ist experimental / work in progress.
- Der Flasher ist ein Service-/Recovery-Workflow und kein normaler
  NightKite-CLI-Befehl.

## Fehlerbehebung

### Cardputer wird nicht geflasht

- Prüfen, ob der richtige USB-Port in PlatformIO gewählt ist.
- `pio run -t upload` verwenden.
- Falls der Upload fehlschlägt, den Cardputer-Adv neu verbinden.

### Controller wird nicht erkannt

- USB-OTG-Kabel und Stromversorgung prüfen.
- Die Statusleiste zeigt den USB-Verbindungsstatus.
- Auf passenden Cards mit `R` neu lesen.

### `USB disconnected`

- Der USB-Host-Transport hat den Controller verloren.
- Controller neu verbinden.
- Ausstehende Kommandos werden vom Code gelöscht.

### `Controller timeout`

- USB kann physisch noch verbunden sein, aber die CLI hat innerhalb des
  konfigurierten Timeouts nicht geantwortet.
- Controller neu verbinden oder Refresh senden.

### SD card not ready

- Prüfen, ob die microSD-Karte steckt und mit einem von der Arduino-SD-Library
  unterstützten Dateisystem formatiert ist.
- Die App legt `/profiles/` und `/firmware/` an, wenn möglich.

### No UF2 file found

- `.uf2`-Dateien unter `/firmware/` ablegen.
- Firmware-Card mit `R` neu scannen.

### Invalid UF2

- Die ausgewählte Datei hat die grundlegende UF2-Validierung nicht bestanden.
- Prüfen, ob es eine echte UF2-Datei ist und keine umbenannte Binary.

### Mass Storage timeout

- Der Controller ist nicht rechtzeitig als USB-Mass-Storage-Gerät erschienen.
- RP2040/RP2350 manuell in BOOTSEL bringen und USB neu verbinden.

### Mount failed

- Das Gerät wurde erkannt, aber FAT/VFS-Mount ist fehlgeschlagen.
- Controller erneut in BOOTSEL verbinden und noch einmal versuchen.

### Write failed

- Das Kopieren der UF2-Datei ist fehlgeschlagen.
- Während des Kopierens nicht trennen. Mit bekannter guter UF2-Datei und Kabel
  erneut versuchen.

### Nach dem Firmware-Flash

- Controller rebooten lassen.
- Danach wieder normal verbinden, damit die NightKite-USB-CLI verfügbar ist.

## Entwicklungsnotizen

- Das Projekt ist bewusst kompakt und auf ein kleines Handheld-Display
  zugeschnitten.
- Die UI soll nicht blockieren; `M5Cardputer.update()` muss regelmäßig laufen.
- USB-CLI-Kommunikation und UF2-Mass-Storage-Flasher sollen klar getrennt
  bleiben.
- Die aktuelle Command-Queue sendet CLI-Befehle mit kurzem Abstand.
- Der Firmware-Flasher pausiert normales CLI-Polling, solange er aktiv ist.
- `scripts/patch_m5cardputer.py` patcht beim Build bei Bedarf die
  M5Cardputer-Abhängigkeit und fügt dort einen fehlenden GPIO-Include hinzu.

Mögliche spätere Erweiterungen:

- Robusteres Verhalten des UF2-Flashers in Randfällen
- Optionales `reboot_bootsel`, falls NightKite Multi das später anbietet
- Optional später BIN/ELF/Picoboot; aktuell kein Kernziel
- Bessere Profilvalidierung und robusteres Profilparsing
- Release-Workflow mit fertigen Firmware-Binaries

## Roadmap

- UI-Polish für bessere Lesbarkeit auf dem kleinen Display
- Robustere Profilverwaltung
- Firmware-Flasher auf echten RP2040/RP2350-Boards weiter stabilisieren
- NightKite-CLI-Kompatibilität formaler dokumentieren
- Release-Workflow mit fertigen Builds
- Optional automatische Firmware-Versionserkennung
- Optional weitere Controller-Ziele

## Lizenz

Es wurde noch keine Lizenzdatei hinzugefügt.

Die vendored Espressif-Komponente `usb_host_msc` enthält ihre eigene Lizenzdatei
unter `lib/usb_host_msc/LICENCE`.

## Ressourcen

- NightKite Multi: https://github.com/SunnyCodes86/nightkite-multi
- NightKite Link: https://github.com/SunnyCodes86/nightkite-link
- M5Cardputer Library: https://github.com/m5stack/M5Cardputer
- Cardputer-Adv Documentation: https://docs.m5stack.com/en/core/Cardputer-Adv
- ESP USB MSC Host Component: https://components.espressif.com/components/espressif/usb_host_msc
- ESP-IDF USB Host: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html
- PlatformIO: https://platformio.org/
