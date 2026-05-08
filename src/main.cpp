#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "M5Cardputer.h"
#if NIGHTKITE_USB_HOST
#include <USBHostSerial.h>
#endif

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint16_t COLOR_BG = TFT_BLACK;
constexpr uint16_t COLOR_PANEL = 0x1082;
constexpr uint16_t COLOR_PANEL_DARK = 0x0841;
constexpr uint16_t COLOR_PANEL_LIGHT = 0x2124;
constexpr uint16_t COLOR_TEXT = TFT_WHITE;
constexpr uint16_t COLOR_MUTED = 0x9CF3;
constexpr uint16_t COLOR_ACCENT = 0x07FF;
constexpr uint16_t COLOR_ACCENT_DARK = 0x0451;
constexpr uint16_t COLOR_OK = 0x07E0;
constexpr uint16_t COLOR_WARN = 0xFFE0;
constexpr uint16_t COLOR_ERR = 0xF800;

constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;
constexpr int STATUS_H = 15;
constexpr int TAB_H = 14;
constexpr int FOOTER_H = 13;
constexpr int CONTENT_Y = STATUS_H + TAB_H;
constexpr int CONTENT_H = SCREEN_H - STATUS_H - TAB_H - FOOTER_H;
constexpr int MAX_TERMINAL_LINES = 8;
constexpr unsigned long CARDPUTER_BATTERY_POLL_MS = 3000;

constexpr int SD_SPI_SCK_PIN = 40;
constexpr int SD_SPI_MISO_PIN = 39;
constexpr int SD_SPI_MOSI_PIN = 14;
constexpr int SD_SPI_CS_PIN = 12;
constexpr uint32_t ALL_PATTERN_MASK = (1UL << 22) - 1UL;

const char* const ALL_PATTERN_LIST = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22";

const char* const VIEW_NAMES[] = {"Status", "Patt", "Cfg", "Prof", "Svc", "Term"};
constexpr size_t VIEW_COUNT = sizeof(VIEW_NAMES) / sizeof(VIEW_NAMES[0]);

enum class View : uint8_t {
  Status,
  Patterns,
  Config,
  Profiles,
  Service,
  Terminal,
};

struct NightKiteState {
  bool linkSeen = false;
  bool errorSeen = false;
  bool unsavedHint = false;
  bool sdReady = false;
  unsigned long lastRxMs = 0;
  unsigned long lastTxMs = 0;

  int pattern = -1;
  int brightness = -1;
  int stripLength = -1;
  int smoothing = -1;
  int accelRange = -1;
  int gyroRange = -1;
  int autoplayInterval = -1;

  String bootCalibration = "--";
  String autoplay = "--";
  String enabledPatterns = "--";
  String invertedPatterns = "--";
  String kiteBatteryVoltage = "--";
  String mpuConnected = "--";
  String dmpReady = "--";
  String fps = "--";
  String cardBatteryLevel = "--";
  String cardBatteryVoltage = "--";
  bool cardCharging = false;
  String lastError = "";
  String lastOk = "";
  String sdStatus = "SD: --";
};

struct MenuItem {
  const char* label;
  const char* command;
  bool confirm;
};

const MenuItem PATTERN_MENU[] = {
    {"Refresh patterns", "patterns", false},
    {"Pattern +", nullptr, false},
    {"Pattern -", nullptr, false},
    {"Enable current", nullptr, false},
    {"Disable current", nullptr, true},
    {"Invert current", nullptr, false},
    {"Normal current", nullptr, false},
    {"Enable all", "enable_pattern 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22", false},
    {"Normal all", "normal_pattern 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22", false},
    {"Pattern 1", "set pattern 1", false},
    {"Pattern 2", "set pattern 2", false},
    {"Pattern 3", "set pattern 3", false},
    {"Pattern 4", "set pattern 4", false},
    {"Pattern 5", "set pattern 5", false},
};

const MenuItem CONFIG_MENU[] = {
    {"Show config", "show", false},
    {"Brightness +", nullptr, false},
    {"Brightness -", nullptr, false},
    {"Autoplay on", "set autoplay on", false},
    {"Autoplay off", "set autoplay off", false},
    {"Autoplay 20s", "set autoplay_interval 20", false},
    {"Save", "save", false},
    {"Load", "load", true},
};

const MenuItem PROFILE_MENU[] = {
    {"Init SD", nullptr, false},
    {"Refresh config", "show", false},
    {"Save slot 1", nullptr, true},
    {"Save slot 2", nullptr, true},
    {"Save slot 3", nullptr, true},
    {"Load slot 1", nullptr, true},
    {"Load slot 2", nullptr, true},
    {"Load slot 3", nullptr, true},
};

const MenuItem SERVICE_MENU[] = {
    {"Battery", "battery", false},
    {"Sensor", "sensor", false},
    {"Timing", "timing", false},
    {"Offsets", "offsets", false},
    {"Quick calibrate", "calibrate quick", true},
    {"Precise calibrate", "calibrate precise", true},
    {"Defaults", "defaults", true},
    {"Reboot", "reboot", true},
};

NightKiteState nk;
View currentView = View::Status;
int selectedIndex = 0;
bool needsRender = true;
bool confirmPending = false;
String pendingCommand;
String pendingLabel;
String terminalLines[MAX_TERMINAL_LINES];
int terminalLineCount = 0;
String inputLine;
String rxLine;
unsigned long lastPollMs = 0;
unsigned long lastCardBatteryPollMs = 0;

int brightnessLevels[] = {95, 127, 159, 191, 223, 255};
constexpr size_t BRIGHTNESS_LEVEL_COUNT = sizeof(brightnessLevels) / sizeof(brightnessLevels[0]);

void addTerminalLine(const String& line);
void sendCommand(const String& command);

class NightKiteTransport {
public:
  virtual bool connected() = 0;
  virtual void sendLine(const String& line) = 0;
  virtual bool readLine(String& line) = 0;
};

class DebugSerialTransport : public NightKiteTransport {
public:
  bool connected() override
  {
    return true;
  }

  void sendLine(const String& line) override
  {
    Serial.print("NKLINK> ");
    Serial.println(line);
  }

  bool readLine(String& line) override
  {
    while (Serial.available() > 0) {
      char c = static_cast<char>(Serial.read());
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        line = buffer;
        buffer = "";
        line.trim();
        return line.length() > 0;
      }
      if (buffer.length() < 180) {
        buffer += c;
      }
    }
    return false;
  }

private:
  String buffer;
};

#if NIGHTKITE_USB_HOST
class UsbHostSerialTransport : public NightKiteTransport {
public:
  UsbHostSerialTransport() : hostSerial(0x2E8A, CDC_HOST_ANY_PID)
  {
  }

  void begin()
  {
    if (started) {
      return;
    }
    started = hostSerial.begin(SERIAL_BAUD, 0, 0, 8);
  }

  bool connected() override
  {
    begin();
    return static_cast<bool>(hostSerial);
  }

  void sendLine(const String& line) override
  {
    begin();
    if (!connected()) {
      return;
    }
    hostSerial.write(reinterpret_cast<const uint8_t*>(line.c_str()), line.length());
    hostSerial.write(static_cast<uint8_t>('\n'));
  }

  bool readLine(String& line) override
  {
    begin();
    while (hostSerial.available() > 0) {
      char c = static_cast<char>(hostSerial.read());
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        line = buffer;
        buffer = "";
        line.trim();
        return line.length() > 0;
      }
      if (buffer.length() < 180) {
        buffer += c;
      }
    }
    return false;
  }

private:
  USBHostSerial hostSerial;
  bool started = false;
  String buffer;
};

UsbHostSerialTransport transport;
#else
DebugSerialTransport transport;
#endif

String profilePath(int slot)
{
  return "/nightkite/profiles/slot" + String(slot) + ".nkc";
}

bool ensureSdReady()
{
  if (nk.sdReady) {
    return true;
  }

  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    nk.sdStatus = "SD init failed";
    addTerminalLine("ERR " + nk.sdStatus);
    return false;
  }

  if (SD.cardType() == CARD_NONE) {
    nk.sdStatus = "No SD card";
    addTerminalLine("ERR " + nk.sdStatus);
    return false;
  }

  SD.mkdir("/nightkite");
  SD.mkdir("/nightkite/profiles");
  nk.sdReady = true;
  nk.sdStatus = "SD ready";
  addTerminalLine("OK " + nk.sdStatus);
  return true;
}

bool validField(const String& value)
{
  return value.length() > 0 && value != "--";
}

uint32_t patternMaskFromList(String list)
{
  uint32_t mask = 0;
  list.trim();
  int start = 0;
  while (start < list.length()) {
    int comma = list.indexOf(',', start);
    String token = comma >= 0 ? list.substring(start, comma) : list.substring(start);
    token.trim();
    int id = token.toInt();
    if (id >= 1 && id <= 22) {
      mask |= (1UL << (id - 1));
    }
    if (comma < 0) {
      break;
    }
    start = comma + 1;
  }
  return mask;
}

String patternListFromMask(uint32_t mask)
{
  String list;
  for (int id = 1; id <= 22; ++id) {
    if ((mask & (1UL << (id - 1))) == 0) {
      continue;
    }
    if (list.length() > 0) {
      list += ',';
    }
    list += String(id);
  }
  return list;
}

void writeCommandIfInt(File& file, const char* key, int value)
{
  if (value >= 0) {
    file.print("set ");
    file.print(key);
    file.print(' ');
    file.println(value);
  }
}

void writeCommandIfString(File& file, const char* key, const String& value)
{
  if (validField(value)) {
    file.print("set ");
    file.print(key);
    file.print(' ');
    file.println(value);
  }
}

bool saveProfileSlot(int slot)
{
  if (!ensureSdReady()) {
    return false;
  }

  String path = profilePath(slot);
  SD.remove(path);
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    nk.sdStatus = "Profile open failed";
    addTerminalLine("ERR " + nk.sdStatus);
    return false;
  }

  file.println("# NightKite Link profile");
  file.println("# CLI command file for NightKite Multi");
  writeCommandIfInt(file, "pattern", nk.pattern);
  writeCommandIfInt(file, "brightness", nk.brightness);
  writeCommandIfInt(file, "strip_length", nk.stripLength);
  writeCommandIfInt(file, "smoothing", nk.smoothing);
  writeCommandIfInt(file, "accel_range", nk.accelRange);
  writeCommandIfInt(file, "gyro_range", nk.gyroRange);
  writeCommandIfString(file, "boot_calibration", nk.bootCalibration);
  writeCommandIfString(file, "autoplay", nk.autoplay);
  writeCommandIfInt(file, "autoplay_interval", nk.autoplayInterval);

  if (validField(nk.enabledPatterns)) {
    uint32_t enabledMask = patternMaskFromList(nk.enabledPatterns);
    uint32_t disabledMask = ALL_PATTERN_MASK & ~enabledMask;
    String disabledPatterns = patternListFromMask(disabledMask);
    file.print("enable_pattern ");
    file.println(ALL_PATTERN_LIST);
    if (disabledPatterns.length() > 0 && disabledPatterns != ALL_PATTERN_LIST) {
      file.print("disable_pattern ");
      file.println(disabledPatterns);
    }
  }

  file.print("normal_pattern ");
  file.println(ALL_PATTERN_LIST);
  if (validField(nk.invertedPatterns)) {
    file.print("invert_pattern ");
    file.println(nk.invertedPatterns);
  }

  file.println("save");
  file.close();

  nk.sdStatus = "Saved slot " + String(slot);
  addTerminalLine("OK " + nk.sdStatus);
  return true;
}

bool loadProfileSlot(int slot)
{
  if (!ensureSdReady()) {
    return false;
  }

  String path = profilePath(slot);
  File file = SD.open(path, FILE_READ);
  if (!file) {
    nk.sdStatus = "Profile missing " + String(slot);
    addTerminalLine("ERR " + nk.sdStatus);
    return false;
  }

  int sent = 0;
  while (file.available()) {
    String command = file.readStringUntil('\n');
    command.trim();
    if (command.length() == 0 || command.startsWith("#")) {
      continue;
    }
    sendCommand(command);
    ++sent;
    delay(20);
  }
  file.close();

  nk.sdStatus = "Loaded slot " + String(slot) + " cmds=" + String(sent);
  addTerminalLine("OK " + nk.sdStatus);
  return sent > 0;
}

void addTerminalLine(const String& line)
{
  if (terminalLineCount < MAX_TERMINAL_LINES) {
    terminalLines[terminalLineCount++] = line;
  } else {
    for (int i = 1; i < MAX_TERMINAL_LINES; ++i) {
      terminalLines[i - 1] = terminalLines[i];
    }
    terminalLines[MAX_TERMINAL_LINES - 1] = line;
  }
  needsRender = true;
}

String valueForKey(const String& line, const char* key)
{
  String token = String(key) + "=";
  int start = line.indexOf(token);
  if (start < 0) {
    return "";
  }
  start += token.length();
  int end = line.indexOf(' ', start);
  if (end < 0) {
    end = line.length();
  }
  String value = line.substring(start, end);
  value.trim();
  return value;
}

void parseIntField(const String& line, const char* key, int& target)
{
  String value = valueForKey(line, key);
  if (value.length() > 0) {
    target = value.toInt();
  }
}

void parseStringField(const String& line, const char* key, String& target)
{
  String token = String(key) + "=";
  if (line.indexOf(token) < 0) {
    return;
  }
  String value = valueForKey(line, key);
  target = value;
}

String stripCliPrompt(String line)
{
  line.trim();
  while (line.startsWith("nk>")) {
    line = line.substring(3);
    line.trim();
  }
  return line;
}

void parseNightKiteLine(const String& line)
{
  String parsedLine = stripCliPrompt(line);

  if (parsedLine.startsWith("OK ")) {
    nk.linkSeen = true;
    nk.errorSeen = false;
    nk.lastRxMs = millis();
    nk.lastOk = parsedLine.substring(3);

    parseIntField(parsedLine, "pattern", nk.pattern);
    parseIntField(parsedLine, "brightness", nk.brightness);
    parseIntField(parsedLine, "strip_length", nk.stripLength);
    parseIntField(parsedLine, "smoothing", nk.smoothing);
    parseIntField(parsedLine, "accel_range", nk.accelRange);
    parseIntField(parsedLine, "gyro_range", nk.gyroRange);
    parseIntField(parsedLine, "autoplay_interval", nk.autoplayInterval);
    parseStringField(parsedLine, "boot_calibration", nk.bootCalibration);
    parseStringField(parsedLine, "autoplay", nk.autoplay);
    parseStringField(parsedLine, "enabled_patterns", nk.enabledPatterns);
    parseStringField(parsedLine, "inverted_patterns", nk.invertedPatterns);
    parseStringField(parsedLine, "battery_voltage", nk.kiteBatteryVoltage);
    parseStringField(parsedLine, "mpu_connected", nk.mpuConnected);
    parseStringField(parsedLine, "dmp_ready", nk.dmpReady);
    parseStringField(parsedLine, "fps", nk.fps);

    if (parsedLine.indexOf("saved=1") >= 0 || parsedLine.indexOf("loaded=1") >= 0) {
      nk.unsavedHint = false;
    }
    if (parsedLine.indexOf("defaults=1") >= 0 || parsedLine.indexOf("applies after reboot") >= 0) {
      nk.unsavedHint = true;
    }
  } else if (parsedLine.startsWith("ERR")) {
    nk.linkSeen = true;
    nk.errorSeen = true;
    nk.lastRxMs = millis();
    nk.lastError = parsedLine;
  } else if (parsedLine.startsWith("INFO") || parsedLine.startsWith("[NightKite CLI]")) {
    nk.linkSeen = true;
    nk.lastRxMs = millis();
  }
}

void sendCommand(const String& command)
{
  if (command.length() == 0) {
    return;
  }
  transport.sendLine(command);
  nk.lastTxMs = millis();
  addTerminalLine("> " + command);
}

void runActionCommand(const String& command)
{
  if (command == "@sd:init") {
    ensureSdReady();
  } else if (command == "@profile:save:1") {
    saveProfileSlot(1);
  } else if (command == "@profile:save:2") {
    saveProfileSlot(2);
  } else if (command == "@profile:save:3") {
    saveProfileSlot(3);
  } else if (command == "@profile:load:1") {
    loadProfileSlot(1);
  } else if (command == "@profile:load:2") {
    loadProfileSlot(2);
  } else if (command == "@profile:load:3") {
    loadProfileSlot(3);
  } else {
    sendCommand(command);
  }
}

void executeMenuItem(const MenuItem& item)
{
  String command = item.command == nullptr ? "" : String(item.command);

  if (command.length() == 0) {
    if (String(item.label) == "Pattern +" && nk.pattern > 0) {
      command = "set pattern " + String(min(22, nk.pattern + 1));
    } else if (String(item.label) == "Pattern -" && nk.pattern > 0) {
      command = "set pattern " + String(max(1, nk.pattern - 1));
    } else if (String(item.label) == "Enable current" && nk.pattern > 0) {
      command = "enable_pattern " + String(nk.pattern);
    } else if (String(item.label) == "Disable current" && nk.pattern > 0) {
      command = "disable_pattern " + String(nk.pattern);
    } else if (String(item.label) == "Invert current" && nk.pattern > 0) {
      command = "invert_pattern " + String(nk.pattern);
    } else if (String(item.label) == "Normal current" && nk.pattern > 0) {
      command = "normal_pattern " + String(nk.pattern);
    } else if (String(item.label) == "Init SD") {
      command = "@sd:init";
    } else if (String(item.label) == "Save slot 1") {
      command = "@profile:save:1";
    } else if (String(item.label) == "Save slot 2") {
      command = "@profile:save:2";
    } else if (String(item.label) == "Save slot 3") {
      command = "@profile:save:3";
    } else if (String(item.label) == "Load slot 1") {
      command = "@profile:load:1";
    } else if (String(item.label) == "Load slot 2") {
      command = "@profile:load:2";
    } else if (String(item.label) == "Load slot 3") {
      command = "@profile:load:3";
    } else if (String(item.label) == "Brightness +") {
      int current = nk.brightness > 0 ? nk.brightness : brightnessLevels[0];
      int next = brightnessLevels[BRIGHTNESS_LEVEL_COUNT - 1];
      for (size_t i = 0; i < BRIGHTNESS_LEVEL_COUNT; ++i) {
        if (brightnessLevels[i] > current) {
          next = brightnessLevels[i];
          break;
        }
      }
      command = "set brightness " + String(next);
    } else if (String(item.label) == "Brightness -") {
      int current = nk.brightness > 0 ? nk.brightness : brightnessLevels[0];
      int next = brightnessLevels[0];
      for (int i = static_cast<int>(BRIGHTNESS_LEVEL_COUNT) - 1; i >= 0; --i) {
        if (brightnessLevels[i] < current) {
          next = brightnessLevels[i];
          break;
        }
      }
      command = "set brightness " + String(next);
    }
  }

  if (item.confirm) {
    confirmPending = true;
    pendingCommand = command;
    pendingLabel = item.label;
    needsRender = true;
    return;
  }

  runActionCommand(command);
}

void drawTextFit(const String& text, int x, int y, int w, uint16_t color)
{
  String out = text;
  const int maxChars = max(1, w / 6);
  if (static_cast<int>(out.length()) > maxChars) {
    out = out.substring(0, maxChars - 1) + "~";
  }
  M5Cardputer.Display.setTextColor(color);
  M5Cardputer.Display.drawString(out, x, y);
}

String showInt(int value)
{
  return value >= 0 ? String(value) : "--";
}

String voltageTextFromMv(int mv)
{
  if (mv <= 0) {
    return "--";
  }
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%.2fV", mv / 1000.0f);
  return String(buffer);
}

void updateCardputerBattery(bool force = false)
{
  if (!force && millis() - lastCardBatteryPollMs < CARDPUTER_BATTERY_POLL_MS) {
    return;
  }
  lastCardBatteryPollMs = millis();

  int level = M5.Power.getBatteryLevel();
  int voltageMv = M5.Power.getBatteryVoltage();
  auto charging = M5.Power.isCharging();

  nk.cardBatteryLevel = level >= 0 ? String(level) + "%" : "--";
  nk.cardBatteryVoltage = voltageTextFromMv(voltageMv);
  nk.cardCharging = charging == m5::Power_Class::is_charging_t::is_charging;
  needsRender = true;
}

uint16_t terminalColor(const String& line)
{
  if (line.startsWith(">")) {
    return COLOR_ACCENT;
  }
  if (line.startsWith("OK")) {
    return COLOR_OK;
  }
  if (line.startsWith("ERR")) {
    return COLOR_ERR;
  }
  if (line.startsWith("INFO")) {
    return COLOR_MUTED;
  }
  return COLOR_TEXT;
}

uint16_t statusColor(bool ok, bool warn = false)
{
  if (ok) {
    return COLOR_OK;
  }
  return warn ? COLOR_WARN : COLOR_MUTED;
}

String footerText()
{
  switch (currentView) {
    case View::Status:
      return "Enter refresh   1 show 2 batt 3 sensor";
    case View::Patterns:
      return "Arrows select   Enter run   ! confirm";
    case View::Config:
      return "Brightness, autoplay, save/load";
    case View::Profiles:
      return "SD profile slots   refresh before save";
    case View::Service:
      return "Diagnostics and calibration tools";
    case View::Terminal:
      return "Type command   Enter send   Back delete";
  }
  return "Tab view   Arrows select   Enter run";
}

void drawStatusBadge(int x, int y, const String& label, uint16_t color)
{
  auto& d = M5Cardputer.Display;
  int w = 24 + min(42, static_cast<int>(label.length()) * 5);
  d.fillRoundRect(x, y, w, 10, 2, COLOR_PANEL_DARK);
  d.drawFastVLine(x, y + 1, 8, color);
  d.setTextColor(color, COLOR_PANEL_DARK);
  d.drawString(label, x + 5, y + 3);
}

void drawChrome()
{
  auto& d = M5Cardputer.Display;
  d.fillScreen(COLOR_BG);
  d.fillRect(0, 0, SCREEN_W, STATUS_H, COLOR_PANEL_DARK);
  d.setTextSize(1);
  d.setFont(&fonts::Font0);
  d.setTextDatum(top_left);

  d.setTextColor(COLOR_TEXT, COLOR_PANEL_DARK);
  d.drawString("NightKite Link", 3, 4);

  drawStatusBadge(83, 3, nk.errorSeen ? "NK ERR" : (nk.linkSeen ? "NK OK" : "NK --"),
                  nk.errorSeen ? COLOR_ERR : statusColor(nk.linkSeen));
  drawStatusBadge(136, 3, nk.sdReady ? "SD OK" : "SD --", statusColor(nk.sdReady));
  drawStatusBadge(181, 3, (nk.cardCharging ? "+" : "") + nk.cardBatteryLevel,
                  nk.cardCharging ? COLOR_OK : COLOR_ACCENT);
  if (nk.unsavedHint) {
    drawStatusBadge(214, 3, "!", COLOR_WARN);
  }

  d.fillRect(0, STATUS_H, SCREEN_W, TAB_H, COLOR_BG);
  int tabW = SCREEN_W / VIEW_COUNT;
  for (size_t i = 0; i < VIEW_COUNT; ++i) {
    bool active = i == static_cast<size_t>(currentView);
    int x = i * tabW;
    d.fillRect(x, STATUS_H, tabW - 1, TAB_H, active ? COLOR_ACCENT_DARK : COLOR_PANEL);
    if (active) {
      d.drawFastHLine(x + 2, STATUS_H + TAB_H - 2, tabW - 5, COLOR_ACCENT);
    }
    d.setTextColor(active ? COLOR_TEXT : COLOR_MUTED, active ? COLOR_ACCENT_DARK : COLOR_PANEL);
    d.drawString(VIEW_NAMES[i], x + 2, STATUS_H + 4);
  }

  d.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, COLOR_PANEL);
  d.setTextColor(COLOR_MUTED, COLOR_PANEL);
  d.drawString(footerText(), 3, SCREEN_H - 9);
}

void drawMetric(int x, int y, const char* label, const String& value, uint16_t color = COLOR_TEXT)
{
  auto& d = M5Cardputer.Display;
  d.fillRoundRect(x, y, 55, 28, 3, COLOR_PANEL_DARK);
  d.drawFastHLine(x + 2, y + 1, 51, COLOR_PANEL_LIGHT);
  d.setTextColor(COLOR_MUTED, COLOR_PANEL_DARK);
  d.drawString(label, x + 4, y + 5);
  d.setTextColor(color, COLOR_PANEL_DARK);
  d.drawString(value, x + 4, y + 17);
}

void drawBar(int x, int y, int w, int h, int value, int minValue, int maxValue, uint16_t color)
{
  auto& d = M5Cardputer.Display;
  d.fillRoundRect(x, y, w, h, 2, COLOR_PANEL_DARK);
  if (value < minValue || maxValue <= minValue) {
    return;
  }
  int clamped = constrain(value, minValue, maxValue);
  int fillW = map(clamped, minValue, maxValue, 0, w - 2);
  d.fillRoundRect(x + 1, y + 1, fillW, h - 2, 1, color);
}

void drawStatusView()
{
  drawMetric(4, CONTENT_Y + 3, "Pattern", showInt(nk.pattern), COLOR_ACCENT);
  drawMetric(64, CONTENT_Y + 3, "Bright", showInt(nk.brightness), COLOR_TEXT);
  drawMetric(124, CONTENT_Y + 3, "Autoplay", nk.autoplay, nk.autoplay == "on" ? COLOR_OK : COLOR_MUTED);
  drawMetric(184, CONTENT_Y + 3, "Link V", nk.kiteBatteryVoltage, COLOR_TEXT);

  drawMetric(4, CONTENT_Y + 35, "Strip", showInt(nk.stripLength));
  drawMetric(64, CONTENT_Y + 35, "DMP", nk.dmpReady == "1" ? "OK" : nk.dmpReady, nk.dmpReady == "1" ? COLOR_OK : COLOR_WARN);
  drawMetric(124, CONTENT_Y + 35, "MPU", nk.mpuConnected == "1" ? "OK" : nk.mpuConnected, nk.mpuConnected == "1" ? COLOR_OK : COLOR_WARN);
  drawMetric(184, CONTENT_Y + 35, "Card", nk.cardBatteryLevel, nk.cardCharging ? COLOR_OK : COLOR_ACCENT);

  drawBar(64, CONTENT_Y + 66, 55, 5, nk.brightness, 95, 255, COLOR_ACCENT);
  drawTextFit("Card " + nk.cardBatteryVoltage, 4, CONTENT_Y + 75, 70, nk.cardCharging ? COLOR_OK : COLOR_MUTED);
  drawTextFit(nk.sdStatus, 77, CONTENT_Y + 75, 38, nk.sdReady ? COLOR_OK : COLOR_MUTED);
  drawTextFit(nk.errorSeen ? nk.lastError : nk.lastOk, 118, CONTENT_Y + 75, 118, nk.errorSeen ? COLOR_ERR : COLOR_MUTED);
}

void drawMenu(const MenuItem* items, size_t count)
{
  auto& d = M5Cardputer.Display;
  int rowH = 12;
  int visibleRows = CONTENT_H / rowH;
  int start = 0;
  if (selectedIndex >= visibleRows) {
    start = selectedIndex - visibleRows + 1;
  }
  if (start > 0) {
    d.setTextColor(COLOR_MUTED, COLOR_BG);
    d.drawString("^", 230, CONTENT_Y + 2);
  }
  for (int row = 0; row < visibleRows && (start + row) < static_cast<int>(count); ++row) {
    int idx = start + row;
    int y = CONTENT_Y + row * rowH;
    bool active = idx == selectedIndex;
    uint16_t bg = active ? COLOR_ACCENT_DARK : (row % 2 == 0 ? COLOR_BG : COLOR_PANEL_DARK);
    d.fillRect(2, y, SCREEN_W - 4, rowH - 1, bg);
    if (active) {
      d.drawFastVLine(3, y + 1, rowH - 3, COLOR_ACCENT);
    }
    d.setTextColor(active ? COLOR_TEXT : COLOR_MUTED, bg);
    d.drawString(active ? ">" : " ", 7, y + 3);
    d.setTextColor(active ? COLOR_TEXT : COLOR_TEXT, bg);
    d.drawString(items[idx].label, 17, y + 3);
    if (items[idx].confirm) {
      d.setTextColor(COLOR_WARN, bg);
      d.drawString("!", 226, y + 3);
    }
  }
  if (start + visibleRows < static_cast<int>(count)) {
    d.setTextColor(COLOR_MUTED, COLOR_BG);
    d.drawString("v", 230, SCREEN_H - FOOTER_H - 10);
  }
}

void drawTerminalView()
{
  auto& d = M5Cardputer.Display;
  int y = CONTENT_Y + 2;
  for (int i = 0; i < terminalLineCount; ++i) {
    uint16_t color = terminalColor(terminalLines[i]);
    drawTextFit(terminalLines[i], 3, y, 234, color);
    y += 10;
  }
  d.fillRect(0, SCREEN_H - FOOTER_H - 15, SCREEN_W, 15, COLOR_PANEL_DARK);
  d.drawFastHLine(0, SCREEN_H - FOOTER_H - 15, SCREEN_W, COLOR_PANEL_LIGHT);
  d.setTextColor(COLOR_ACCENT, COLOR_BG);
  d.setTextColor(COLOR_ACCENT, COLOR_PANEL_DARK);
  d.drawString("> ", 3, SCREEN_H - FOOTER_H - 10);
  drawTextFit(inputLine, 15, SCREEN_H - FOOTER_H - 10, 220, COLOR_ACCENT);
}

void drawConfirmDialog()
{
  auto& d = M5Cardputer.Display;
  d.fillRoundRect(16, 39, 208, 58, 4, COLOR_PANEL);
  d.drawRoundRect(16, 39, 208, 58, 4, COLOR_WARN);
  d.fillRect(16, 39, 208, 14, COLOR_PANEL_DARK);
  d.setTextColor(COLOR_WARN, COLOR_PANEL);
  d.drawString("Confirm action", 26, 44);
  d.setTextColor(COLOR_TEXT, COLOR_PANEL);
  drawTextFit(pendingLabel, 26, 61, 188, COLOR_TEXT);
  d.setTextColor(COLOR_MUTED, COLOR_PANEL);
  d.drawString("Enter yes   Back no", 26, 80);
}

void render()
{
  if (!needsRender) {
    return;
  }
  drawChrome();
  switch (currentView) {
    case View::Status:
      drawStatusView();
      break;
    case View::Patterns:
      drawMenu(PATTERN_MENU, sizeof(PATTERN_MENU) / sizeof(PATTERN_MENU[0]));
      break;
    case View::Config:
      drawMenu(CONFIG_MENU, sizeof(CONFIG_MENU) / sizeof(CONFIG_MENU[0]));
      break;
    case View::Profiles:
      drawMenu(PROFILE_MENU, sizeof(PROFILE_MENU) / sizeof(PROFILE_MENU[0]));
      break;
    case View::Service:
      drawMenu(SERVICE_MENU, sizeof(SERVICE_MENU) / sizeof(SERVICE_MENU[0]));
      break;
    case View::Terminal:
      drawTerminalView();
      break;
  }
  if (confirmPending) {
    drawConfirmDialog();
  }
  needsRender = false;
}

size_t menuCountForCurrentView()
{
  switch (currentView) {
    case View::Patterns:
      return sizeof(PATTERN_MENU) / sizeof(PATTERN_MENU[0]);
    case View::Config:
      return sizeof(CONFIG_MENU) / sizeof(CONFIG_MENU[0]);
    case View::Profiles:
      return sizeof(PROFILE_MENU) / sizeof(PROFILE_MENU[0]);
    case View::Service:
      return sizeof(SERVICE_MENU) / sizeof(SERVICE_MENU[0]);
    default:
      return 0;
  }
}

void resetSelection()
{
  selectedIndex = 0;
}

void changeView(int delta)
{
  int next = static_cast<int>(currentView) + delta;
  if (next < 0) {
    next = VIEW_COUNT - 1;
  }
  if (next >= static_cast<int>(VIEW_COUNT)) {
    next = 0;
  }
  currentView = static_cast<View>(next);
  confirmPending = false;
  resetSelection();
  needsRender = true;
}

void moveSelection(int delta)
{
  size_t count = menuCountForCurrentView();
  if (count == 0) {
    return;
  }

  int next = selectedIndex + delta;
  if (next < 0) {
    next = 0;
  }
  if (next >= static_cast<int>(count)) {
    next = static_cast<int>(count) - 1;
  }
  if (next != selectedIndex) {
    selectedIndex = next;
    needsRender = true;
  }
}

void runSelected()
{
  switch (currentView) {
    case View::Patterns:
      executeMenuItem(PATTERN_MENU[selectedIndex]);
      break;
    case View::Config:
      executeMenuItem(CONFIG_MENU[selectedIndex]);
      break;
    case View::Profiles:
      executeMenuItem(PROFILE_MENU[selectedIndex]);
      break;
    case View::Service:
      executeMenuItem(SERVICE_MENU[selectedIndex]);
      break;
    case View::Status:
      sendCommand("show");
      sendCommand("battery");
      sendCommand("sensor");
      break;
    case View::Terminal:
      sendCommand(inputLine);
      inputLine = "";
      break;
  }
}

void quickCommand(char c)
{
  switch (c) {
    case '1':
      sendCommand("show");
      break;
    case '2':
      sendCommand("battery");
      break;
    case '3':
      sendCommand("sensor");
      break;
    case '4':
      sendCommand("patterns");
      break;
    case '5':
      sendCommand("timing");
      break;
    case '6':
      sendCommand("offsets");
      break;
    default:
      return;
  }
}

void handleWordChar(char c)
{
  if (confirmPending) {
    return;
  }

  if (currentView == View::Terminal) {
    if (inputLine.length() < 96) {
      inputLine += c;
      needsRender = true;
    }
    return;
  }

  switch (c) {
    case 'a':
    case 'A':
    case ',':
    case '<':
      changeView(-1);
      break;
    case 'd':
    case 'D':
    case '/':
    case '?':
      changeView(1);
      break;
    case 'w':
    case 'W':
    case ';':
    case ':':
      moveSelection(-1);
      break;
    case 's':
    case 'S':
    case '.':
    case '>':
      moveSelection(1);
      break;
    default:
      if (c >= '1' && c <= '9') {
        quickCommand(c);
      }
      break;
  }
}

void handleKeyboard()
{
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return;
  }

  auto& status = M5Cardputer.Keyboard.keysState();

  if (confirmPending) {
    if (status.enter) {
      runActionCommand(pendingCommand);
      confirmPending = false;
      pendingCommand = "";
      pendingLabel = "";
      needsRender = true;
    } else if (status.del) {
      confirmPending = false;
      pendingCommand = "";
      pendingLabel = "";
      needsRender = true;
    }
    return;
  }

  if (status.tab) {
    changeView(1);
    return;
  }

  if (status.enter) {
    runSelected();
    needsRender = true;
    return;
  }

  if (status.del) {
    if (currentView == View::Terminal && inputLine.length() > 0) {
      inputLine.remove(inputLine.length() - 1);
      needsRender = true;
    } else if (currentView != View::Status) {
      currentView = View::Status;
      resetSelection();
      needsRender = true;
    }
    return;
  }

  for (char c : status.word) {
    handleWordChar(c);
  }
}

void pollTransport()
{
  String line;
  while (transport.readLine(line)) {
    addTerminalLine(line);
    parseNightKiteLine(line);
  }

  if (millis() - lastPollMs > 5000) {
    lastPollMs = millis();
    if (!nk.linkSeen) {
      sendCommand("show");
    }
  }
}

}  // namespace

void setup()
{
  Serial.begin(SERIAL_BAUD);

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(96);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setFont(&fonts::Font0);
  M5Cardputer.Display.setTextDatum(top_left);

  updateCardputerBattery(true);
  addTerminalLine("NightKite Link boot");
  addTerminalLine("Debug transport on USB Serial");
  sendCommand("show");
}

void loop()
{
  M5Cardputer.update();
  updateCardputerBattery();
  handleKeyboard();
  pollTransport();
  render();
  delay(5);
}
