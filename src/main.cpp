#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <math.h>
#include <vector>
#include "M5Cardputer.h"
#include "Uf2Validator.h"
#include "UsbMscUf2Flasher.h"
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
constexpr int STATUS_H = 14;
constexpr int FOOTER_H = 13;
constexpr int CONTENT_Y = STATUS_H;
constexpr int CONTENT_H = SCREEN_H - STATUS_H - FOOTER_H;
constexpr int MAX_CLI_LINE_CHARS = 512;
constexpr unsigned long CARDPUTER_BATTERY_POLL_MS = 3000;
constexpr unsigned long LINK_STALE_MS = 9000;
constexpr unsigned long COMMAND_SEND_INTERVAL_MS = 160;

constexpr int SD_SPI_SCK_PIN = 40;
constexpr int SD_SPI_MISO_PIN = 39;
constexpr int SD_SPI_MOSI_PIN = 14;
constexpr int SD_SPI_CS_PIN = 12;
constexpr uint32_t ALL_PATTERN_MASK = (1UL << 22) - 1UL;
const char* const ALL_PATTERN_LIST = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22";

const char* const PATTERN_NAMES[] = {
    "",
    "Rainbow",
    "Full color",
    "Motion bright",
    "Runner fixed",
    "Runner reactive",
    "Runner dual",
    "Heartbeat",
    "Ping pong",
    "Comet swarm",
    "Breath storm",
    "Jerk wave",
    "Yaw spinner",
    "Yaw circle",
    "Runner dual inv",
    "Palette beat",
    "Pacifica kite",
    "Twinkle motion",
    "Fire jet",
    "Noise ring",
    "Pride yaw",
    "Confetti jerk",
    "Center ripple",
};
constexpr int PATTERN_COUNT = 22;

int brightnessLevels[] = {95, 127, 159, 191, 223, 255};
constexpr size_t BRIGHTNESS_LEVEL_COUNT = sizeof(brightnessLevels) / sizeof(brightnessLevels[0]);
int smoothingLevels[] = {1, 10, 20, 40, 60, 80, 100, 150, 256, 512};
constexpr size_t SMOOTHING_LEVEL_COUNT = sizeof(smoothingLevels) / sizeof(smoothingLevels[0]);
int autoplayIntervalLevels[] = {1, 5, 10, 20, 30, 60, 120, 300};
constexpr size_t AUTOPLAY_INTERVAL_LEVEL_COUNT = sizeof(autoplayIntervalLevels) / sizeof(autoplayIntervalLevels[0]);
int gyroRangeLevels[] = {250, 500, 1000, 2000};
constexpr size_t GYRO_RANGE_LEVEL_COUNT = sizeof(gyroRangeLevels) / sizeof(gyroRangeLevels[0]);
int accelRangeLevels[] = {2, 4, 8, 16};
constexpr size_t ACCEL_RANGE_LEVEL_COUNT = sizeof(accelRangeLevels) / sizeof(accelRangeLevels[0]);

struct PatternConfig {
  int id = 0;
  String name;
  bool cycleEnabled = false;
  bool inverted = false;
};

struct ControllerSettings {
  int brightness = -1;
  int stripLength = -1;
  int activePattern = -1;
  int smoothing = -1;
  int accelRange = -1;
  int gyroRange = -1;
  String bootCalibration = "--";
  String fps = "--";
  bool autoplayEnabled = false;
  int autoplayIntervalSeconds = -1;
  int controllerBatteryPercent = -1;
  float controllerBatteryVoltage = NAN;
  uint32_t enabledPatternMask = 0;
  uint32_t invertedPatternMask = 0;
  std::vector<PatternConfig> patterns;
};

enum class Card : uint8_t {
  Status,
  Brightness,
  Config,
  Calibration,
  ActivePattern,
  PatternList,
  PatternBulk,
  Firmware,
  Profiles,
};

constexpr int CARD_COUNT = 9;

enum class Mode : uint8_t {
  Cards,
  PatternDetail,
  ConfirmBulk,
  ConfirmProfileDelete,
  ProfileNameInput,
  ConfirmProfileOverwrite,
  ConfirmProfileApply,
};

enum class FlashUiState : uint8_t {
  Idle,
  Confirm,
  WaitingForBootsel,
  WaitingForMassStorage,
  MassStorageConnected,
  CopyingUf2,
  CopyComplete,
  WaitingForReboot,
  Success,
  Error,
};

struct FlashUiStatus {
  FlashUiState state = FlashUiState::Idle;
  String filename;
  String fullPath;
  String target;
  String message;
  String errorMessage;
  size_t totalBytes = 0;
  size_t copiedBytes = 0;
  int percent = 0;
  bool massStorageConnected = false;
  bool success = false;
  bool busy = false;
  unsigned long stateStartedMs = 0;
  unsigned long lastAnimationMs = 0;
  uint8_t spinner = 0;
};

struct AppState {
  bool usbConnected = false;
  bool controllerConnected = false;
  bool controllerError = false;
  bool sdReady = false;
  int cardputerBatteryPercent = -1;
  String cardputerBatteryVoltage = "--";
  bool cardputerCharging = false;
  ControllerSettings settings;
  ControllerSettings loadedProfile;
  bool hasLoadedProfile = false;
  String loadedProfileName;
  String loadedProfilePath;
  String profileNameInput;
  String pendingProfileName;
  String pendingProfilePath;
  int selectedCard = 0;
  int selectedPatternIndex = 0;
  int selectedProfileAction = 0;
  int selectedProfileIndex = 0;
  int selectedConfigField = 0;
  int selectedCalAction = 0;
  int selectedBulkAction = 0;
  int selectedFirmwareTarget = 0;
  int selectedFirmwareFileIndex = 0;
  bool patternEditsPending = false;
  FlashUiStatus flash;
  std::vector<String> profileFiles;
  std::vector<String> firmwareFiles;
  String lastCommand;
  String lastResponse;
  String statusMessage = "Ready";
  uint16_t statusColor = COLOR_MUTED;
  bool dirty = true;
};

AppState app;
Mode mode = Mode::Cards;
unsigned long lastCardBatteryPollMs = 0;
unsigned long lastPollMs = 0;
unsigned long lastRxMs = 0;
unsigned long lastCommandSendMs = 0;
bool lastUsbConnected = false;
String rxLine;
std::vector<String> commandQueue;
bool patternSyncInProgress = false;
bool canvasReady = false;
M5Canvas uiCanvas(&M5Cardputer.Display);
String editValue;
bool editBool = false;
bool detailCycle = false;
bool detailInvert = false;
int draftStripLength = -1;
int draftSmoothing = -1;
int draftAccelRange = -1;
int draftGyroRange = -1;
bool draftAutoplayEnabled = false;
int draftAutoplayIntervalSeconds = -1;

UsbMscUf2Flasher uf2Flasher;

bool ensureSdReady();

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
      if (buffer.length() < MAX_CLI_LINE_CHARS) {
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
      if (buffer.length() < MAX_CLI_LINE_CHARS) {
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

void setStatus(const String& text, uint16_t color = COLOR_MUTED)
{
  app.statusMessage = text;
  app.statusColor = color;
  app.dirty = true;
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

bool parseBoolText(const String& value)
{
  return value == "on" || value == "1" || value == "true" || value == "yes";
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
    if (id >= 1 && id <= PATTERN_COUNT) {
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
  for (int id = 1; id <= PATTERN_COUNT; ++id) {
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

const char* patternName(int patternId)
{
  if (patternId >= 1 && patternId <= PATTERN_COUNT) {
    return PATTERN_NAMES[patternId];
  }
  return "--";
}

void ensurePatternModel()
{
  if (app.settings.patterns.size() == PATTERN_COUNT) {
    return;
  }
  app.settings.patterns.clear();
  app.settings.patterns.reserve(PATTERN_COUNT);
  for (int id = 1; id <= PATTERN_COUNT; ++id) {
    PatternConfig pattern;
    pattern.id = id;
    pattern.name = patternName(id);
    app.settings.patterns.push_back(pattern);
  }
}

void applyPatternMasks(uint32_t enabledMask, uint32_t invertedMask, bool updateEnabled, bool updateInverted)
{
  if (app.patternEditsPending || patternSyncInProgress) {
    return;
  }
  ensurePatternModel();
  if (updateEnabled) {
    app.settings.enabledPatternMask = enabledMask;
  }
  if (updateInverted) {
    app.settings.invertedPatternMask = invertedMask;
  }
  for (auto& pattern : app.settings.patterns) {
    uint32_t bit = 1UL << (pattern.id - 1);
    if (updateEnabled) {
      pattern.cycleEnabled = (enabledMask & bit) != 0;
    }
    if (updateInverted) {
      pattern.inverted = (invertedMask & bit) != 0;
    }
  }
  app.dirty = true;
}

uint32_t currentEnabledMask()
{
  ensurePatternModel();
  uint32_t mask = 0;
  for (const auto& pattern : app.settings.patterns) {
    if (pattern.cycleEnabled) {
      mask |= 1UL << (pattern.id - 1);
    }
  }
  app.settings.enabledPatternMask = mask;
  return mask;
}

uint32_t currentInvertedMask()
{
  ensurePatternModel();
  uint32_t mask = 0;
  for (const auto& pattern : app.settings.patterns) {
    if (pattern.inverted) {
      mask |= 1UL << (pattern.id - 1);
    }
  }
  app.settings.invertedPatternMask = mask;
  return mask;
}

String shortText(String text, int chars);

class NightKiteCommands {
public:
  static String refreshAll()
  {
    return "show";
  }
  static String refreshPatterns()
  {
    return "patterns";
  }
  static String setBrightness(int value)
  {
    return "set brightness " + String(value);
  }
  static String setStripLength(int value)
  {
    return "set strip_length " + String(value);
  }
  static String setPattern(int value)
  {
    return "set pattern " + String(value);
  }
  static String setSmoothing(int value)
  {
    return "set smoothing " + String(value);
  }
  static String setAccelRange(int value)
  {
    return "set accel_range " + String(value);
  }
  static String setGyroRange(int value)
  {
    return "set gyro_range " + String(value);
  }
  static String setAutoplay(bool enabled)
  {
    return String("set autoplay ") + (enabled ? "on" : "off");
  }
  static String setAutoplayInterval(int seconds)
  {
    return "set autoplay_interval " + String(seconds);
  }
  static String setPatternCycle(int id, bool enabled)
  {
    return String(enabled ? "enable_pattern " : "disable_pattern ") + String(id);
  }
  static String setPatternInvert(int id, bool inverted)
  {
    return String(inverted ? "invert_pattern " : "normal_pattern ") + String(id);
  }
  static String setAllCycle(bool enabled)
  {
    // Existing NightKite Multi CLI supports comma-separated pattern IDs.
    return String(enabled ? "enable_pattern " : "disable_pattern ") + ALL_PATTERN_LIST;
  }
  static String setAllInvert(bool inverted)
  {
    // TODO: If firmware later adds set all_patterns_invert, map it here.
    return String(inverted ? "invert_pattern " : "normal_pattern ") + ALL_PATTERN_LIST;
  }
};

void sendCommand(const String& command)
{
  if (command.length() == 0) {
    setStatus("Command missing", COLOR_WARN);
    return;
  }
  bool wasEmpty = commandQueue.empty();
  commandQueue.push_back(command);
  app.lastCommand = command;
  if (wasEmpty) {
    setStatus("Queued command", COLOR_ACCENT);
  }
}

void pollCommandQueue()
{
  if (commandQueue.empty()) {
    if (patternSyncInProgress) {
      patternSyncInProgress = false;
      app.patternEditsPending = false;
      setStatus("Pattern states sent", COLOR_OK);
    }
    return;
  }
  if (millis() - lastCommandSendMs < COMMAND_SEND_INTERVAL_MS) {
    return;
  }
  if (!app.usbConnected) {
    setStatus("USB disconnected", COLOR_ERR);
    commandQueue.clear();
    patternSyncInProgress = false;
    app.dirty = true;
    return;
  }
  String command = commandQueue.front();
  commandQueue.erase(commandQueue.begin());
  transport.sendLine(command);
  lastCommandSendMs = millis();
  app.lastCommand = command;
  app.dirty = true;
  if (commandQueue.empty() && !patternSyncInProgress) {
    setStatus("Command sent", COLOR_ACCENT);
  }
}

int wrappedValue(const int* levels, size_t count, int currentValue, int delta)
{
  if (count == 0) {
    return currentValue;
  }
  int index = 0;
  if (currentValue >= 0) {
    int bestDistance = abs(currentValue - levels[0]);
    for (size_t i = 1; i < count; ++i) {
      int distance = abs(currentValue - levels[i]);
      if (distance < bestDistance) {
        bestDistance = distance;
        index = static_cast<int>(i);
      }
    }
  }
  int next = index + delta;
  if (next < 0) {
    next = count - 1;
  } else if (next >= static_cast<int>(count)) {
    next = 0;
  }
  return levels[next];
}

int wrapRange(int value, int minValue, int maxValue, int step, int delta)
{
  if (value < minValue || value > maxValue) {
    value = minValue;
  }
  value += step * delta;
  if (value < minValue) {
    value = maxValue;
  } else if (value > maxValue) {
    value = minValue;
  }
  return value;
}

String showInt(int value)
{
  return value >= 0 ? String(value) : "--";
}

String shortText(String text, int chars)
{
  if (static_cast<int>(text.length()) > chars) {
    return text.substring(0, max(0, chars - 1)) + "~";
  }
  return text;
}

void drawTextFit(const String& text, int x, int y, int w, uint16_t color, uint16_t bg = COLOR_BG)
{
  String out = shortText(text, max(1, w / 6));
  uiCanvas.setTextColor(color, bg);
  uiCanvas.drawString(out, x, y);
}

void drawBar(int x, int y, int w, int h, int value, int minValue, int maxValue, uint16_t color)
{
  auto& d = uiCanvas;
  d.fillRoundRect(x, y, w, h, 2, COLOR_PANEL_DARK);
  d.drawRoundRect(x, y, w, h, 2, COLOR_PANEL_LIGHT);
  if (value < minValue || maxValue <= minValue) {
    return;
  }
  int fillW = map(constrain(value, minValue, maxValue), minValue, maxValue, 0, w - 2);
  d.fillRoundRect(x + 1, y + 1, fillW, h - 2, 1, color);
}

void drawStatusBar()
{
  auto& d = uiCanvas;
  d.fillRect(0, 0, SCREEN_W, STATUS_H, COLOR_PANEL_DARK);
  String cp = app.cardputerBatteryPercent >= 0 ? String(app.cardputerBatteryPercent) + "%" : "--";
  String text = String("USB") + (app.usbConnected ? "+" : "-") + " CTRL" +
                (app.controllerConnected ? (app.controllerError ? "!" : "+") : "-");
  if (app.settings.controllerBatteryPercent >= 0) {
    text += " NK:" + String(app.settings.controllerBatteryPercent) + "%";
  }
  bool cliBusy = !commandQueue.empty() || patternSyncInProgress;
  String queueText = "Q:" + String(commandQueue.size());
  uint16_t queueColor = cliBusy ? COLOR_WARN : COLOR_MUTED;
  drawTextFit(text, 3, 4, 96, app.controllerError ? COLOR_WARN : COLOR_TEXT, COLOR_PANEL_DARK);
  drawTextFit(queueText, 103, 4, 34, queueColor, COLOR_PANEL_DARK);
  drawTextFit(String(app.selectedCard + 1) + "/" + String(CARD_COUNT), 160, 4, 28, COLOR_MUTED, COLOR_PANEL_DARK);
  drawTextFit(String("CP:") + cp, 194, 4, 43, app.cardputerCharging ? COLOR_OK : COLOR_ACCENT, COLOR_PANEL_DARK);
}

void drawFooter(const String& help)
{
  auto& d = uiCanvas;
  d.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, COLOR_PANEL);
  drawTextFit(help, 3, SCREEN_H - 9, SCREEN_W - 6, COLOR_MUTED, COLOR_PANEL);
}

void drawTitle(const String& title)
{
  drawTextFit(title, 8, CONTENT_Y + 8, 160, COLOR_MUTED);
}

void drawBigValue(const String& value, int y, uint16_t color = COLOR_ACCENT)
{
  auto& d = uiCanvas;
  d.setFont(&fonts::Font4);
  d.setTextSize(1);
  d.setTextColor(color, COLOR_BG);
  d.drawString(shortText(value, 8), 8, y);
  d.setFont(&fonts::Font0);
}

void drawStatusTile(int x, int y, int w, int h, const String& label, const String& value, uint16_t color)
{
  auto& d = uiCanvas;
  d.fillRoundRect(x, y, w, h, 3, COLOR_PANEL_DARK);
  d.drawFastHLine(x + 3, y + 2, w - 6, COLOR_PANEL_LIGHT);
  d.setTextColor(COLOR_MUTED, COLOR_PANEL_DARK);
  drawTextFit(label, x + 5, y + 5, w - 10, COLOR_MUTED, COLOR_PANEL_DARK);
  d.setTextColor(color, COLOR_PANEL_DARK);
  drawTextFit(value, x + 5, y + h - 11, w - 10, color, COLOR_PANEL_DARK);
}

void drawStatusCard()
{
  drawTitle("NightKite Link");
  drawStatusTile(6, CONTENT_Y + 25, 53, 27, "USB", app.usbConnected ? "OK" : "--", app.usbConnected ? COLOR_OK : COLOR_WARN);
  drawStatusTile(64, CONTENT_Y + 25, 53, 27, "CTRL", app.controllerConnected ? (app.controllerError ? "ERR" : "OK") : "--",
                 app.controllerError ? COLOR_ERR : (app.controllerConnected ? COLOR_OK : COLOR_WARN));
  drawStatusTile(122, CONTENT_Y + 25, 53, 27, "Bright", showInt(app.settings.brightness), COLOR_ACCENT);
  drawStatusTile(181, CONTENT_Y + 25, 53, 27, "Pattern", showInt(app.settings.activePattern), COLOR_ACCENT);

  drawStatusTile(6, CONTENT_Y + 58, 53, 27, "Strip", showInt(app.settings.stripLength), COLOR_TEXT);
  drawStatusTile(64, CONTENT_Y + 58, 53, 27, "Smooth", showInt(app.settings.smoothing), COLOR_TEXT);
  drawStatusTile(122, CONTENT_Y + 58, 53, 27, "Auto", app.settings.autoplayEnabled ? "ON" : "OFF",
                 app.settings.autoplayEnabled ? COLOR_OK : COLOR_MUTED);
  drawStatusTile(181, CONTENT_Y + 58, 53, 27, "Int", showInt(app.settings.autoplayIntervalSeconds) + "s", COLOR_TEXT);
  drawFooter("A/D cards  R refresh");
}

void drawValueCard(const String& title, const String& value, const String& sub, const String& help)
{
  drawTitle(title);
  drawBigValue(value, CONTENT_Y + 28);
  if (sub.length() > 0) {
    drawTextFit(sub, 10, CONTENT_Y + 74, 218, COLOR_TEXT);
  }
  drawFooter(help);
}

void drawBrightnessCard()
{
  int value = editValue.length() ? editValue.toInt() : app.settings.brightness;
  drawTitle("Brightness");
  auto& d = uiCanvas;
  d.setFont(&fonts::Font4);
  d.setTextSize(1);
  d.setTextColor(COLOR_ACCENT, COLOR_BG);
  d.drawString(showInt(value), 8, CONTENT_Y + 27);
  d.setFont(&fonts::Font2);
  d.setTextColor(COLOR_MUTED, COLOR_BG);
  d.drawString("/255", 92, CONTENT_Y + 38);
  d.setFont(&fonts::Font0);
  drawBar(10, CONTENT_Y + 75, 150, 9, value, 0, 255, COLOR_ACCENT);
  drawFooter("W/S change + send");
}

void drawPatternCard()
{
  int value = editValue.length() ? editValue.toInt() : app.settings.activePattern;
  ensurePatternModel();
  bool cycle = value >= 1 && value <= PATTERN_COUNT ? app.settings.patterns[value - 1].cycleEnabled : false;
  bool inverted = value >= 1 && value <= PATTERN_COUNT ? app.settings.patterns[value - 1].inverted : false;
  drawTitle("Pattern");
  char num[8];
  snprintf(num, sizeof(num), "%02d", value > 0 ? value : 0);
  drawBigValue(value > 0 ? String(num) : "--", CONTENT_Y + 27);
  drawTextFit(patternName(value), 10, CONTENT_Y + 69, 145, COLOR_TEXT);
  drawTextFit(String("Cycle ") + (cycle ? "ON" : "OFF"), 158, CONTENT_Y + 57, 72, cycle ? COLOR_OK : COLOR_MUTED);
  drawTextFit(String("Inv ") + (inverted ? "ON" : "OFF"), 158, CONTENT_Y + 72, 72, inverted ? COLOR_WARN : COLOR_MUTED);
  drawFooter("W/S send  C cycle  I invert");
}

void drawConfigCard()
{
  const char* labels[] = {"Strip", "Smooth", "Accel", "Gyro", "Auto", "Interval"};
  String values[] = {
      showInt(draftStripLength) + " LED",
      showInt(draftSmoothing),
      showInt(draftAccelRange) + "g",
      showInt(draftGyroRange),
      draftAutoplayEnabled ? "ON" : "OFF",
      showInt(draftAutoplayIntervalSeconds) + " s",
  };
  drawTextFit("Config", 8, CONTENT_Y + 6, 120, COLOR_MUTED);
  for (int i = 0; i < 6; ++i) {
    int x = 8 + (i % 3) * 76;
    int y = CONTENT_Y + 25 + (i / 3) * 34;
    int w = 70;
    bool active = i == app.selectedConfigField;
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(x, y, w, 28, 3, bg);
    drawTextFit(labels[i], x + 5, y + 6, w - 10, COLOR_MUTED, bg);
    drawTextFit(values[i], x + 5, y + 18, w - 10, active ? COLOR_TEXT : COLOR_ACCENT, bg);
  }
  drawFooter("C field  W/S edit  ENTER set");
}

const char* const CAL_ACTIONS[] = {"Refresh FPS", "Quick calib", "Precise calib", "Boot quick/off"};
constexpr int CAL_ACTION_COUNT = sizeof(CAL_ACTIONS) / sizeof(CAL_ACTIONS[0]);

void drawCalibrationCard()
{
  drawTextFit("Motion Service", 8, CONTENT_Y + 5, 140, COLOR_MUTED);
  drawStatusTile(8, CONTENT_Y + 23, 68, 28, "FPS", app.settings.fps, COLOR_ACCENT);
  drawStatusTile(84, CONTENT_Y + 23, 68, 28, "Boot", app.settings.bootCalibration, COLOR_TEXT);
  drawStatusTile(160, CONTENT_Y + 23, 68, 28, "Pattern", showInt(app.settings.activePattern), COLOR_TEXT);

  for (int i = 0; i < CAL_ACTION_COUNT; ++i) {
    int x = i < 2 ? 8 + i * 116 : 8 + (i - 2) * 116;
    int y = i < 2 ? CONTENT_Y + 58 : CONTENT_Y + 78;
    bool active = i == app.selectedCalAction;
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(x, y, 108, 16, 2, bg);
    drawTextFit(String(active ? "> " : "  ") + CAL_ACTIONS[i], x + 5, y + 5, 98, active ? COLOR_TEXT : COLOR_MUTED, bg);
  }
  drawFooter("W/S select  ENTER run");
}

void drawPatternListCard()
{
  ensurePatternModel();
  drawTextFit("Patterns", 8, CONTENT_Y + 4, 110, COLOR_MUTED);
  int rowH = 20;
  int visible = 4;
  int start = 0;
  if (app.selectedPatternIndex >= visible) {
    start = app.selectedPatternIndex - visible + 1;
  }
  for (int row = 0; row < visible && start + row < PATTERN_COUNT; ++row) {
    int idx = start + row;
    const auto& pattern = app.settings.patterns[idx];
    int y = CONTENT_Y + 17 + row * rowH;
    bool active = idx == app.selectedPatternIndex;
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(5, y, SCREEN_W - 10, rowH - 2, 2, bg);
    char line[40];
    snprintf(line, sizeof(line), "%02d %c %c %s", pattern.id, pattern.cycleEnabled ? '+' : '-',
             pattern.inverted ? 'I' : 'N', pattern.name.c_str());
    uiCanvas.setFont(&fonts::Font2);
    uiCanvas.setTextSize(1);
    drawTextFit(active ? String("> ") + line : String("  ") + line, 10, y + 3, 220, active ? COLOR_TEXT : COLOR_MUTED, bg);
    uiCanvas.setFont(&fonts::Font0);
  }
  drawFooter("W/S scroll  C cycle  I invert  R read");
}

const char* const BULK_ACTIONS[] = {"Save all states", "Enable all cycle", "Disable all cycle", "Invert all", "Normal all"};
constexpr int BULK_ACTION_COUNT = sizeof(BULK_ACTIONS) / sizeof(BULK_ACTIONS[0]);

void drawBulkCard()
{
  drawTextFit("Bulk Actions", 8, CONTENT_Y + 4, 160, COLOR_MUTED);
  for (int i = 0; i < BULK_ACTION_COUNT; ++i) {
    int y = CONTENT_Y + 18 + i * 17;
    bool active = i == app.selectedBulkAction;
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(8, y, 216, 15, 2, bg);
    drawTextFit(String(active ? "> " : "  ") + BULK_ACTIONS[i], 14, y + 5, 204, active ? COLOR_TEXT : COLOR_MUTED, bg);
  }
  drawFooter("W/S select  ENTER confirm");
}

const char* const FIRMWARE_TARGETS[] = {"RP2040", "RP2350"};
constexpr int FIRMWARE_TARGET_COUNT = sizeof(FIRMWARE_TARGETS) / sizeof(FIRMWARE_TARGETS[0]);

String selectedFirmwarePath()
{
  if (app.selectedFirmwareFileIndex < 0 || app.selectedFirmwareFileIndex >= static_cast<int>(app.firmwareFiles.size())) {
    return "";
  }
  return "/firmware/" + app.firmwareFiles[app.selectedFirmwareFileIndex];
}

void refreshFirmwareList()
{
  app.firmwareFiles.clear();
  if (!ensureSdReady()) {
    app.dirty = true;
    return;
  }
  SD.mkdir("/firmware");
  File dir = SD.open("/firmware");
  if (!dir) {
    setStatus("Firmware dir missing", COLOR_WARN);
    app.dirty = true;
    return;
  }
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    String name = entry.name();
    entry.close();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) {
      name = name.substring(slash + 1);
    }
    String lower = name;
    lower.toLowerCase();
    if (lower.endsWith(".uf2")) {
      app.firmwareFiles.push_back(name);
    }
  }
  dir.close();
  app.selectedFirmwareFileIndex = constrain(app.selectedFirmwareFileIndex, 0, max(0, static_cast<int>(app.firmwareFiles.size()) - 1));
  setStatus("UF2 files: " + String(app.firmwareFiles.size()), app.firmwareFiles.empty() ? COLOR_WARN : COLOR_OK);
  app.dirty = true;
}

void drawFirmwareCard()
{
  drawTextFit("Firmware Flash", 8, CONTENT_Y + 5, 130, COLOR_MUTED);
  drawTextFit(app.sdReady ? "SD OK" : "SD --", 178, CONTENT_Y + 5, 50, app.sdReady ? COLOR_OK : COLOR_WARN);

  uiCanvas.fillRoundRect(8, CONTENT_Y + 22, 224, 18, 3, COLOR_PANEL_DARK);
  drawTextFit(String("Target: ") + FIRMWARE_TARGETS[app.selectedFirmwareTarget], 14, CONTENT_Y + 28, 130, COLOR_ACCENT, COLOR_PANEL_DARK);
  drawTextFit("C toggle", 172, CONTENT_Y + 28, 52, COLOR_MUTED, COLOR_PANEL_DARK);

  int visible = 3;
  int start = 0;
  if (app.selectedFirmwareFileIndex >= visible) {
    start = app.selectedFirmwareFileIndex - visible + 1;
  }
  for (int row = 0; row < visible && start + row < static_cast<int>(app.firmwareFiles.size()); ++row) {
    int idx = start + row;
    int y = CONTENT_Y + 46 + row * 18;
    bool active = idx == app.selectedFirmwareFileIndex;
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(8, y, 224, 16, 2, bg);
    drawTextFit(String(active ? "> " : "  ") + app.firmwareFiles[idx], 14, y + 5, 210, active ? COLOR_TEXT : COLOR_MUTED, bg);
  }
  if (app.firmwareFiles.empty()) {
    drawTextFit("No UF2 in /firmware", 12, CONTENT_Y + 52, 200, COLOR_WARN);
  }
  drawFooter("W/S file  C target  R scan  ENTER flash");
}

bool flashWorkflowActive()
{
  return app.flash.state != FlashUiState::Idle;
}

bool flashWorkflowBusy()
{
  return app.flash.busy || uf2Flasher.isRunning();
}

void setFlashState(FlashUiState state, const String& message = "")
{
  app.flash.state = state;
  app.flash.message = message;
  app.flash.stateStartedMs = millis();
  app.flash.lastAnimationMs = 0;
  app.dirty = true;
}

String selectedFirmwareName()
{
  if (app.selectedFirmwareFileIndex < 0 || app.selectedFirmwareFileIndex >= static_cast<int>(app.firmwareFiles.size())) {
    return "";
  }
  return app.firmwareFiles[app.selectedFirmwareFileIndex];
}

String bytesKb(size_t bytes)
{
  return String(static_cast<unsigned>((bytes + 1023) / 1024));
}

void drawSpinner(int x, int y, uint16_t color)
{
  const char frames[] = {'|', '/', '-', '\\'};
  uiCanvas.setTextColor(color, COLOR_BG);
  uiCanvas.drawChar(frames[app.flash.spinner % 4], x, y);
}

void drawProgressBar(int x, int y, int w, int h, int percent)
{
  uiCanvas.fillRoundRect(x, y, w, h, 2, COLOR_PANEL_DARK);
  uiCanvas.drawRoundRect(x, y, w, h, 2, COLOR_PANEL_LIGHT);
  int fillW = map(constrain(percent, 0, 100), 0, 100, 0, w - 2);
  uiCanvas.fillRoundRect(x + 1, y + 1, fillW, h - 2, 1, COLOR_OK);
}

void drawFlashWorkflow()
{
  drawTextFit("NightKite Link", 8, CONTENT_Y + 5, 150, COLOR_MUTED);
  switch (app.flash.state) {
    case FlashUiState::Idle:
      return;
    case FlashUiState::Confirm:
      drawTextFit("Flash firmware?", 16, CONTENT_Y + 28, 180, COLOR_WARN);
      drawTextFit(shortText(app.flash.filename, 30), 16, CONTENT_Y + 50, 210, COLOR_TEXT);
      drawTextFit(String("Target: ") + app.flash.target, 16, CONTENT_Y + 67, 150, COLOR_ACCENT);
      drawFooter("ENTER start  ESC cancel");
      break;
    case FlashUiState::WaitingForBootsel:
      drawTextFit("Firmware Update", 12, CONTENT_Y + 21, 180, COLOR_ACCENT);
      drawTextFit("Put controller into", 12, CONTENT_Y + 41, 190, COLOR_TEXT);
      drawTextFit("BOOTSEL mode", 12, CONTENT_Y + 56, 190, COLOR_TEXT);
      drawTextFit("Hold BOOTSEL, reconnect USB", 12, CONTENT_Y + 76, 215, COLOR_MUTED);
      drawFooter("ENTER continue  ESC cancel");
      break;
    case FlashUiState::WaitingForMassStorage:
      drawTextFit("Waiting for", 20, CONTENT_Y + 32, 160, COLOR_TEXT);
      drawTextFit(app.flash.target + " drive...", 20, CONTENT_Y + 49, 180, COLOR_ACCENT);
      drawSpinner(112, CONTENT_Y + 76, COLOR_WARN);
      drawFooter("ESC cancel");
      break;
    case FlashUiState::MassStorageConnected:
      drawTextFit(app.flash.target + " detected", 18, CONTENT_Y + 34, 190, COLOR_OK);
      drawTextFit("Mass Storage ready", 18, CONTENT_Y + 54, 190, COLOR_TEXT);
      drawTextFit("Preparing flash...", 18, CONTENT_Y + 74, 190, COLOR_MUTED);
      drawFooter("Do not unplug");
      break;
    case FlashUiState::CopyingUf2:
      drawTextFit("Copying firmware", 12, CONTENT_Y + 21, 180, COLOR_TEXT);
      drawProgressBar(12, CONTENT_Y + 43, 160, 13, app.flash.percent);
      drawTextFit(String(app.flash.percent) + "%", 183, CONTENT_Y + 46, 45, COLOR_OK);
      drawTextFit(bytesKb(app.flash.copiedBytes) + " / " + bytesKb(app.flash.totalBytes) + " KB", 12, CONTENT_Y + 65, 150,
                  COLOR_MUTED);
      drawTextFit(shortText(app.flash.filename, 32), 12, CONTENT_Y + 80, 210, COLOR_TEXT);
      drawFooter("Do not unplug");
      break;
    case FlashUiState::CopyComplete:
      drawTextFit("Firmware copied", 18, CONTENT_Y + 36, 180, COLOR_OK);
      drawTextFit("Closing drive...", 18, CONTENT_Y + 59, 180, COLOR_MUTED);
      drawFooter("Please wait");
      break;
    case FlashUiState::WaitingForReboot:
      drawTextFit("Firmware copied", 18, CONTENT_Y + 28, 180, COLOR_OK);
      drawTextFit("Controller is", 18, CONTENT_Y + 49, 180, COLOR_TEXT);
      drawTextFit("flashing/rebooting", 18, CONTENT_Y + 64, 180, COLOR_TEXT);
      drawSpinner(190, CONTENT_Y + 64, COLOR_WARN);
      drawFooter("Please wait...");
      break;
    case FlashUiState::Success:
      drawTextFit("Flash complete", 22, CONTENT_Y + 38, 190, COLOR_OK);
      drawTextFit("Controller restarted", 22, CONTENT_Y + 60, 190, COLOR_TEXT);
      drawFooter("ENTER continue");
      break;
    case FlashUiState::Error:
      drawTextFit("Flash failed", 18, CONTENT_Y + 31, 180, COLOR_ERR);
      drawTextFit(shortText(app.flash.errorMessage, 32), 18, CONTENT_Y + 54, 205, COLOR_TEXT);
      drawFooter("ENTER retry  ESC back");
      break;
  }
}

const char* const PROFILE_ACTIONS[] = {"Init SD", "Save current", "Apply loaded"};
constexpr int PROFILE_ACTION_COUNT = sizeof(PROFILE_ACTIONS) / sizeof(PROFILE_ACTIONS[0]);

String profileDisplayName(const String& fileName)
{
  String name = fileName;
  int slash = name.lastIndexOf('/');
  if (slash >= 0) {
    name = name.substring(slash + 1);
  }
  if (name.endsWith(".json")) {
    name = name.substring(0, name.length() - 5);
  }
  return name;
}

void drawProfilesCard()
{
  drawTextFit("Profiles", 8, CONTENT_Y + 5, 92, COLOR_MUTED);
  uiCanvas.fillRoundRect(174, CONTENT_Y + 3, 58, 13, 2, app.sdReady ? COLOR_PANEL_DARK : COLOR_PANEL);
  drawTextFit(app.sdReady ? "SD OK" : "SD --", 179, CONTENT_Y + 7, 48, app.sdReady ? COLOR_OK : COLOR_WARN,
              app.sdReady ? COLOR_PANEL_DARK : COLOR_PANEL);
  drawTextFit(String("Loaded: ") + (app.hasLoadedProfile ? shortText(app.loadedProfileName, 20) : "none"), 8,
              CONTENT_Y + 20, 220, app.hasLoadedProfile ? COLOR_ACCENT : COLOR_MUTED);
  int total = PROFILE_ACTION_COUNT + static_cast<int>(app.profileFiles.size());
  int visible = 3;
  int start = 0;
  if (app.selectedProfileAction >= visible) {
    start = app.selectedProfileAction - visible + 1;
  }
  for (int row = 0; row < visible && start + row < total; ++row) {
    int i = start + row;
    int y = CONTENT_Y + 40 + row * 20;
    bool active = i == app.selectedProfileAction;
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(8, y, 224, 18, 3, bg);
    bool action = i < PROFILE_ACTION_COUNT;
    String label = action ? String(PROFILE_ACTIONS[i]) : shortText(app.profileFiles[i - PROFILE_ACTION_COUNT], 22);
    String prefix = active ? "> " : "  ";
    if (!action) {
      String path = "/profiles/" + app.profileFiles[i - PROFILE_ACTION_COUNT];
      if (app.hasLoadedProfile && app.loadedProfilePath == path) {
        prefix = active ? ">*" : " *";
      }
    }
    drawTextFit(prefix + label, 14, y + 6, action ? 132 : 168, active ? COLOR_TEXT : COLOR_MUTED, bg);
    if (!action) {
      drawTextFit("ENT load", 177, y + 6, 50, COLOR_ACCENT, bg);
    }
  }
  drawFooter("W/S select  ENTER  I delete");
}

void drawProfileNameInput()
{
  drawTextFit("Save Profile", 10, CONTENT_Y + 12, 160, COLOR_MUTED);
  drawTextFit("Name:", 12, CONTENT_Y + 34, 70, COLOR_TEXT);
  uiCanvas.fillRoundRect(12, CONTENT_Y + 51, 216, 21, 3, COLOR_PANEL_DARK);
  drawTextFit(shortText(app.profileNameInput + "_", 32), 18, CONTENT_Y + 58, 204, COLOR_ACCENT, COLOR_PANEL_DARK);
  drawFooter("Type name  ENTER save  DEL delete");
}

void drawConfirmProfileOverwrite()
{
  drawTextFit("Profile exists", 14, CONTENT_Y + 25, 180, COLOR_WARN);
  drawTextFit(shortText(profileDisplayName(app.pendingProfileName), 28), 14, CONTENT_Y + 47, 210, COLOR_TEXT);
  drawTextFit("Overwrite?", 14, CONTENT_Y + 68, 140, COLOR_WARN);
  drawFooter("ENTER yes  ESC no");
}

void drawConfirmProfileApply()
{
  drawTextFit("Apply profile?", 14, CONTENT_Y + 28, 170, COLOR_WARN);
  drawTextFit(shortText(app.loadedProfileName, 30), 14, CONTENT_Y + 51, 210, COLOR_TEXT);
  drawTextFit("Send to controller", 14, CONTENT_Y + 72, 170, COLOR_MUTED);
  drawFooter("ENTER apply  ESC cancel");
}

void drawPatternDetail()
{
  ensurePatternModel();
  const auto& pattern = app.settings.patterns[app.selectedPatternIndex];
  drawTextFit("Pattern " + String(pattern.id), 8, CONTENT_Y + 8, 100, COLOR_MUTED);
  drawTextFit(pattern.name, 8, CONTENT_Y + 24, 220, COLOR_ACCENT);
  drawTextFit(String("Cycle: ") + (detailCycle ? "ON" : "OFF"), 12, CONTENT_Y + 50, 150, detailCycle ? COLOR_OK : COLOR_MUTED);
  drawTextFit(String("Invert: ") + (detailInvert ? "ON" : "OFF"), 12, CONTENT_Y + 68, 150, detailInvert ? COLOR_WARN : COLOR_MUTED);
  drawFooter("C cycle  I invert  ENTER apply  ESC back");
}

void drawConfirmBulk()
{
  drawTextFit(String(BULK_ACTIONS[app.selectedBulkAction]) + "?", 12, CONTENT_Y + 34, 220, COLOR_WARN);
  drawTextFit("ENTER yes", 12, CONTENT_Y + 58, 120, COLOR_TEXT);
  drawTextFit("ESC cancel", 12, CONTENT_Y + 74, 120, COLOR_MUTED);
  drawFooter("Confirm bulk action");
}

void drawConfirmProfileDelete()
{
  int profileIndex = app.selectedProfileAction - PROFILE_ACTION_COUNT;
  String name = profileIndex >= 0 && profileIndex < static_cast<int>(app.profileFiles.size()) ? app.profileFiles[profileIndex] : "";
  drawTextFit("Delete profile?", 12, CONTENT_Y + 30, 180, COLOR_WARN);
  drawTextFit(shortText(name, 30), 12, CONTENT_Y + 50, 210, COLOR_TEXT);
  drawTextFit("ENTER delete", 12, CONTENT_Y + 72, 120, COLOR_ERR);
  drawTextFit("ESC cancel", 130, CONTENT_Y + 72, 100, COLOR_MUTED);
  drawFooter("Confirm delete");
}

bool profileModalActive()
{
  return mode == Mode::ProfileNameInput || mode == Mode::ConfirmProfileOverwrite ||
         mode == Mode::ConfirmProfileApply;
}

void render()
{
  if (!app.dirty) {
    return;
  }

  auto& d = uiCanvas;
  d.fillScreen(COLOR_BG);
  d.setTextSize(1);
  d.setFont(&fonts::Font0);
  d.setTextDatum(top_left);
  drawStatusBar();

  if (flashWorkflowActive()) {
    drawFlashWorkflow();
  } else if (mode == Mode::PatternDetail) {
    drawPatternDetail();
  } else if (mode == Mode::ConfirmBulk) {
    drawConfirmBulk();
  } else if (mode == Mode::ConfirmProfileDelete) {
    drawConfirmProfileDelete();
  } else if (mode == Mode::ProfileNameInput) {
    drawProfileNameInput();
  } else if (mode == Mode::ConfirmProfileOverwrite) {
    drawConfirmProfileOverwrite();
  } else if (mode == Mode::ConfirmProfileApply) {
    drawConfirmProfileApply();
  } else {
    switch (static_cast<Card>(app.selectedCard)) {
      case Card::Status:
        drawStatusCard();
        break;
      case Card::Brightness:
        drawBrightnessCard();
        break;
      case Card::Config:
        drawConfigCard();
        break;
      case Card::Calibration:
        drawCalibrationCard();
        break;
      case Card::ActivePattern:
        drawPatternCard();
        break;
      case Card::PatternList:
        drawPatternListCard();
        break;
      case Card::PatternBulk:
        drawBulkCard();
        break;
      case Card::Firmware:
        drawFirmwareCard();
        break;
      case Card::Profiles:
        drawProfilesCard();
        break;
    }
  }
  if (canvasReady) {
    uiCanvas.pushSprite(0, 0);
  }
  app.dirty = false;
}

bool ensureSdReady()
{
  if (app.sdReady) {
    return true;
  }

  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    setStatus("SD init failed", COLOR_ERR);
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    setStatus("No SD card", COLOR_ERR);
    return false;
  }
  SD.mkdir("/profiles");
  app.sdReady = true;
  setStatus("SD ready", COLOR_OK);
  return true;
}

void refreshProfileList()
{
  app.profileFiles.clear();
  if (!ensureSdReady()) {
    app.dirty = true;
    return;
  }
  File dir = SD.open("/profiles");
  if (!dir) {
    setStatus("Profile dir missing", COLOR_WARN);
    app.dirty = true;
    return;
  }
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    String name = entry.name();
    entry.close();
    if (name.endsWith(".json")) {
      int slash = name.lastIndexOf('/');
      if (slash >= 0) {
        name = name.substring(slash + 1);
      }
      app.profileFiles.push_back(name);
    }
  }
  dir.close();
  int maxIndex = PROFILE_ACTION_COUNT + static_cast<int>(app.profileFiles.size()) - 1;
  app.selectedProfileAction = constrain(app.selectedProfileAction, 0, max(0, maxIndex));
  setStatus("Profiles: " + String(app.profileFiles.size()), COLOR_MUTED);
  app.dirty = true;
}

String sanitizeProfileBaseName(String name)
{
  name.trim();
  String out;
  for (size_t i = 0; i < name.length() && out.length() < 40; ++i) {
    char c = name[i];
    if (c == ' ') {
      out += '_';
    } else if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
      out += c;
    } else if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
               c == '|') {
      out += '_';
    }
  }
  while (out.startsWith("_") || out.startsWith(".")) {
    out = out.substring(1);
  }
  while (out.endsWith("_") || out.endsWith(".")) {
    out = out.substring(0, out.length() - 1);
  }
  return out;
}

String profilePathForName(const String& name)
{
  String base = sanitizeProfileBaseName(name);
  if (base.length() == 0) {
    return "";
  }
  if (!base.endsWith(".json")) {
    base += ".json";
  }
  return "/profiles/" + base;
}

void writeJsonString(File& file, const String& value)
{
  file.print('"');
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      file.print('\\');
    }
    file.print(c);
  }
  file.print('"');
}

bool saveCurrentProfileToPath(const String& path, const String& displayName, bool overwrite)
{
  if (path.length() == 0) {
    setStatus("Invalid name", COLOR_ERR);
    return false;
  }
  if (!ensureSdReady()) {
    return false;
  }
  if (SD.exists(path)) {
    if (!overwrite) {
      setStatus("Profile exists", COLOR_WARN);
      return false;
    }
    if (!SD.remove(path)) {
      setStatus("Overwrite failed", COLOR_ERR);
      return false;
    }
  }
  setStatus("Saving profile...", COLOR_ACCENT);
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    setStatus("Profile open failed", COLOR_ERR);
    return false;
  }

  ensurePatternModel();
  file.println("{");
  file.println("  \"profile_version\": 1,");
  file.println("  \"project\": \"NightKite Link\",");
  file.println("  \"target\": \"NightKite Multi\",");
  file.println("  \"settings\": {");
  file.printf("    \"brightness\": %d,\n", app.settings.brightness);
  file.printf("    \"strip_length\": %d,\n", app.settings.stripLength);
  file.printf("    \"active_pattern\": %d,\n", app.settings.activePattern);
  file.printf("    \"smoothing\": %d,\n", app.settings.smoothing);
  file.printf("    \"accel_range\": %d,\n", app.settings.accelRange);
  file.printf("    \"gyro_range\": %d,\n", app.settings.gyroRange);
  file.printf("    \"enabled_pattern_mask\": %lu,\n", static_cast<unsigned long>(currentEnabledMask()));
  file.printf("    \"inverted_pattern_mask\": %lu,\n", static_cast<unsigned long>(currentInvertedMask()));
  file.println("    \"autoplay\": {");
  file.printf("      \"enabled\": %s,\n", app.settings.autoplayEnabled ? "true" : "false");
  file.printf("      \"interval_seconds\": %d\n", app.settings.autoplayIntervalSeconds);
  file.println("    },");
  file.println("    \"patterns\": [");
  for (size_t i = 0; i < app.settings.patterns.size(); ++i) {
    const auto& pattern = app.settings.patterns[i];
    file.print("      {\"id\": ");
    file.print(pattern.id);
    file.print(", \"name\": ");
    writeJsonString(file, pattern.name);
    file.print(", \"cycle_enabled\": ");
    file.print(pattern.cycleEnabled ? "true" : "false");
    file.print(", \"inverted\": ");
    file.print(pattern.inverted ? "true" : "false");
    file.print("}");
    file.println(i + 1 < app.settings.patterns.size() ? "," : "");
  }
  file.println("    ]");
  file.println("  }");
  file.println("}");
  file.close();
  app.loadedProfile = app.settings;
  app.hasLoadedProfile = true;
  app.loadedProfileName = displayName.length() > 0 ? displayName : profileDisplayName(path);
  app.loadedProfilePath = path;
  setStatus("Profile saved", COLOR_OK);
  refreshProfileList();
  return true;
}

String newestProfilePath()
{
  if (!ensureSdReady()) {
    return "";
  }
  File dir = SD.open("/profiles");
  if (!dir) {
    return "";
  }
  String newest;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    String name = entry.name();
    entry.close();
    if (name.endsWith(".json") && name > newest) {
      newest = name;
    }
  }
  dir.close();
  if (newest.length() == 0) {
    return "";
  }
  if (!newest.startsWith("/")) {
    newest = "/profiles/" + newest;
  }
  return newest;
}

int jsonInt(const String& json, const char* key, int fallback)
{
  String token = String("\"") + key + "\":";
  int start = json.indexOf(token);
  if (start < 0) {
    return fallback;
  }
  start += token.length();
  while (start < json.length() && isspace(static_cast<unsigned char>(json[start]))) {
    ++start;
  }
  return json.substring(start).toInt();
}

uint32_t jsonUint32(const String& json, const char* key, uint32_t fallback)
{
  String token = String("\"") + key + "\":";
  int start = json.indexOf(token);
  if (start < 0) {
    return fallback;
  }
  start += token.length();
  while (start < json.length() && isspace(static_cast<unsigned char>(json[start]))) {
    ++start;
  }
  return static_cast<uint32_t>(strtoul(json.substring(start).c_str(), nullptr, 10));
}

bool jsonBool(const String& json, const char* key, bool fallback)
{
  String token = String("\"") + key + "\":";
  int start = json.indexOf(token);
  if (start < 0) {
    return fallback;
  }
  start += token.length();
  while (start < json.length() && isspace(static_cast<unsigned char>(json[start]))) {
    ++start;
  }
  return json.substring(start).startsWith("true");
}

bool loadNewestProfile()
{
  String path = newestProfilePath();
  if (path.length() == 0) {
    setStatus("No profile found", COLOR_WARN);
    return false;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    setStatus("Profile read failed", COLOR_ERR);
    return false;
  }
  String json;
  while (file.available()) {
    json += static_cast<char>(file.read());
    if (json.length() > 8192) {
      break;
    }
  }
  file.close();

  ControllerSettings loaded = app.settings;
  loaded.brightness = jsonInt(json, "brightness", loaded.brightness);
  loaded.stripLength = jsonInt(json, "strip_length", loaded.stripLength);
  loaded.activePattern = jsonInt(json, "active_pattern", loaded.activePattern);
  loaded.smoothing = jsonInt(json, "smoothing", loaded.smoothing);
  loaded.accelRange = jsonInt(json, "accel_range", loaded.accelRange);
  loaded.gyroRange = jsonInt(json, "gyro_range", loaded.gyroRange);
  loaded.enabledPatternMask = jsonUint32(json, "enabled_pattern_mask", currentEnabledMask());
  loaded.invertedPatternMask = jsonUint32(json, "inverted_pattern_mask", currentInvertedMask());
  loaded.autoplayEnabled = jsonBool(json, "enabled", loaded.autoplayEnabled);
  loaded.autoplayIntervalSeconds = jsonInt(json, "interval_seconds", loaded.autoplayIntervalSeconds);
  loaded.patterns.clear();
  loaded.patterns.reserve(PATTERN_COUNT);
  for (int id = 1; id <= PATTERN_COUNT; ++id) {
    PatternConfig pattern;
    pattern.id = id;
    pattern.name = patternName(id);
    pattern.cycleEnabled = (loaded.enabledPatternMask & (1UL << (id - 1))) != 0;
    pattern.inverted = (loaded.invertedPatternMask & (1UL << (id - 1))) != 0;
    loaded.patterns.push_back(pattern);
  }
  app.loadedProfile = loaded;
  app.hasLoadedProfile = true;
  app.loadedProfilePath = path;
  app.loadedProfileName = profileDisplayName(path);
  setStatus("Profile loaded", COLOR_OK);
  return true;
}

bool loadProfileFile(const String& fileName)
{
  if (!ensureSdReady()) {
    return false;
  }
  String path = fileName.startsWith("/") ? fileName : "/profiles/" + fileName;
  File file = SD.open(path, FILE_READ);
  if (!file) {
    setStatus("Profile read failed", COLOR_ERR);
    return false;
  }
  String json;
  while (file.available()) {
    json += static_cast<char>(file.read());
    if (json.length() > 8192) {
      break;
    }
  }
  file.close();

  ControllerSettings loaded = app.settings;
  loaded.brightness = jsonInt(json, "brightness", loaded.brightness);
  loaded.stripLength = jsonInt(json, "strip_length", loaded.stripLength);
  loaded.activePattern = jsonInt(json, "active_pattern", loaded.activePattern);
  loaded.smoothing = jsonInt(json, "smoothing", loaded.smoothing);
  loaded.accelRange = jsonInt(json, "accel_range", loaded.accelRange);
  loaded.gyroRange = jsonInt(json, "gyro_range", loaded.gyroRange);
  loaded.enabledPatternMask = jsonUint32(json, "enabled_pattern_mask", currentEnabledMask());
  loaded.invertedPatternMask = jsonUint32(json, "inverted_pattern_mask", currentInvertedMask());
  loaded.autoplayEnabled = jsonBool(json, "enabled", loaded.autoplayEnabled);
  loaded.autoplayIntervalSeconds = jsonInt(json, "interval_seconds", loaded.autoplayIntervalSeconds);
  loaded.patterns.clear();
  loaded.patterns.reserve(PATTERN_COUNT);
  for (int id = 1; id <= PATTERN_COUNT; ++id) {
    PatternConfig pattern;
    pattern.id = id;
    pattern.name = patternName(id);
    pattern.cycleEnabled = (loaded.enabledPatternMask & (1UL << (id - 1))) != 0;
    pattern.inverted = (loaded.invertedPatternMask & (1UL << (id - 1))) != 0;
    loaded.patterns.push_back(pattern);
  }
  app.loadedProfile = loaded;
  app.hasLoadedProfile = true;
  app.loadedProfilePath = path;
  app.loadedProfileName = profileDisplayName(fileName);
  setStatus("Profile loaded", COLOR_OK);
  return true;
}

void applyLoadedProfile()
{
  if (!app.hasLoadedProfile) {
    setStatus("No profile loaded", COLOR_WARN);
    return;
  }
  setStatus("Applying profile...", COLOR_ACCENT);
  sendCommand(NightKiteCommands::setBrightness(app.loadedProfile.brightness));
  sendCommand(NightKiteCommands::setStripLength(app.loadedProfile.stripLength));
  sendCommand(NightKiteCommands::setPattern(app.loadedProfile.activePattern));
  sendCommand(NightKiteCommands::setSmoothing(app.loadedProfile.smoothing));
  sendCommand(NightKiteCommands::setAccelRange(app.loadedProfile.accelRange));
  sendCommand(NightKiteCommands::setGyroRange(app.loadedProfile.gyroRange));
  sendCommand(NightKiteCommands::setAutoplay(app.loadedProfile.autoplayEnabled));
  sendCommand(NightKiteCommands::setAutoplayInterval(app.loadedProfile.autoplayIntervalSeconds));
  String enabledList = patternListFromMask(app.loadedProfile.enabledPatternMask);
  String disabledList = patternListFromMask(ALL_PATTERN_MASK & ~app.loadedProfile.enabledPatternMask);
  if (enabledList.length() > 0) {
    sendCommand(String("enable_pattern ") + enabledList);
  }
  if (disabledList.length() > 0) {
    sendCommand(String("disable_pattern ") + disabledList);
  }
  sendCommand(String("normal_pattern ") + ALL_PATTERN_LIST);
  String invertedList = patternListFromMask(app.loadedProfile.invertedPatternMask);
  if (invertedList.length() > 0) {
    sendCommand(String("invert_pattern ") + invertedList);
  }
  setStatus("Profile applied", COLOR_OK);
}

void startProfileNameInput()
{
  if (!ensureSdReady()) {
    return;
  }
  app.profileNameInput = app.hasLoadedProfile ? app.loadedProfileName : "";
  app.pendingProfileName = "";
  app.pendingProfilePath = "";
  mode = Mode::ProfileNameInput;
  app.dirty = true;
}

void submitProfileNameInput()
{
  String base = sanitizeProfileBaseName(app.profileNameInput);
  if (base.length() == 0) {
    setStatus("Name required", COLOR_WARN);
    app.dirty = true;
    return;
  }
  String path = profilePathForName(base);
  if (path.length() == 0) {
    setStatus("Invalid name", COLOR_ERR);
    app.dirty = true;
    return;
  }
  app.pendingProfileName = profileDisplayName(base);
  app.pendingProfilePath = path;
  if (SD.exists(path)) {
    setStatus("Profile exists", COLOR_WARN);
    mode = Mode::ConfirmProfileOverwrite;
    app.dirty = true;
    return;
  }
  saveCurrentProfileToPath(path, app.pendingProfileName, false);
  mode = Mode::Cards;
  app.dirty = true;
}

void confirmProfileOverwrite()
{
  if (app.pendingProfilePath.length() == 0 || app.pendingProfileName.length() == 0) {
    setStatus("Invalid name", COLOR_ERR);
    mode = Mode::ProfileNameInput;
    app.dirty = true;
    return;
  }
  saveCurrentProfileToPath(app.pendingProfilePath, app.pendingProfileName, true);
  mode = Mode::Cards;
  app.dirty = true;
}

void startApplyLoadedProfile()
{
  if (!app.hasLoadedProfile) {
    setStatus("No profile loaded", COLOR_WARN);
    return;
  }
  mode = Mode::ConfirmProfileApply;
  app.dirty = true;
}

void parsePatternStates(const String& line)
{
  if (app.patternEditsPending && static_cast<Card>(app.selectedCard) == Card::PatternList) {
    return;
  }
  String token = "patterns=";
  int start = line.indexOf(token);
  if (start < 0) {
    return;
  }
  start += token.length();
  ensurePatternModel();
  while (start < line.length()) {
    int comma = line.indexOf(',', start);
    String entry = comma >= 0 ? line.substring(start, comma) : line.substring(start);
    entry.trim();
    int firstColon = entry.indexOf(':');
    int lastColon = entry.lastIndexOf(':');
    if (firstColon > 0 && lastColon > firstColon) {
      int id = entry.substring(0, firstColon).toInt();
      String name = entry.substring(firstColon + 1, lastColon);
      String state = entry.substring(lastColon + 1);
      state.trim();
      if (id >= 1 && id <= PATTERN_COUNT) {
        auto& pattern = app.settings.patterns[id - 1];
        // Keep local names stable; malformed/prompt-tailed CLI output must not leak into the list.
        pattern.name = patternName(id);
        pattern.cycleEnabled = state == "on";
      }
    }
    if (comma < 0) {
      break;
    }
    start = comma + 1;
  }
  app.dirty = true;
}

void parseNightKiteLine(const String& line)
{
  String parsed = stripCliPrompt(line);
  app.lastResponse = parsed;

  if (parsed.startsWith("OK ")) {
    app.controllerConnected = true;
    app.controllerError = false;
    lastRxMs = millis();

    parseIntField(parsed, "pattern", app.settings.activePattern);
    parseIntField(parsed, "brightness", app.settings.brightness);
    parseIntField(parsed, "strip_length", app.settings.stripLength);
    parseIntField(parsed, "smoothing", app.settings.smoothing);
    parseIntField(parsed, "accel_range", app.settings.accelRange);
    parseIntField(parsed, "gyro_range", app.settings.gyroRange);
    parseIntField(parsed, "autoplay_interval", app.settings.autoplayIntervalSeconds);

    String bootCalibration = valueForKey(parsed, "boot_calibration");
    if (bootCalibration.length() > 0) {
      app.settings.bootCalibration = bootCalibration;
    }
    String fps = valueForKey(parsed, "fps");
    if (fps.length() > 0) {
      app.settings.fps = fps;
    }

    String autoplay = valueForKey(parsed, "autoplay");
    if (autoplay.length() > 0) {
      app.settings.autoplayEnabled = parseBoolText(autoplay);
    }

    String enabled = valueForKey(parsed, "enabled_patterns");
    if (enabled.length() > 0) {
      applyPatternMasks(patternMaskFromList(enabled), 0, true, false);
    }
    String inverted = valueForKey(parsed, "inverted_patterns");
    if (inverted.length() > 0) {
      applyPatternMasks(0, patternMaskFromList(inverted), false, true);
    }
    String voltage = valueForKey(parsed, "battery_voltage");
    if (voltage.length() > 0) {
      app.settings.controllerBatteryVoltage = voltage.toFloat();
    }
    parsePatternStates(parsed);
    setStatus(shortText(parsed, 34), COLOR_OK);
  } else if (parsed.startsWith("ERR")) {
    app.controllerConnected = true;
    app.controllerError = true;
    lastRxMs = millis();
    setStatus(shortText(parsed, 34), COLOR_ERR);
  } else if (parsed.startsWith("INFO") || parsed.startsWith("[NightKite CLI]")) {
    app.controllerConnected = true;
    lastRxMs = millis();
    setStatus(shortText(parsed, 34), COLOR_MUTED);
  }
  app.dirty = true;
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
  app.cardputerBatteryPercent = level;
  app.cardputerBatteryVoltage = voltageTextFromMv(voltageMv);
  app.cardputerCharging = charging == m5::Power_Class::is_charging_t::is_charging;
  app.dirty = true;
}

void pollTransport()
{
  bool wasUsbConnected = app.usbConnected;
  app.usbConnected = transport.connected();
  if (app.usbConnected != wasUsbConnected) {
    if (!app.usbConnected) {
      app.controllerConnected = false;
      app.controllerError = true;
      commandQueue.clear();
      setStatus("USB disconnected", COLOR_ERR);
    } else {
      app.controllerError = false;
      setStatus("USB connected", COLOR_OK);
      sendCommand(NightKiteCommands::refreshAll());
    }
    app.dirty = true;
  }

  String line;
  while (transport.readLine(line)) {
    parseNightKiteLine(line);
  }

  if (app.controllerConnected && millis() - lastRxMs > LINK_STALE_MS) {
    app.controllerConnected = false;
    app.controllerError = true;
    setStatus("Controller timeout", COLOR_ERR);
    app.dirty = true;
  }

  if (millis() - lastPollMs > 5000) {
    lastPollMs = millis();
    if (app.usbConnected) {
      sendCommand(NightKiteCommands::refreshAll());
    }
  }
}

void syncEditFromCard()
{
  switch (static_cast<Card>(app.selectedCard)) {
    case Card::Brightness:
      editValue = showInt(app.settings.brightness);
      break;
    case Card::Config:
      editValue = "";
      draftStripLength = app.settings.stripLength;
      draftSmoothing = app.settings.smoothing;
      draftAccelRange = app.settings.accelRange;
      draftGyroRange = app.settings.gyroRange;
      draftAutoplayEnabled = app.settings.autoplayEnabled;
      draftAutoplayIntervalSeconds = app.settings.autoplayIntervalSeconds;
      break;
    case Card::ActivePattern:
      editValue = showInt(app.settings.activePattern);
      break;
    case Card::Calibration:
      editValue = "";
      break;
    default:
      editValue = "";
      break;
  }
}

void changeCard(int delta)
{
  app.selectedCard += delta;
  if (app.selectedCard < 0) {
    app.selectedCard = CARD_COUNT - 1;
  } else if (app.selectedCard >= CARD_COUNT) {
    app.selectedCard = 0;
  }
  mode = Mode::Cards;
  syncEditFromCard();
  if (static_cast<Card>(app.selectedCard) == Card::Profiles && app.profileFiles.empty() && app.sdReady) {
    refreshProfileList();
  }
  if (static_cast<Card>(app.selectedCard) == Card::Firmware && app.firmwareFiles.empty() && app.sdReady) {
    refreshFirmwareList();
  }
  app.dirty = true;
}

void refreshCurrentCard()
{
  if (static_cast<Card>(app.selectedCard) == Card::PatternList || static_cast<Card>(app.selectedCard) == Card::PatternBulk) {
    sendCommand(NightKiteCommands::refreshPatterns());
    sendCommand("get inverted_patterns");
  } else if (static_cast<Card>(app.selectedCard) == Card::Profiles) {
    refreshProfileList();
  } else if (static_cast<Card>(app.selectedCard) == Card::Firmware) {
    refreshFirmwareList();
  } else if (static_cast<Card>(app.selectedCard) == Card::Calibration) {
    sendCommand("timing");
    sendCommand(NightKiteCommands::refreshAll());
  } else {
    sendCommand(NightKiteCommands::refreshAll());
  }
}

void sendPatternState(int id, bool cycle, bool inverted)
{
  if (id < 1 || id > PATTERN_COUNT) {
    return;
  }
  sendCommand(NightKiteCommands::setPatternCycle(id, cycle));
  sendCommand(NightKiteCommands::setPatternInvert(id, inverted));
}

void togglePatternCycle(int patternIndex, bool sendNow)
{
  ensurePatternModel();
  if (patternIndex < 0 || patternIndex >= PATTERN_COUNT) {
    return;
  }
  auto& pattern = app.settings.patterns[patternIndex];
  pattern.cycleEnabled = !pattern.cycleEnabled;
  if (sendNow) {
    sendCommand(NightKiteCommands::setPatternCycle(pattern.id, pattern.cycleEnabled));
  } else {
    setStatus("Cycle changed locally", COLOR_WARN);
    app.patternEditsPending = true;
  }
  app.dirty = true;
}

void togglePatternInvert(int patternIndex, bool sendNow)
{
  ensurePatternModel();
  if (patternIndex < 0 || patternIndex >= PATTERN_COUNT) {
    return;
  }
  auto& pattern = app.settings.patterns[patternIndex];
  pattern.inverted = !pattern.inverted;
  if (sendNow) {
    sendCommand(NightKiteCommands::setPatternInvert(pattern.id, pattern.inverted));
  } else {
    setStatus("Invert changed locally", COLOR_WARN);
    app.patternEditsPending = true;
  }
  app.dirty = true;
}

void startFirmwareFlash();

void changeValue(int delta)
{
  switch (static_cast<Card>(app.selectedCard)) {
    case Card::Brightness:
      editValue = String(wrappedValue(brightnessLevels, BRIGHTNESS_LEVEL_COUNT, editValue.toInt(), delta));
      app.settings.brightness = editValue.toInt();
      sendCommand(NightKiteCommands::setBrightness(app.settings.brightness));
      break;
    case Card::Config:
      if (app.selectedConfigField == 0) {
        draftStripLength = wrapRange(draftStripLength, 10, 35, 1, delta);
      } else if (app.selectedConfigField == 1) {
        draftSmoothing = wrappedValue(smoothingLevels, SMOOTHING_LEVEL_COUNT, draftSmoothing, delta);
      } else if (app.selectedConfigField == 2) {
        draftAccelRange = wrappedValue(accelRangeLevels, ACCEL_RANGE_LEVEL_COUNT, draftAccelRange, delta);
      } else if (app.selectedConfigField == 3) {
        draftGyroRange = wrappedValue(gyroRangeLevels, GYRO_RANGE_LEVEL_COUNT, draftGyroRange, delta);
      } else if (app.selectedConfigField == 4) {
        draftAutoplayEnabled = !draftAutoplayEnabled;
      } else {
        draftAutoplayIntervalSeconds =
            wrappedValue(autoplayIntervalLevels, AUTOPLAY_INTERVAL_LEVEL_COUNT, draftAutoplayIntervalSeconds, delta);
      }
      break;
    case Card::ActivePattern:
      editValue = String(wrapRange(editValue.toInt(), 1, PATTERN_COUNT, 1, delta));
      app.settings.activePattern = editValue.toInt();
      sendCommand(NightKiteCommands::setPattern(app.settings.activePattern));
      break;
    case Card::Calibration:
      app.selectedCalAction = constrain(app.selectedCalAction + delta, 0, CAL_ACTION_COUNT - 1);
      break;
    case Card::PatternList:
      app.selectedPatternIndex = constrain(app.selectedPatternIndex + delta, 0, PATTERN_COUNT - 1);
      break;
    case Card::PatternBulk:
      app.selectedBulkAction = constrain(app.selectedBulkAction + delta, 0, BULK_ACTION_COUNT - 1);
      break;
    case Card::Firmware:
      app.selectedFirmwareFileIndex = constrain(app.selectedFirmwareFileIndex + delta, 0,
                                                max(0, static_cast<int>(app.firmwareFiles.size()) - 1));
      break;
    case Card::Profiles:
      app.selectedProfileAction = constrain(app.selectedProfileAction + delta, 0,
                                            max(0, PROFILE_ACTION_COUNT + static_cast<int>(app.profileFiles.size()) - 1));
      break;
    default:
      break;
  }
  app.dirty = true;
}

void applyCurrentCard()
{
  switch (static_cast<Card>(app.selectedCard)) {
    case Card::Status:
      refreshCurrentCard();
      break;
    case Card::Brightness:
      sendCommand(NightKiteCommands::setBrightness(editValue.toInt()));
      break;
    case Card::Config:
      if (app.selectedConfigField == 0) {
        app.settings.stripLength = draftStripLength;
        sendCommand(NightKiteCommands::setStripLength(draftStripLength));
      } else if (app.selectedConfigField == 1) {
        app.settings.smoothing = draftSmoothing;
        sendCommand(NightKiteCommands::setSmoothing(draftSmoothing));
      } else if (app.selectedConfigField == 2) {
        app.settings.accelRange = draftAccelRange;
        sendCommand(NightKiteCommands::setAccelRange(draftAccelRange));
      } else if (app.selectedConfigField == 3) {
        app.settings.gyroRange = draftGyroRange;
        sendCommand(NightKiteCommands::setGyroRange(draftGyroRange));
      } else if (app.selectedConfigField == 4) {
        app.settings.autoplayEnabled = draftAutoplayEnabled;
        sendCommand(NightKiteCommands::setAutoplay(draftAutoplayEnabled));
      } else {
        app.settings.autoplayIntervalSeconds = draftAutoplayIntervalSeconds;
        sendCommand(NightKiteCommands::setAutoplayInterval(draftAutoplayIntervalSeconds));
      }
      app.dirty = true;
      break;
    case Card::ActivePattern:
      sendCommand(NightKiteCommands::setPattern(editValue.toInt()));
      break;
    case Card::Calibration:
      if (app.selectedCalAction == 0) {
        sendCommand("timing");
      } else if (app.selectedCalAction == 1) {
        sendCommand("calibrate quick");
      } else if (app.selectedCalAction == 2) {
        sendCommand("calibrate precise");
      } else {
        bool nextQuick = app.settings.bootCalibration != "quick";
        app.settings.bootCalibration = nextQuick ? "quick" : "off";
        sendCommand(String("set boot_calibration ") + app.settings.bootCalibration);
      }
      app.dirty = true;
      break;
    case Card::PatternList: {
      ensurePatternModel();
      const auto& pattern = app.settings.patterns[app.selectedPatternIndex];
      detailCycle = pattern.cycleEnabled;
      detailInvert = pattern.inverted;
      mode = Mode::PatternDetail;
      app.dirty = true;
      break;
    }
    case Card::PatternBulk:
      mode = Mode::ConfirmBulk;
      app.dirty = true;
      break;
    case Card::Firmware:
      startFirmwareFlash();
      break;
    case Card::Profiles:
      if (app.selectedProfileAction == 0) {
        ensureSdReady();
        refreshProfileList();
      } else if (app.selectedProfileAction == 1) {
        startProfileNameInput();
      } else if (app.selectedProfileAction == 2) {
        startApplyLoadedProfile();
      } else {
        int profileIndex = app.selectedProfileAction - PROFILE_ACTION_COUNT;
        if (profileIndex >= 0 && profileIndex < static_cast<int>(app.profileFiles.size())) {
          loadProfileFile(app.profileFiles[profileIndex]);
        }
      }
      break;
  }
}

void saveAllPatternStates()
{
  ensurePatternModel();
  for (const auto& pattern : app.settings.patterns) {
    sendCommand(NightKiteCommands::setPatternCycle(pattern.id, pattern.cycleEnabled));
    sendCommand(NightKiteCommands::setPatternInvert(pattern.id, pattern.inverted));
  }
  sendCommand("save");
  patternSyncInProgress = true;
  setStatus("Sending pattern states", COLOR_OK);
}

void startFirmwareFlash()
{
  if (!ensureSdReady()) {
    app.flash.errorMessage = "No SD card";
    setFlashState(FlashUiState::Error);
    return;
  }
  String path = selectedFirmwarePath();
  String name = selectedFirmwareName();
  if (path.length() == 0 || !SD.exists(path)) {
    app.flash.errorMessage = "No UF2 selected";
    setFlashState(FlashUiState::Error);
    return;
  }

  Serial.print("[UF2] validating: ");
  Serial.println(path);
  Uf2ValidationInfo validation = Uf2Validator::validate(path);
  Serial.print("[UF2] validation: ");
  Serial.println(Uf2Validator::message(validation.result));
  if (validation.result != Uf2ValidationResult::Ok) {
    app.flash.errorMessage = Uf2Validator::message(validation.result);
    setFlashState(FlashUiState::Error);
    return;
  }

  app.flash = FlashUiStatus{};
  app.flash.filename = name;
  app.flash.fullPath = path;
  app.flash.target = FIRMWARE_TARGETS[app.selectedFirmwareTarget];
  app.flash.totalBytes = validation.fileSize;
  setStatus("Flash confirm", COLOR_WARN);
  setFlashState(FlashUiState::Confirm);
}

void beginFirmwareFlashAfterBootsel()
{
  if (app.flash.fullPath.length() == 0) {
    app.flash.errorMessage = "No UF2 selected";
    setFlashState(FlashUiState::Error);
    return;
  }
  commandQueue.clear();
  patternSyncInProgress = false;
  app.flash.busy = true;
  setStatus("Flash mode", COLOR_WARN);
  setFlashState(FlashUiState::WaitingForMassStorage);
  if (!uf2Flasher.startFlash(app.flash.fullPath, app.flash.filename)) {
    app.flash.busy = false;
    app.flash.errorMessage = uf2Flasher.resultMessage();
    setFlashState(FlashUiState::Error);
  }
}

void updateFlashWorkflow()
{
  if (!flashWorkflowActive()) {
    return;
  }
  if (millis() - app.flash.lastAnimationMs > 220) {
    app.flash.lastAnimationMs = millis();
    app.flash.spinner = (app.flash.spinner + 1) % 4;
    if (app.flash.state == FlashUiState::WaitingForMassStorage || app.flash.state == FlashUiState::WaitingForReboot) {
      app.dirty = true;
    }
  }

  if (app.flash.busy || uf2Flasher.isRunning()) {
    uf2Flasher.poll();
    const FlashProgress& progress = uf2Flasher.progress();
    app.flash.totalBytes = progress.totalBytes;
    app.flash.copiedBytes = progress.copiedBytes;
    app.flash.percent = progress.percent;
    app.flash.massStorageConnected = progress.massStorageConnected;
    app.flash.message = progress.message;

    if (progress.massStorageConnected && app.flash.state == FlashUiState::WaitingForMassStorage) {
      setFlashState(FlashUiState::MassStorageConnected, "Mass Storage ready");
    }
    if (app.flash.state == FlashUiState::MassStorageConnected && millis() - app.flash.stateStartedMs > 650) {
      setFlashState(FlashUiState::CopyingUf2, "Copying firmware");
    }
    if (progress.copiedBytes > 0 && progress.copiedBytes < progress.totalBytes &&
        app.flash.state != FlashUiState::CopyingUf2) {
      setFlashState(FlashUiState::CopyingUf2, "Copying firmware");
    }
    if (progress.totalBytes > 0 && progress.copiedBytes >= progress.totalBytes &&
        (app.flash.state == FlashUiState::CopyingUf2 || app.flash.state == FlashUiState::MassStorageConnected)) {
      setFlashState(FlashUiState::CopyComplete, "Firmware copied");
    }
    if (app.flash.state == FlashUiState::CopyComplete && millis() - app.flash.stateStartedMs > 650) {
      setFlashState(FlashUiState::WaitingForReboot, "Waiting for reboot");
    }
    if (progress.done) {
      app.flash.busy = false;
      if (progress.success) {
        setStatus("Flash complete", COLOR_OK);
        setFlashState(FlashUiState::Success, "Flash complete");
      } else {
        app.flash.errorMessage = progress.message.length() > 0 ? progress.message : uf2Flasher.resultMessage();
        setStatus(app.flash.errorMessage, COLOR_ERR);
        setFlashState(FlashUiState::Error);
      }
    }
    app.dirty = true;
  }
}

void applyPatternDetail()
{
  ensurePatternModel();
  auto& pattern = app.settings.patterns[app.selectedPatternIndex];
  if (detailCycle != pattern.cycleEnabled) {
    sendCommand(NightKiteCommands::setPatternCycle(pattern.id, detailCycle));
  }
  if (detailInvert != pattern.inverted) {
    sendCommand(NightKiteCommands::setPatternInvert(pattern.id, detailInvert));
  }
  pattern.cycleEnabled = detailCycle;
  pattern.inverted = detailInvert;
  setStatus("Pattern update sent", COLOR_OK);
  mode = Mode::Cards;
  app.dirty = true;
}

void deleteSelectedProfile()
{
  int profileIndex = app.selectedProfileAction - PROFILE_ACTION_COUNT;
  if (profileIndex < 0 || profileIndex >= static_cast<int>(app.profileFiles.size())) {
    setStatus("No profile selected", COLOR_WARN);
    return;
  }
  String fileName = app.profileFiles[profileIndex];
  String path = fileName.startsWith("/") ? fileName : "/profiles/" + fileName;
  if (!ensureSdReady()) {
    return;
  }
  if (SD.remove(path)) {
    if (app.hasLoadedProfile && app.loadedProfilePath == path) {
      app.hasLoadedProfile = false;
      app.loadedProfileName = "";
      app.loadedProfilePath = "";
    }
    setStatus("Deleted " + fileName, COLOR_OK);
    refreshProfileList();
  } else {
    setStatus("Delete failed", COLOR_ERR);
  }
}

void runBulkAction()
{
  switch (app.selectedBulkAction) {
    case 0:
      saveAllPatternStates();
      break;
    case 1:
      sendCommand(NightKiteCommands::setAllCycle(true));
      applyPatternMasks(ALL_PATTERN_MASK, 0, true, false);
      break;
    case 2:
      sendCommand(NightKiteCommands::setAllCycle(false));
      applyPatternMasks(0, 0, true, false);
      break;
    case 3:
      sendCommand(NightKiteCommands::setAllInvert(true));
      applyPatternMasks(0, ALL_PATTERN_MASK, false, true);
      break;
    case 4:
      sendCommand(NightKiteCommands::setAllInvert(false));
      applyPatternMasks(0, 0, false, true);
      break;
  }
  mode = Mode::Cards;
  app.dirty = true;
}

void handleWordChar(char c)
{
  if (flashWorkflowActive()) {
    return;
  }

  if (mode == Mode::ProfileNameInput) {
    if (app.profileNameInput.length() < 40 && c >= 32 && c <= 126) {
      app.profileNameInput += c;
      app.dirty = true;
    }
    return;
  }

  if (mode == Mode::PatternDetail) {
    if (c == 'c' || c == 'C') {
      detailCycle = !detailCycle;
      app.dirty = true;
    } else if (c == 'i' || c == 'I') {
      detailInvert = !detailInvert;
      app.dirty = true;
    }
    return;
  }

  if ((c == 'c' || c == 'C') && static_cast<Card>(app.selectedCard) == Card::Config) {
    app.selectedConfigField = (app.selectedConfigField + 1) % 6;
    app.dirty = true;
    return;
  }
  if ((c == 'c' || c == 'C') && static_cast<Card>(app.selectedCard) == Card::Firmware) {
    app.selectedFirmwareTarget = (app.selectedFirmwareTarget + 1) % FIRMWARE_TARGET_COUNT;
    app.dirty = true;
    return;
  }
  if (c == 'c' || c == 'C') {
    if (static_cast<Card>(app.selectedCard) == Card::ActivePattern) {
      int patternId = editValue.length() ? editValue.toInt() : app.settings.activePattern;
      togglePatternCycle(patternId - 1, true);
      return;
    }
    if (static_cast<Card>(app.selectedCard) == Card::PatternList) {
      togglePatternCycle(app.selectedPatternIndex, false);
      return;
    }
  }
  if (c == 'i' || c == 'I') {
    if (static_cast<Card>(app.selectedCard) == Card::ActivePattern) {
      int patternId = editValue.length() ? editValue.toInt() : app.settings.activePattern;
      togglePatternInvert(patternId - 1, true);
      return;
    }
    if (static_cast<Card>(app.selectedCard) == Card::PatternList) {
      togglePatternInvert(app.selectedPatternIndex, false);
      return;
    }
    if (static_cast<Card>(app.selectedCard) == Card::Profiles &&
        app.selectedProfileAction >= PROFILE_ACTION_COUNT) {
      mode = Mode::ConfirmProfileDelete;
      app.dirty = true;
      return;
    }
  }

  switch (c) {
    case 'a':
    case 'A':
    case ',':
    case '<':
      changeCard(-1);
      break;
    case 'd':
    case 'D':
    case '/':
    case '?':
      changeCard(1);
      break;
    case 'w':
    case 'W':
    case ';':
    case ':':
      changeValue(-1);
      break;
    case 's':
    case 'S':
    case '.':
    case '>':
      changeValue(1);
      break;
    case 'r':
    case 'R':
      refreshCurrentCard();
      break;
    default:
      break;
  }
}

void handleKeyboard()
{
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return;
  }

  auto& status = M5Cardputer.Keyboard.keysState();

  if (flashWorkflowActive()) {
    if (status.enter) {
      if (app.flash.state == FlashUiState::Confirm) {
        setFlashState(FlashUiState::WaitingForBootsel);
      } else if (app.flash.state == FlashUiState::WaitingForBootsel) {
        beginFirmwareFlashAfterBootsel();
      } else if (app.flash.state == FlashUiState::Success) {
        app.flash = FlashUiStatus{};
        setStatus("Ready", COLOR_MUTED);
      } else if (app.flash.state == FlashUiState::Error) {
        setFlashState(FlashUiState::WaitingForBootsel);
      }
      app.dirty = true;
      return;
    }
    if (status.del) {
      if (app.flash.state == FlashUiState::Confirm || app.flash.state == FlashUiState::WaitingForBootsel ||
          app.flash.state == FlashUiState::WaitingForMassStorage || app.flash.state == FlashUiState::Error ||
          app.flash.state == FlashUiState::Success) {
        if (app.flash.state == FlashUiState::WaitingForMassStorage) {
          uf2Flasher.cancel();
        }
        app.flash = FlashUiStatus{};
        setStatus("Flash cancelled", COLOR_WARN);
      }
      return;
    }
    return;
  }

  if (status.enter) {
    if (mode == Mode::PatternDetail) {
      applyPatternDetail();
    } else if (mode == Mode::ConfirmBulk) {
      runBulkAction();
    } else if (mode == Mode::ConfirmProfileDelete) {
      deleteSelectedProfile();
      mode = Mode::Cards;
    } else if (mode == Mode::ProfileNameInput) {
      submitProfileNameInput();
    } else if (mode == Mode::ConfirmProfileOverwrite) {
      confirmProfileOverwrite();
    } else if (mode == Mode::ConfirmProfileApply) {
      applyLoadedProfile();
      mode = Mode::Cards;
      app.dirty = true;
    } else {
      applyCurrentCard();
    }
    return;
  }

  if (status.del || (status.opt && profileModalActive())) {
    if (mode == Mode::ProfileNameInput && status.del && app.profileNameInput.length() > 0) {
      app.profileNameInput.remove(app.profileNameInput.length() - 1);
    } else if (mode == Mode::PatternDetail || mode == Mode::ConfirmBulk || mode == Mode::ConfirmProfileDelete ||
               mode == Mode::ProfileNameInput || mode == Mode::ConfirmProfileOverwrite ||
               mode == Mode::ConfirmProfileApply) {
      if (mode == Mode::ProfileNameInput || mode == Mode::ConfirmProfileOverwrite) {
        setStatus("Save cancelled", COLOR_WARN);
      } else if (mode == Mode::ConfirmProfileApply) {
        setStatus("Apply cancelled", COLOR_WARN);
      }
      mode = Mode::Cards;
    } else if (app.selectedCard != 0) {
      app.selectedCard = 0;
      syncEditFromCard();
    }
    app.dirty = true;
    return;
  }

  if (status.tab) {
    changeCard(1);
    return;
  }

  for (char c : status.word) {
    handleWordChar(c);
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
  uiCanvas.setColorDepth(16);
  canvasReady = uiCanvas.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  uiCanvas.setTextSize(1);
  uiCanvas.setFont(&fonts::Font0);
  uiCanvas.setTextDatum(top_left);
  randomSeed(static_cast<uint32_t>(esp_random()));

  ensurePatternModel();
  updateCardputerBattery(true);
  syncEditFromCard();
  setStatus("NightKite Link boot", COLOR_MUTED);
  sendCommand(NightKiteCommands::refreshAll());
}

void loop()
{
  M5Cardputer.update();
  updateCardputerBattery();
  handleKeyboard();
  updateFlashWorkflow();
  if (!flashWorkflowBusy()) {
    pollTransport();
    pollCommandQueue();
  }
  render();
  delay(2);
}
