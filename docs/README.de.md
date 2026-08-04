# NightKite Link Dokumentation

## Projektübersicht

NightKite Link ist ein kompaktes Gerät zum Konfigurieren und Warten von
NightKite-Multi-Controllern. Das etablierte M5Stack-Cardputer-Adv-/StampS3-Target
bleibt das vollständig funktionsfähige Handheld. Der M5Stack Tab5 ist ein zweites
Target mit eigener 1280-x-720-Touch-Oberfläche. Sie unterstützt USB- und
BLE-NK4-Controllerauswahl, Konfiguration, Profile, Pattern, Wiedergabe, Sync,
Audio Beacon, Diagnose, SD und UF2-Service.

Ziel ist, einen NightKite-Controller ohne Laptop konfigurieren und warten zu
können. NightKite Link nutzt dafür soweit möglich die vorhandene
NightKite-USB-CLI. Für den normalen Konfigurationsablauf sind keine Änderungen
an der NightKite-Multi-Firmware erforderlich, solange die erwarteten CLI-Befehle
vorhanden sind.

Die Cardputer-Oberfläche bleibt wegen des kleinen Displays kartenbasiert. Die
Tab5-Oberfläche ist getrennt, damit Touch und die größere Fläche ohne
Fallunterscheidungen in den Cardputer-Views genutzt werden können.

## Multi-Target-Architektur

- `src/main.cpp` enthält Cardputer-UI und Hardwareintegration.
- `src/targets/tab5/main.cpp` enthält Tab5-Touch-UI und Hardwareintegration.
- `include/` und die portablen Quellen in `src/` werden geteilt: Aufbau von
  NK4-/Legacy-Kommandos und UUIDs, Profile-Codec, Queue-/Session-Regeln,
  Controller-Zustandsparser, Audio-Sync-DSP, Beacon-Codec und UF2-Validierung.
- Beim Tab5 läuft Arduino als ESP-IDF-Komponente. Die lokale
  `sdkconfig.defaults` aktiviert P4-PSRAM und ESP-Hosted/NimBLE über den C6,
  ohne die Cardputer-SDK-Konfiguration zu verändern. `dependencies.lock` fixiert
  die aufgelösten IDF-Komponenten.

Der P4 hat keinen eigenen Bluetooth-Controller. BLE-GATT-Central, Scanning und
Legacy-Advertising laufen deshalb über den C6 per ESP-Hosted SDIO/VHCI. Der
gewählte Arduino-/ESP-IDF-Toolchain unterstützt diesen Pfad und baut ihn in das
Tab5-Target ein. NightKite Link verwendet kein WLAN. Meldet der C6 die Version
`0.0.0`, bricht Link vor der NimBLE-Initialisierung kontrolliert ab. Das
M5Stack-Werksimage stellt WLAN/SDIO wieder her, aktiviert aber nicht den
Hosted-BLE-Pfad. Es dient als Recovery-Basis; anschließend muss über denselben
internen C6-Downloadanschluss und USB-TTL-Adapter ein zur im Tab5-Build fixierten
ESP-Hosted-Version passendes Coprozessor-Image installiert werden. Link lehnt
Versionen älter als 2.6 ab. Zugang und Verdrahtung des internen Anschlusses
beschreibt die
[M5Stack-Anleitung](https://docs.m5stack.com/en/guide/restore_factory/m5tab5_c6_wifi).

## Funktionen

Im aktuellen Code vorhanden:

- Cardputer-Akkuanzeige inklusive Ladestatus
- PCM-basierte Startup-, Tasten-, Navigations-, Confirm-, Cancel-, Success- und Error-Sounds
- lokale Link-Options-Card für Sound, Lautstärke, Tasten-/Startton und Displayhelligkeit
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
- Sync-Setup-Test-Card zum Vorbereiten von Firmware-4.0-Master/Follower-Beacon-Tests
- Experimentelle Audio-Beacon-Card mit V1/V2-Manual- und Cardputer-Mikrofon-Audio-Sync-Modi
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

- M5Stack Cardputer-Adv / StampS3 oder M5Stack Tab5
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

Die experimentelle Audio-Beacon-Card sendet als Beacon Master
NightKite-Sync-Beacon-V1- oder
V2-BLE-Manufacturer-Data direkt vom Cardputer als nicht verbindbares
Advertising. V1 funktioniert weiterhin mit unveränderter Firmware 4.0. Der
V2-Audio-Testmodus benötigt NightKite-Multi-Firmware mit V2-Empfang
(`4cfa6a0` oder neuer). Die Audio-Sync-Patterns 23 bis 27 benötigen
NightKite Multi ab `0f03e9e`. Dieser Modus ist von der BLE-GATT-Konfiguration getrennt
und hält während des Broadcasts keine dauerhafte GATT-Verbindung offen. Verfügbar
sind `V1 Manual`, `V2 Manual`, `V2 Mic Energy` und `V2 Mic Full`. Mic Energy
steuert Energy und Confidence per Mikrofon und behält die manuellen Bandwerte.
Mic Full steuert zusätzlich Bass, Mid und Treble und aktiviert eine einfache
Beat-/BPM-/Phasenerkennung.

Zum Testen werden NightKite-Multi-Controller als Follower vorbereitet:
`play_mode=sync`, `sync_enabled=1`, `sync_role=follower`, passende
`sync_group` 1-4 und `wireless_enabled=1`. Danach auf der Audio-Beacon-Card
dieselbe Gruppe sowie Pattern, Helligkeit und BPM wählen und mit `Enter` den
Broadcast starten oder stoppen. V1 wählt den etablierten Pfad, V2 Manual die
manuellen Testwerte und die Mic-Modi die Live-Analyse. In Mic-Modi sind
Sensitivity, Noise Gate, Smoothing, Beat Detect und Pause einstellbar.
Tap-Tempo bleibt der Fallback, wenn die Beat-Erkennung deaktiviert oder unsicher
ist.

Die Aufnahme läuft asynchron mit 8 kHz, mono und 256 Samples bzw. 32 ms pro
Frame. Der DSP entfernt DC, verfolgt RMS, Peak und einen langsamen Noise Floor,
wendet Gate und Attack/Release-Glättung an und normalisiert Energy. Mic Full
nutzt eine kleine Goertzel-Filterbank für ungefähr 60-250 Hz, 250-2000 Hz und
2000-3400 Hz. Die obere Grenze folgt dem Nyquist-Limit von 4 kHz. UI-Sounds
werden während aktiver Mikrofonaufnahme unterdrückt. V2 bleibt bei 22 Byte
Payload und 29 Byte Legacy Advertising ohne Local Name oder Service Data.

Der Cardputer-Katalog umfasst jetzt alle 27 Firmware-Patterns. Neu sind
`audio_pulse_angle_color` (23), `audio_spectrum_ribbon` (24),
`audio_beat_ripples` (25), `audio_band_comets` (26) und `audio_beat_mosaic`
(27). Die IDs sind in V1 Manual, V2 Manual, V2 Mic Energy und V2 Mic Full
auswählbar. Firmware-4.0-Controller melden ihre Patternanzahl über NK4; im
Firmware-3.x-Legacy-Modus bleiben Konfiguration und Bulk-Aktionen auf 22
Patterns begrenzt.

Controller-Diagnose:

```text
NK4 seq=10 cmd=get section=sync
NK4 seq=20 cmd=audio_sync_status
```

Hardwaretest:

1. Controller mit `play_mode=sync`, `sync_enabled=1`, `sync_role=follower`,
   passender `sync_group` und `wireless_enabled=1` konfigurieren.
2. `V2 Mic Full` starten, Musik oder einen regelmäßigen Puls am Cardputer
   abspielen und die Patterns 23, 24, 25, 26 und 27 nacheinander auswählen.
3. Beide obigen Kommandos für jedes Pattern über Controller-USB ausführen.
4. Erwartet werden `sync_locked=1`, `local_pattern` beziehungsweise
   `sync_pattern` 23 bis 27, `audio_valid=1`, `last_beacon_version=2`,
   steigendes `scan_decode_v2`, reagierende Energy-/Bandwerte, steigende
   Confidence bei stabilem Puls, plausibles `audio_beat_ms` und
   `scan_crc_fail=0`.

## Bauen und Flashen von NightKite Link

Repository klonen:

```sh
git clone https://github.com/SunnyCodes86/nightkite-link.git
cd nightkite-link
```

Danach den Ordner in VS Code / PlatformIO öffnen oder direkt die PlatformIO CLI
verwenden.

Die PlatformIO-Environments heißen `cardputer` und `tab5`; der Default-Build
baut beide.

Build:

```sh
pio run
```

Upload:

```sh
pio run -e cardputer -t upload
pio run -e tab5 -t upload
```

Serial Monitor:

```sh
pio device monitor -e cardputer
pio device monitor -e tab5
```

Aktuelle Targets aus `platformio.ini`:

- Plattform: pioarduino `55.03.37` für beide Targets
- Cardputer: `m5stack-stamps3`, Arduino
- Tab5: `m5stack-tab5-p4`, Arduino als ESP-IDF-Komponente
- monitor speed: `115200`

## Tab5-Arbeitsablauf und Hardwarediagnose

Eine feste Navigationsleiste gliedert den Touch-Ablauf in `Connect`, `Control`,
`Patterns`, `Playback`, `Sync`, `Audio`, `Profiles`, `Controller`, `Service` und
`Firmware`. `Connect` wählt USB oder startet einen BLE-Scan; gefundene
BLE-Controller werden direkt angetippt. Nach NK4-Handshake und vollständigem
Initial-Refresh stehen diese Arbeitsabläufe bereit:

- Pattern/Helligkeit, Wiedergabe, Pattern-Maske und Bulk-Bearbeitung mit
  getrennten Entwürfen und explizitem `Apply & Save`
- Sync-Rolle, Gruppe, Master-UID, Verlustverhalten sowie Funkstatus und
  Funkprofil
- Audio Beacon V1/V2 manuell oder mikrofongeführt mit Tap-Tempo, manuellen
  V2-Werten für Energy/Bänder/Confidence, Beat-, BPM-, Sequenz- und Advertising-Status
- Profile erstellen, überschreiben, laden, live anwenden, umbenennen und nach
  Bestätigung löschen; ein Profil-Apply speichert bewusst nicht automatisch
- Controllername, Striplänge, Smoothing, Sensorbereiche, Boot-Kalibrierung,
  bestätigte Werkseinstellungen und separates persistentes Speichern
- Service-Tabs für USB-only Schnell-/Präzisionskalibrierung, Timing-/Sensor-
  Refresh, abgesichertes NK4-Terminal, Live-Sync-/Funk-/BLE-Diagnosen,
  SD-Prüfung und alle persistenten lokalen Sound-, Lautstärke-, Touch-/Startton-
  und Displayoptionen
- vollständige UF2-Prüfung, RP2040-/RP2350-Zielwahl, Bestätigung,
  Byte-/Prozent-Fortschrittsanzeige und Abbruch im Firmware-Workflow

`Reload` fragt vor dem Verwerfen lokaler Entwürfe nach; auch das bewusst nicht
persistente Live-Anwenden eines Profils verlangt eine Bestätigung. `Disconnect` leert Session,
Queue und abgeleiteten Zustand. Busy-, Timeout-, Protokoll-, Queue- und
Disconnect-Fehler bleiben sichtbar und führen nicht zu einem fälschlich
gemeldeten Speichern. Das Terminal akzeptiert eine NK4-`cmd=`-Zeile; `save` und
`defaults` sind dort gesperrt und bleiben den bestätigten UI-Abläufen vorbehalten.

Über den seriellen Monitor stehen `status`, `reload`, `sd`, `audio`, `usb`, `gatt`,
`beacon` und `all` bereit. Display und gemeinsamer Kern werden beim Start
geprüft. `reload` nutzt denselben sicheren Queue-Refresh wie die Touch-Taste,
`sd` mountet die Karte per 4-Bit-SDMMC, `audio` spielt einen gut
hörbaren 4-kHz-Testton und prüft anschließend einen Mikrofonpegel, `usb` wartet auf einen NK4-
Controller, `gatt` scannt, verbindet den ersten Treffer und prüft Read/Write/
Notify bis zum vollständigen Initial-Refresh. `beacon` sendet drei Sekunden
lang ein gültiges NightKite-V1-Sync-Beacon für Gruppe 1 und darf deshalb nur
mit dem vorgesehenen Follower in Reichweite ausgeführt werden. Eine aktive
GATT-Sitzung blockiert Advertising; ein GATT-Scan stoppt einen laufenden
Diagnose-Advertiser. USB kann während des Advertisings verbunden bleiben.
`all` startet nur die lokalen SD-/Audio-Prüfungen und den USB-Pfad; die
Funkprüfungen bleiben wegen ihrer Transportumschaltung bewusst einzeln.

Praktisch bestätigt sind ST7121-Display mit 1280 x 720, Touch bis zu allen
Rändern, 4-Bit-SDMMC, Lautsprecher und Mikrofon, USB-NK4 einschließlich
Schreiben/Speichern/Neuladen und Wiederverbinden sowie ESP-Hosted 2.12.11 mit
BLE-Scan, GATT-Verbindung, NK4 Read/Write/Notify und Beacon-Empfang durch einen
Controller. Ein 10:21-minütiger GATT-Lauf mit parallelem Controller-USB
absolvierte elf vollständige Refresh-Zyklen ohne Timeout oder Reconnect. In
einem weiteren Lauf blieben 16 Beacon-Fenster und 16 parallele USB-Refreshes
fehlerfrei; der Controller dekodierte 170 Beacons ohne Decode-, CRC- oder
Gruppenfehler. Der Controller-Hostpfad des Tab5 ist physisch der USB-A-Anschluss.
USB-C ist mit festen Device-Role-CC-Widerständen an das getrennte USB-Device-
Datenpaar geführt und kann einen NightKite-Controller nicht direkt hosten.
Die dauerhaft sichtbare Kopfzeile zeigt den Tab5-Akkustand samt aktivem
Ladezustand und den vom verbundenen Controller gemeldeten Akkustand.
Noch offen sind die zweite unterstützte Display-Controller-Revision ST7123 und
das reale UF2-Schreiben einschließlich BOOTSEL-Zielprüfung, Disconnect und
Reboot auf jeweils einem RP2040- und RP2350-Gerät. Parser, Family-Validierung und
Zielkonflikte sind automatisiert geprüft, bestätigen aber kein reales Flashen.
Beim physischen USB-Abziehen meldet der ESP-IDF-Endpoint-
Cleanup nach dem bereits erkannten `DEV_GONE` zweimal `ESP_ERR_INVALID_STATE`.
Die Anwendung wechselt dabei korrekt und ohne Phantom-Verbindung in den
wartenden Zustand; die rein kosmetische Meldung wird nicht durch einen
riskanten Framework-Patch unterdrückt.

Gegenüber dem Cardputer bleiben bewusst wenige Unterschiede: Die Tab5-
Controlleroberfläche setzt Firmware 4.x/NK4 voraus; der Firmware-3.x-Legacy-
USB-Pfad bleibt unverändert im Cardputer. Kompakte Sync-Test-Shortcuts werden
nicht 1:1 kopiert, weil dieselben Aktionen in den größeren Sync-, Control-,
Playback- und Diagnoseabläufen liegen. Cardputer-Tastatur-/PCM-Feedback wird
auf Tab5 durch Touch-Töne mit denselben persistenten Soundregeln abgebildet.
Die vollständige Zuordnung steht in der
[Cardputer-/Tab5-Funktionsmatrix](TAB5_FUNCTION_MATRIX.md). Die erweiterten
Tab5-Abläufe benötigen noch den folgenden gebündelten Hardwarelauf.

### Gebündelter Hardware-Abschlusstest

Die neue Gesamtoberfläche wird nicht kleinteilig nach jeder Funktion geflasht.
Der abschließende Hardwarelauf bündelt stattdessen:

1. Finalen Tab5-Build flashen; Boot, beide Display-Controller-Revisionen soweit
   vorhanden, vollständige Touch-Ausrichtung, Navigation, persistente Sound-/
   Displayoptionen, Start-/Touch-/Statustöne und beide Akkuanzeigen prüfen.
2. Über USB NK4 verbinden; Initial-Refresh, Control, Playback, Pattern-Maske,
   Bulk, Controller-, Kalibrierungs-, Terminal- und Sync-Abläufe jeweils
   anwenden, speichern, bestätigt mit offenen Entwürfen neu laden und nach
   physischem Disconnect wieder verbinden. Queue-/Busy- und kontrollierte
   Fehleranzeigen dabei beobachten.
3. Profil auf SD erstellen, überschreiben, laden, live anwenden, anschließend
   explizit speichern, umbenennen und löschen. Zusätzlich defektes, zu großes
   und schreibunterbrochenes Profil sowie `.bak`-Wiederherstellung prüfen.
4. Dieselben repräsentativen Read-/Write-/Save-Abläufe über BLE inklusive Scan,
   Connect, Read, Write, Notify, automatischem Diagnose-Polling, langer Sync-
   Antwort und sauberem Disconnect ausführen.
5. GATT trennen, Controller als Follower konfigurieren und Audio Beacon V1/V2,
   Manual einschließlich Tap-Tempo und V2-Bändern/Confidence, Mic Energy und
   Mic Full prüfen. Parallel USB-Refreshes ausführen und
   Lock-, Decode-, CRC-, Audio- und Advertising-Zähler kontrollieren; danach
   Lautsprecher-/Mikrofondiagnose und Mic-Pause prüfen.
6. Kalibrierung und abgesichertes Terminal prüfen. Anschließend passende und
   falsche RP2040-/RP2350-UF2-Dateien validieren, falsches BOOTSEL-Ziel ablehnen
   und je ein echtes Flashen einschließlich Fortschritt, Disconnect/Reboot und
   kontrolliertem Abbruch durchführen.

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
- werden auf `Firmware Update` ausgewählt

Profile:

- liegen unter `/profiles/`
- verwenden `.json`
- neue Profile werden als `profile_001.json` bis `profile_999.json` benannt

## Profilformat

Profile werden mit einem größenbegrenzten JSON-Decoder geschrieben und gelesen.
Fehlerhafte, abgeschnittene, zu große, nicht unterstützte oder typ-/bereichsfalsche
Profile werden abgewiesen, bevor sie den geladenen Profilzustand ersetzen.

Aktuell gespeicherte Struktur. `profile_version: 2` ergänzt optionale
Firmware-4.0-Felder. Ältere Profile bleiben lesbar; fehlende Schlüssel behalten
den aktuellen Wert bzw. den Default.
Fehlen die kompakten Pattern-Masken, dienen `patterns[].cycle_enabled` und
`patterns[].inverted` als Kompatibilitäts-Fallback. Beim Speichern wird zuerst
eine temporäre Datei vollständig geschrieben und geprüft. Ein unterbrochener
Overwrite behält eine wiederherstellbare `.bak`-Datei, die beim nächsten
Profil-Scan zurückgespielt wird.

Ein geladenes Profil kann erst angewendet werden, nachdem die aktuelle USB- oder
BLE-Controller-Session ihren initialen Identity-, Status-, Config- und
Play-State-Refresh abgeschlossen hat. Ein Disconnect im Bestätigungsdialog
bricht das Anwenden ab.

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
    "enabled_pattern_mask": 134217727,
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
setzen. Profile mit IDs 1 bis 22 bleiben unverändert lesbar. Beim Anwenden auf
einen älteren Controller werden Masken und aktive Pattern defensiv auf dessen
gemeldeten beziehungsweise Legacy-Patternbereich begrenzt.

## Bedienung

Die aktuelle Tastaturbehandlung verarbeitet diese Eingaben:

| Taste | Aktion |
| --- | --- |
| Pfeil links / rechts | Vorherige / nächste Card |
| Pfeil hoch / runter | Wert editieren oder Auswahl bewegen |
| `A` / `D`, `W` / `S` | Fallbacks für die entsprechenden Pfeiltasten |
| `Enter` | Anwenden, öffnen, bestätigen oder im Flash-Workflow fortfahren |
| `Backspace` / `DEL` | Zurück oder abbrechen, wo unterstützt |
| `Tab` | Nächste Card |
| `R` | Aktuelle Card bzw. Controllerdaten neu lesen, wo implementiert |
| `T` | Tap-Tempo auf der Audio-Beacon-Card |
| `C` | Editierbares Feld wählen, Firmware-Ziel umschalten oder Pattern-Cycle toggeln |
| `I` | Pattern-Invert toggeln oder ausgewähltes Profil auf der Profiles-Card löschen |

Die physischen Cardputer-Pfeiltasten liefern die von der Tastaturbibliothek
verwendeten Satzzeichen-Aliase. Footer zeigen deshalb zuerst kompakte
ASCII-Pfeile und nur die wichtigsten vollständigen Hinweise, die in 240 px passen.

Während kritischer Firmware-Kopierzustände ist die normale Card-Navigation
gesperrt. Abbrechen ist nur in sicheren Flash-Zuständen möglich.

Editierbare Cards halten einen lokalen Draft, solange ein Feld pending ist.
Automatische Controller-Refreshes aktualisieren weiter den Controller-State,
überschreiben aber den aktiven Draft nicht. Pending-Felder sind mit `*`
markiert; `Enter` wendet sie an, `Backspace` / `DEL` verwirft die lokale
Änderung.

## UI-Konzept

NightKite Link nutzt ein Card-based Interface statt eines großen klassischen
Menüs, weil das Display nur 240 x 135 px groß ist. Die flache Reihenfolge stellt
Live-Funktionen nach vorne und Diagnose-/Servicefunktionen nach hinten:
`Status`, `Pattern Live`, `Brightness`, `Play`, `Audio Beacon`, `Patterns`,
`Pattern Bulk`, `Profiles`, `Link Options`, `Controller`, `BLE Connect`, `Controller Setup`,
`Controller Sync`, `Controller Radio`, `Motion Service`, `Sync Diagnostics`,
`Sync Setup Test` und `Firmware Update`.

Auf `Patterns` wechselt Pfeil hoch/runter das Controller-Pattern als Live-Vorschau.
`Enter` öffnet die Detailansicht für Cycle und Invert.

Die `Link Options`-Card ändert ausschließlich Cardputer/NightKite Link. Mit `C`
wählt man Sound an/aus, Lautstärke, Tastentöne, Startton, Displayhelligkeit oder
den lokalen Reset; Pfeil hoch/runter wendet den gewählten Wert sofort an. Reset
stellt nur diese Link-Standardwerte wieder her. Es wird kein Controller-Befehl
gesendet, Controller-Save/-Defaults werden nicht aufgerufen und SD-Profile
bleiben unverändert.

Die lokalen Einstellungen liegen als versionierter Datensatz mit Prüfsumme im
ESP32-Preferences-/NVS-Namespace `nk-link`. Schnelle Änderungen werden
zusammengefasst und nach einer Sekunde geschrieben; unveränderte Werte werden
nicht erneut geschrieben. Beim Start werden die Werte validiert. Ein ungültiger
Datensatz fällt auf die Standardwerte zurück und wird repariert. Standard sind
Sound an, Lautstärke 210, Tasten- und Startton an sowie Displayhelligkeit 96.

Die Statusleiste oben zeigt kompakt Transport/Protokoll (`USB LEG` oder
`USB NK4`), kompakte Queue (`Q0`...`Q9+` oder `Q!`), Play-/Rollen-Token, Controller-Akku
falls verfügbar und Cardputer-Akku. Der Firmware-Flasher verwendet eigene
Workflow-Screens für Bestätigung, BOOTSEL-Anweisung, Warten, Fortschritt, Reboot
und Fehler.

Die Controller-Card zeigt `Cfg repaired`, wenn Firmware 4.x eine erfolgreiche
Reparatur der persistenten Konfiguration meldet. Akkuwarnzustände des Controllers
(`LOW`, `CRIT`, `CUT`, `EMPTY`) haben Vorrang vor der geglätteten Spannungs-/
Prozentanzeige. Ein Schutzübergang bleibt dadurch auch während der
Anzeigehysterese sichtbar.

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

Firmware 4.x kann BLE-NK4-Zeilen mit bis zu 4094 Zeichen liefern. Link übernimmt
die vollständige Zeile und setzt beliebig aufgeteilte TX-Notify-Chunks bis zum
abschließenden Newline wieder zusammen. Overflow-Fehler mit zurückgespiegelter
Sequenz werden wie jede passende `err`-Antwort verarbeitet; der wartende Befehl
schlägt dadurch sofort fehl statt erst in ein Timeout zu laufen.

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

Bei Firmware 4.x werden `calibrate quick` und `calibrate precise` über USB auf
`NK4 cmd=calibrate mode=quick|precise` abgebildet. Link hält die Anfrage bis zu
zehn Minuten offen, da die präzise Kalibrierung absichtlich langsam ist. Über
BLE wird diese blockierende Wartungsfunktion nicht angeboten; Link weist auf
die notwendige USB-Verbindung hin.

Im NK4-Modus werden bestehende UI-Aktionen in NK4-Requests übersetzt, zum
Beispiel `cmd=set brightness=...`, `cmd=set play_mode=manual|autoplay|sync`,
`cmd=set sync_enabled=0|1`, `cmd=set sync_group=...`,
`cmd=set sync_role=standalone|master|follower`,
`cmd=set wireless_enabled=0|1`, `cmd=set wireless_profile=...`,
`cmd=set enabled_mask=...` und `cmd=set inverted_mask=...`.

Der BLE-NK4-Service aus Firmware 4.0 kann experimentell über die BLE-Connect-Card
verwendet werden. NightKite Link scannt nach `NK-...`-Geräten oder der
NightKite-Service-UUID, verbindet genau einen Controller und nutzt denselben
NK4-Parser wie USB. Der Scan läuft im Hintergrund, sodass Tastatur, Audio und UI
weiterlaufen; Verbindungsversuche sind zeitlich begrenzt und können nach einem
Fehler wiederholt werden. TX-Notify-Chunks werden bis zum Zeilenende `\n`
zusammengesetzt. USB bleibt der stabile empfohlene Pfad. Link ist Konfigurator
und Diagnosegerät; es leitet keine Echtzeit-Sync-Beacons weiter und streamt keine
LED-Frames.

Die aktuelle Controller-Firmware sendet die Service-UUID im primären
Advertising-Paket und den `NK-...`-Namen in der Scan Response. Link scannt aktiv
und akzeptiert beide Merkmale, daher bleibt diese Aufteilung auffindbar. Ein
Sync-Master unterdrückt sein verbindbares GATT-Advertising, solange er den Funk
besitzt; vor einem BLE-NK4-Scan daher die Verbindung trennen bzw. den
Master-Beacon-Modus verlassen.

Bulk-Invert wird aktuell über kommaseparierte `invert_pattern`- bzw.
`normal_pattern`-Befehle umgesetzt. Im Code ist ein zukünftiger dedizierter
Befehl im Stil von `set all_patterns_invert` als TODO markiert, falls die
Controller-Firmware so etwas später anbietet.

## Zwei-Controller-Sync-Setup-Test

Für Firmware-4.0-Controller mit USB NK4 bietet die Sync-Setup-Test-Card einen
kompakten Setup- und Diagnoseablauf für die ersten Master/Follower-Beacon-Tests.
Sie ist nur Konfigurator und Diagnoseansicht. BLE NK4 kann zur Konfiguration
genutzt werden, ist aber kein Echtzeit-Sync-Pfad und leitet keinen Sync weiter.

Typischer Master-Ablauf:

1. Controller A per USB verbinden und `USB NK4` prüfen.
2. Sync Setup Test öffnen.
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
2. Sync Setup Test öffnen.
3. Dieselbe Gruppe und dasselbe Wireless-Profil wie beim Master wählen.
4. `Configure Follower` ausführen.
5. `Save` ausführen.

`Configure Follower` stellt folgende Befehle in die Queue:

- `set name=NK-Follower`
- `set play_mode=sync`
- `set sync_enabled=1 sync_group=<group> sync_role=follower`
- `set wireless_enabled=1 wireless_profile=<profile>`

`Refresh Sync` fragt `get section=sync`, `sync_status`,
`get section=wireless` und `status` ab. Solange die Sync-Setup-Test-Card geöffnet ist,
pollt Link `sync_status` ungefähr alle 1,8 Sekunden und
`get section=wireless` ungefähr alle 5 Sekunden. Die bestehende
Dirty-/Draft-Logik verhindert weiterhin, dass aktive Eingaben überschrieben
werden.

Die Pattern-Liste zeigt bei Firmware 4.0 zusätzlich eine kompakte
Pattern-Klassifizierung:

- `S`: sync-ready
- `P`: partial-sync
- `L`: lokal/reaktiv
- `?`: Klassifizierung nicht bekannt

Die separate Sync-Diagnostics-Card zeigt PatternClock- und Apply-Diagnosewerte wie
`drift_ms`, `phase_ms`, `beacon_phase_ms`, `last_beacon_seq`,
`last_applied_seq`, `sync_apply_count`, `sync_apply_skipped`,
`sync_apply_reason`, `last_pattern_change_latency_ms`, `sync_ready_pattern`,
`partial_sync_pattern`, `sync_autoplay`, `master_autoplay` und
`autoplay_next_ms`.

Damit ist sichtbar, ob Master-Autoplay im Sync-Modus aktiv ist und ob ein
Follower Beacons tatsächlich anwendet. USB NK4 bleibt der stabile Diagnosepfad;
BLE NK4 ist als experimenteller Konfigurations- und Diagnosepfad verfügbar.

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

## Speichern und Werkreset

Live-Änderungen wie Brightness oder aktives Pattern werden sofort an den
Controller gesendet, sind aber erst nach `save` persistent. Auf der Controller-Card
ist `S save` die klare Persistenz-Aktion.

Pattern-Änderungen bleiben nach erfolgreichem Live-Befehl als `UNSAVED`
markiert. Erst die bestätigte persistente `save`-Antwort des Controllers löscht
die Markierung. Nach einem fehlgeschlagenen/abgelaufenen Pattern-Befehl, einem
teilweise fehlgeschlagenen Batch oder einer erst nach `save` eingereihten
Änderung bleibt `UNSAVED` auch bei einer späteren `save ok`-Antwort aktiv.

Schnelle Live-Änderungen an Brightness oder aktivem Pattern ersetzen jeweils
einen älteren noch nicht gesendeten Befehl desselben Typs. Dabei wird keine
andere Benutzerbefehls-Grenze überschritten. Die Queue hält höchstens 64
wartende Befehle; Hintergrund-Refreshes werden zuerst zusammengefasst oder
verworfen. Passt ein Benutzerbefehl trotzdem nicht, erscheint
`Command queue full`. NK4 meldet den Erfolg einer Mehrfachoperation nur, wenn
alle Benutzerbefehle erfolgreich waren. Legacy-Batches werden lediglich als
gesendet gemeldet, weil Legacy keine sequenzbasierte Batch-Bestätigung besitzt.

`C reset USB` setzt nur den Link-seitigen USB-/Protokollzustand zurück. Das ist
kein Controller-Werkreset.

`F defaults` öffnet eine Bestätigung für Controller-Werkseinstellungen. Nach
Bestätigung sendet Link im USB-NK4-Modus:

- `defaults confirm=1`
- `save`
- `info`, `status`, `get section=config`, `get section=play`,
  `get section=sync`, `get section=wireless`, `get section=patterns`

Persistente Firmware-4.0-Felder sind: Gerätename, Brightness, aktives Pattern,
Strip Length, Smoothing, Accel-/Gyro-Range, Boot Calibration, Autoplay an/aus,
Autoplay-Intervall, Play Mode, Boot Mode, Enabled-/Inverted-Masks, Sync Enabled,
Sync Group, Sync Role, Sync Master UID, Sync Loss Behavior, Wireless Enabled und
Wireless Profile.

Nicht persistent sind Laufzeitdiagnosen wie PatternClock-Phase,
Beacon-Zähler/-Alter, Lock-State, Apply-Zähler, Apply-Reason, Pattern-Latenz,
Battery-Status und Verbindungszustände.

## Firmware-Flasher

Der Firmware-Flasher arbeitet mit UF2-Dateien auf der SD-Karte und dem
USB-Mass-Storage-Modus des RP2040/RP2350-Controllers.

Aktueller Ablauf:

1. Eine `.uf2`-Firmwaredatei nach `/firmware/` auf der SD-Karte kopieren.
2. Firmware Update öffnen.
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
- Magic-Werte, Payload-Größe, Nummerierung und Blockanzahl jedes UF2-Blocks sind gültig
- jede angegebene UF2-Family passt zum gewählten RP2040- oder RP2350-Ziel
- Dateien ohne Family-ID werden abgelehnt, weil sie nicht sicher zugeordnet werden können
- das verbundene BOOTSEL-Gerät hat vor dem ersten Schreibzugriff Raspberry Pis USB-VID
  und die erwartete RP2040- oder RP2350-Boot-PID
- alle Schreib-, VFS-Flush-/Close- und Unmount-Vorgänge müssen erfolgreich sein

Erfolg wird erst angezeigt, wenn die komplette Datei übertragen wurde und sich das
passende BOOTSEL-Gerät für den Neustart getrennt hat. Ein Reboot-Timeout wird als
Fehler und nicht als erfolgreicher Flash-Vorgang gemeldet.

Warnungen:

- Während des Kopierens nicht trennen.
- Das gewählte Ziel muss sowohl zur UF2-Family als auch zum verbundenen Controller passen.
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
- Firmware Update mit `R` neu scannen.

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

### Tab5-UI-Performance

Die Tab5-Oberfläche behält den 1280-x-720-Canvas jetzt bei und zeichnet bei
normalen Änderungen ausschließlich zusammengefasste Dirty-Regions neu.
Vollbilder sind auf Seiten- und Modalwechsel sowie große kombinierte Änderungen
begrenzt. Das physische Display bleibt in nativer 720-x-1280-Rotation, während
der PSRAM-Canvas logische 1280-x-720-Koordinaten behält; dadurch entfällt der
teure gedrehte M5GFX-Vollbildtransfer. Touch-down-Feedback wird vor Ton,
Diagnose und ausgelagerter SD-/Profil-/Firmwarearbeit übertragen. Messwerte,
Toolchain-/Kconfig-Vergleich sowie Cachevergleich und LVGL-Empfehlung stehen in
[TAB5_UI_PERFORMANCE.md](TAB5_UI_PERFORMANCE.md).

- Das Projekt ist bewusst kompakt und auf ein kleines Handheld-Display
  zugeschnitten.
- Die UI soll nicht blockieren; `M5Cardputer.update()` muss regelmäßig laufen.
- USB-CLI-Kommunikation und UF2-Mass-Storage-Flasher sollen klar getrennt
  bleiben.
- Die begrenzte Command-Queue erhält die Reihenfolge der Benutzerbefehle und
  sendet sie mit kurzem Abstand.
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
