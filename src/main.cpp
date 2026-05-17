#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <math.h>
#include <vector>
#include "M5Cardputer.h"
#include "SoundManager.h"
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
constexpr unsigned long CONTROLLER_BATTERY_POLL_MS = 60000;
constexpr unsigned long LINK_STALE_MS = 9000;
constexpr unsigned long COMMAND_SEND_INTERVAL_MS = 160;
constexpr unsigned long AUTO_STATUS_POLL_MS = 4000;
constexpr unsigned long AUTO_REFRESH_IDLE_MS = 1800;
constexpr unsigned long SYNC_TEST_STATUS_POLL_MS = 1800;
constexpr unsigned long SYNC_TEST_WIRELESS_POLL_MS = 5000;
constexpr unsigned long USB_RECONNECT_STABLE_MS = 600;
constexpr unsigned long NK4_COMMAND_TIMEOUT_MS = 1400;
constexpr unsigned long NK4_PROBE_TIMEOUT_MS = 1600;
constexpr unsigned long NK4_MACHINE_DELAY_MS = 120;
constexpr unsigned long SPLASH_DURATION_MS = 1500;
constexpr unsigned long STARTUP_SOUND_DELAY_MS = 180;

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
  bool hasControllerBattery = false;
  uint32_t enabledPatternMask = 0;
  uint32_t invertedPatternMask = 0;
  String deviceName;
  String playMode = "unknown";
  String bootMode = "unknown";
  bool syncEnabled = false;
  int syncGroup = -1;
  String syncRole = "unknown";
  String syncMasterUid;
  String syncLossBehavior = "unknown";
  bool wirelessEnabled = false;
  String wirelessProfile = "unknown";
  std::vector<PatternConfig> patterns;
};

enum class ProtocolMode : uint8_t {
  Unknown,
  Probing,
  Legacy,
  Nk4,
};

enum class TransportMode : uint8_t {
  Usb,
  Ble,
};

struct ControllerIdentity {
  String uid;
  String shortId;
  String name;
  String firmware;
  String protocol;
  String hardware;
  String caps;
};

struct ControllerCapabilities {
  bool play = false;
  bool sync = false;
  bool wireless = false;
  bool ble = false;
  bool syncRadio = false;
};

struct PlayState {
  String playMode = "unknown";
  String bootMode = "unknown";
};

struct SyncState {
  bool supported = false;
  bool enabled = false;
  int group = -1;
  String role = "unknown";
  String masterUid;
  String lossBehavior = "unknown";
  String state = "unknown";
  bool locked = false;
  int lastSeq = -1;
  int driftMs = 0;
  bool beaconTx = false;
  bool beaconRx = false;
  unsigned long beaconTxCount = 0;
  unsigned long beaconRxCount = 0;
  unsigned long beaconCrcErrors = 0;
  unsigned long beaconGroupMismatch = 0;
  int beaconAgeMs = -1;
  String radioMode = "unknown";
};

struct WirelessState {
  bool supported = false;
  bool enabled = false;
  String profile = "unknown";
  bool bleSupported = false;
  bool bleEnabled = false;
  bool bleInitialized = false;
  bool bleAdvertising = false;
  bool bleConnected = false;
  bool bleGatt = false;
  String bleName;
  String wifi = "unknown";
  bool syncRadioSupported = false;
  bool syncRadioActive = false;
};

struct DiagnosticsState {
  String imu = "unknown";
  String bootStage = "unknown";
  String configValid = "unknown";
  bool configRepaired = false;
  int configVersion = -1;
  bool safeBoot = false;
};

struct CommandQueueEntry {
  String command;
  bool nk4Raw = false;
  uint16_t seq = 0;
  unsigned long sentAt = 0;
  uint32_t generation = 0;
};

enum class Card : uint8_t {
  Status,
  Device,
  Play,
  Sync,
  Wireless,
  SyncTest,
  Brightness,
  Config,
  Calibration,
  ActivePattern,
  PatternList,
  PatternBulk,
  Firmware,
  Profiles,
};

constexpr int CARD_COUNT = 14;

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
  ProtocolMode protocolMode = ProtocolMode::Unknown;
  TransportMode transportMode = TransportMode::Usb;
  ControllerIdentity identity;
  ControllerCapabilities capabilities;
  PlayState play;
  SyncState sync;
  WirelessState wireless;
  DiagnosticsState diagnostics;
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
  int selectedPlayField = 0;
  int selectedSyncField = 0;
  int selectedWirelessField = 0;
  int selectedSyncTestAction = 0;
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
unsigned long lastControllerBatteryReadMs = 0;
unsigned long lastUserInputMs = 0;
unsigned long lastSyncTestStatusPollMs = 0;
unsigned long lastSyncTestWirelessPollMs = 0;
unsigned long usbConnectedSinceMs = 0;
bool lastUsbConnected = false;
bool usbProbePending = false;
String rxLine;
std::vector<CommandQueueEntry> commandQueue;
bool nk4Pending = false;
CommandQueueEntry pendingNk4;
uint16_t nextNk4Seq = 1;
uint32_t connectionGeneration = 0;
bool nk4MachineSent = false;
bool nk4HelloSent = false;
unsigned long nk4ProbeStartMs = 0;
unsigned long nk4MachineSentMs = 0;
bool patternSyncInProgress = false;
bool transferCompleteSoundPending = false;
bool canvasReady = false;
M5Canvas uiCanvas(&M5Cardputer.Display);
SoundManager soundManager;
bool splashActive = true;
bool startupSoundPlayed = false;
unsigned long splashStartMs = 0;
String editValue;
bool editBool = false;
bool detailCycle = false;
bool detailInvert = false;
bool brightnessDirty = false;
bool patternDirty = false;
int draftBrightness = -1;
int draftActivePattern = -1;
int draftStripLength = -1;
int draftSmoothing = -1;
int draftAccelRange = -1;
int draftGyroRange = -1;
bool draftAutoplayEnabled = false;
int draftAutoplayIntervalSeconds = -1;
String draftPlayMode = "unknown";
String draftBootMode = "unknown";
bool draftPlayAutoplayEnabled = false;
int draftPlayAutoplayIntervalSeconds = -1;
bool draftSyncEnabled = false;
int draftSyncGroup = -1;
String draftSyncRole = "unknown";
String draftSyncLossBehavior = "unknown";
bool draftWirelessEnabled = false;
String draftWirelessProfile = "unknown";
int syncTestGroup = 1;
String syncTestProfile = "balanced";
uint8_t configDirtyMask = 0;
uint8_t playDirtyMask = 0;
uint8_t syncDirtyMask = 0;
uint8_t wirelessDirtyMask = 0;

constexpr uint8_t CONFIG_DIRTY_STRIP = 1 << 0;
constexpr uint8_t CONFIG_DIRTY_SMOOTH = 1 << 1;
constexpr uint8_t CONFIG_DIRTY_ACCEL = 1 << 2;
constexpr uint8_t CONFIG_DIRTY_GYRO = 1 << 3;
constexpr uint8_t CONFIG_DIRTY_AUTOPLAY = 1 << 4;
constexpr uint8_t CONFIG_DIRTY_INTERVAL = 1 << 5;
constexpr uint8_t PLAY_DIRTY_MODE = 1 << 0;
constexpr uint8_t PLAY_DIRTY_BOOT = 1 << 1;
constexpr uint8_t PLAY_DIRTY_AUTOPLAY = 1 << 2;
constexpr uint8_t PLAY_DIRTY_INTERVAL = 1 << 3;
constexpr uint8_t SYNC_DIRTY_ENABLED = 1 << 0;
constexpr uint8_t SYNC_DIRTY_GROUP = 1 << 1;
constexpr uint8_t SYNC_DIRTY_ROLE = 1 << 2;
constexpr uint8_t SYNC_DIRTY_LOSS = 1 << 3;
constexpr uint8_t WIRELESS_DIRTY_ENABLED = 1 << 0;
constexpr uint8_t WIRELESS_DIRTY_PROFILE = 1 << 1;

UsbMscUf2Flasher uf2Flasher;

bool ensureSdReady();
void syncEditFromCard();

bool flashCopyAudioQuiet()
{
  if (splashActive) {
    return true;
  }
  return app.flash.state == FlashUiState::MassStorageConnected || app.flash.state == FlashUiState::CopyingUf2 ||
         app.flash.state == FlashUiState::CopyComplete || app.flash.state == FlashUiState::WaitingForReboot;
}

void playStatusSound(const String& text, uint16_t color)
{
  if (flashCopyAudioQuiet()) {
    return;
  }
  if (color == COLOR_ERR) {
    soundManager.playError();
  } else if (color == COLOR_OK && !text.startsWith("OK") && !text.startsWith("USB connected") &&
             !text.startsWith("Profiles:") && !text.startsWith("UF2 files:") && text != "SD ready" &&
             text != "Transfer complete" && text != "Pattern states sent") {
    soundManager.playSuccess();
  }
}

class NightKiteTransport {
public:
  virtual bool connected() = 0;
  virtual void sendLine(const String& line) = 0;
  virtual bool readLine(String& line) = 0;
  virtual void clearBuffers() {}
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

  void clearBuffers() override
  {
    buffer = "";
    while (Serial.available() > 0) {
      Serial.read();
    }
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

  void clearBuffers() override
  {
    begin();
    buffer = "";
    while (hostSerial.available() > 0) {
      hostSerial.read();
    }
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
  playStatusSound(text, color);
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
  int start = 0;
  while (true) {
    start = line.indexOf(token, start);
    if (start < 0) {
      return "";
    }
    if (start == 0 || line[start - 1] == ' ') {
      break;
    }
    ++start;
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

const char* protocolToken()
{
  switch (app.protocolMode) {
    case ProtocolMode::Legacy:
      return "LEG";
    case ProtocolMode::Nk4:
      return "NK4";
    case ProtocolMode::Probing:
      return "...";
    case ProtocolMode::Unknown:
    default:
      return "--";
  }
}

const char* transportToken()
{
  return app.transportMode == TransportMode::Ble ? "BLE" : "USB";
}

String connectionToken()
{
  if (!app.usbConnected) {
    return "NO USB";
  }
  if (usbProbePending || app.protocolMode == ProtocolMode::Probing) {
    return "USB DET";
  }
  return String(transportToken()) + " " + protocolToken();
}

String controllerLabel()
{
  if (app.identity.name.length() > 0) {
    return app.identity.name;
  }
  if (app.identity.shortId.length() > 0) {
    return app.identity.shortId;
  }
  if (app.identity.uid.length() > 0) {
    return app.identity.uid.substring(0, min(8, static_cast<int>(app.identity.uid.length())));
  }
  return "NK";
}

String playToken()
{
  if (app.play.playMode == "manual") {
    return "MAN";
  }
  if (app.play.playMode == "autoplay") {
    return "AUTO";
  }
  if (app.play.playMode == "sync") {
    return "SYNC";
  }
  return "--";
}

String roleToken()
{
  if (app.sync.role == "master") {
    return "M";
  }
  if (app.sync.role == "follower") {
    return "F";
  }
  if (app.sync.role == "standalone") {
    return "S";
  }
  return "-";
}

uint32_t parseUint32Text(String value, uint32_t fallback)
{
  value.trim();
  if (value.length() == 0) {
    return fallback;
  }
  return static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 0));
}

unsigned long parseUlongText(String value, unsigned long fallback)
{
  value.trim();
  if (value.length() == 0) {
    return fallback;
  }
  return strtoul(value.c_str(), nullptr, 0);
}

void parseIntField(const String& line, const char* key, int& target)
{
  String value = valueForKey(line, key);
  if (value.length() > 0) {
    target = value.toInt();
  }
}

bool parseFloatField(const String& line, const char* key, float& target)
{
  String value = valueForKey(line, key);
  if (value.length() == 0) {
    return false;
  }
  target = value.toFloat();
  return true;
}

bool parseBoolText(const String& value)
{
  return value == "on" || value == "1" || value == "true" || value == "yes";
}

bool hasBoolKey(const String& line, const char* key, bool& target)
{
  String value = valueForKey(line, key);
  if (value.length() == 0) {
    return false;
  }
  target = parseBoolText(value);
  return true;
}

void parseStringField(const String& line, const char* key, String& target)
{
  String value = valueForKey(line, key);
  if (value.length() > 0) {
    target = value;
  }
}

bool parsePercentNearSymbol(const String& line, int& percent)
{
  int pos = line.indexOf('%');
  if (pos < 0) {
    return false;
  }
  int start = pos - 1;
  while (start >= 0 && isDigit(static_cast<unsigned char>(line[start]))) {
    --start;
  }
  if (start == pos - 1) {
    return false;
  }
  int value = line.substring(start + 1, pos).toInt();
  if (value < 0 || value > 100) {
    return false;
  }
  percent = value;
  return true;
}

bool parseVoltageNearSymbol(const String& line, float& voltage)
{
  int pos = line.indexOf('V');
  if (pos < 0) {
    pos = line.indexOf('v');
  }
  if (pos < 0) {
    return false;
  }
  int start = pos - 1;
  while (start >= 0 && (isDigit(static_cast<unsigned char>(line[start])) || line[start] == '.')) {
    --start;
  }
  if (start == pos - 1) {
    return false;
  }
  float value = line.substring(start + 1, pos).toFloat();
  if (value < 0.5f || value > 6.5f) {
    return false;
  }
  voltage = value;
  return true;
}

void parseControllerBattery(const String& line)
{
  float voltage = NAN;
  bool voltageParsed = parseFloatField(line, "battery_voltage", voltage) || parseFloatField(line, "battery_v", voltage) ||
                       parseFloatField(line, "controller_battery_voltage", voltage);
  String mv = valueForKey(line, "battery_mv");
  if (mv.length() > 0) {
    voltage = mv.toFloat() / 1000.0f;
    voltageParsed = true;
  }
  if (!voltageParsed) {
    voltageParsed = parseVoltageNearSymbol(line, voltage);
  }
  if (voltageParsed && voltage > 0.5f && voltage < 6.5f) {
    app.settings.controllerBatteryVoltage = voltage;
    app.settings.hasControllerBattery = true;
  }

  int percent = -1;
  const char* percentKeys[] = {"battery_percent", "battery_pct", "controller_battery_percent"};
  for (const char* key : percentKeys) {
    String value = valueForKey(line, key);
    if (value.length() == 0) {
      continue;
    }
    int parsed = value.toInt();
    if (parsed >= 0 && parsed <= 100) {
      percent = parsed;
      break;
    }
  }
  if (percent < 0) {
    parsePercentNearSymbol(line, percent);
  }
  if (percent >= 0) {
    app.settings.controllerBatteryPercent = percent;
    app.settings.hasControllerBattery = true;
  }
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
  static String refreshBattery()
  {
    return "battery";
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

String legacyCommandToNk4Payload(String command)
{
  command.trim();
  if (command == "show") {
    return "cmd=status";
  }
  if (command == "patterns" || command == "get inverted_patterns") {
    return "cmd=get section=patterns";
  }
  if (command == "battery") {
    return "cmd=battery";
  }
  if (command == "sensor") {
    return "cmd=sensor";
  }
  if (command == "timing") {
    return "cmd=timing";
  }
  if (command == "save") {
    return "cmd=save";
  }
  if (command == "calibrate quick") {
    return "cmd=calibrate mode=quick";
  }
  if (command == "calibrate precise") {
    return "cmd=calibrate mode=precise";
  }
  if (command.startsWith("set brightness ")) {
    return "cmd=set brightness=" + command.substring(15);
  }
  if (command.startsWith("set strip_length ")) {
    return "cmd=set strip_length=" + command.substring(17);
  }
  if (command.startsWith("set pattern ")) {
    return "cmd=set pattern=" + command.substring(12);
  }
  if (command.startsWith("set smoothing ")) {
    return "cmd=set smoothing=" + command.substring(14);
  }
  if (command.startsWith("set accel_range ")) {
    return "cmd=set accel_range=" + command.substring(16);
  }
  if (command.startsWith("set gyro_range ")) {
    return "cmd=set gyro_range=" + command.substring(15);
  }
  if (command.startsWith("set boot_calibration ")) {
    return "cmd=set boot_calibration=" + command.substring(21);
  }
  if (command.startsWith("set autoplay_interval ")) {
    return "cmd=set autoplay_interval=" + command.substring(22);
  }
  if (command.startsWith("set autoplay ")) {
    String value = command.substring(13);
    value.trim();
    return String("cmd=set autoplay=") + (parseBoolText(value) ? "1" : "0");
  }
  if (command.startsWith("enable_pattern ") || command.startsWith("disable_pattern ")) {
    bool enable = command.startsWith("enable_pattern ");
    String list = command.substring(enable ? 15 : 16);
    uint32_t mask = currentEnabledMask();
    uint32_t requested = patternMaskFromList(list);
    mask = enable ? (mask | requested) : (mask & ~requested);
    return "cmd=set enabled_mask=" + String(mask);
  }
  if (command.startsWith("invert_pattern ") || command.startsWith("normal_pattern ")) {
    bool invert = command.startsWith("invert_pattern ");
    String list = command.substring(invert ? 15 : 15);
    uint32_t mask = currentInvertedMask();
    uint32_t requested = patternMaskFromList(list);
    mask = invert ? (mask | requested) : (mask & ~requested);
    return "cmd=set inverted_mask=" + String(mask);
  }
  return "cmd=" + command;
}

void enqueueCommandEntry(const String& command, bool nk4Raw = false)
{
  CommandQueueEntry entry;
  entry.command = command;
  entry.nk4Raw = nk4Raw;
  entry.generation = connectionGeneration;
  commandQueue.push_back(entry);
  app.lastCommand = command;
}

void sendCommand(const String& command, bool announce = true)
{
  if (command.length() == 0) {
    setStatus("Command missing", COLOR_WARN);
    return;
  }
  bool wasEmpty = commandQueue.empty();
  if (app.protocolMode == ProtocolMode::Nk4 && command == NightKiteCommands::refreshAll()) {
    enqueueCommandEntry("cmd=info", true);
    enqueueCommandEntry("cmd=caps", true);
    enqueueCommandEntry("cmd=status", true);
    enqueueCommandEntry("cmd=get section=config", true);
    enqueueCommandEntry("cmd=get section=play", true);
    enqueueCommandEntry("cmd=get section=sync", true);
    enqueueCommandEntry("cmd=get section=wireless", true);
    enqueueCommandEntry("cmd=get section=patterns", true);
  } else if (app.protocolMode == ProtocolMode::Nk4) {
    enqueueCommandEntry(legacyCommandToNk4Payload(command), true);
  } else {
    enqueueCommandEntry(command, false);
  }
  if (announce && wasEmpty) {
    setStatus("Queued command", COLOR_ACCENT);
  }
}

void enqueueNk4RefreshForSet(const String& command)
{
  if (command.indexOf("play_mode=") >= 0 || command.indexOf("boot_mode=") >= 0 ||
      command.indexOf("autoplay=") >= 0 || command.indexOf("autoplay_interval=") >= 0) {
    enqueueCommandEntry("cmd=get section=play", true);
    enqueueCommandEntry("cmd=status", true);
  } else if (command.indexOf("sync_enabled=") >= 0 || command.indexOf("sync_group=") >= 0 ||
             command.indexOf("sync_role=") >= 0 || command.indexOf("sync_loss_behavior=") >= 0 ||
             command.indexOf("sync_master_uid=") >= 0) {
    enqueueCommandEntry("cmd=get section=sync", true);
    enqueueCommandEntry("cmd=sync_status", true);
  } else if (command.indexOf("wireless_enabled=") >= 0 || command.indexOf("wireless_profile=") >= 0) {
    enqueueCommandEntry("cmd=get section=wireless", true);
  } else if (command.indexOf("name=") >= 0) {
    enqueueCommandEntry("cmd=info", true);
    enqueueCommandEntry("cmd=status", true);
  } else if (command.indexOf("enabled_mask=") >= 0 || command.indexOf("inverted_mask=") >= 0) {
    enqueueCommandEntry("cmd=get section=patterns", true);
  } else if (command.indexOf("pattern=") >= 0) {
    enqueueCommandEntry("cmd=status", true);
    enqueueCommandEntry("cmd=get section=patterns", true);
  } else if (command.indexOf("brightness=") >= 0 || command.indexOf("strip_length=") >= 0 ||
             command.indexOf("smoothing=") >= 0 || command.indexOf("accel_range=") >= 0 ||
             command.indexOf("gyro_range=") >= 0 || command.indexOf("boot_calibration=") >= 0) {
    enqueueCommandEntry("cmd=get section=config", true);
    enqueueCommandEntry("cmd=status", true);
  }
}

void handleNk4CommandOk(const String& command)
{
  if (command == "cmd=save") {
    setStatus("Saved", COLOR_OK);
    return;
  }
  if (!command.startsWith("cmd=set ")) {
    return;
  }
  if (command.indexOf("play_mode=") >= 0) {
    playDirtyMask &= ~PLAY_DIRTY_MODE;
  }
  if (command.indexOf("boot_mode=") >= 0) {
    playDirtyMask &= ~PLAY_DIRTY_BOOT;
  }
  if (command.indexOf("autoplay_interval=") >= 0) {
    playDirtyMask &= ~PLAY_DIRTY_INTERVAL;
    configDirtyMask &= ~CONFIG_DIRTY_INTERVAL;
  } else if (command.indexOf("autoplay=") >= 0) {
    playDirtyMask &= ~PLAY_DIRTY_AUTOPLAY;
    configDirtyMask &= ~CONFIG_DIRTY_AUTOPLAY;
  }
  if (command.indexOf("brightness=") >= 0) {
    brightnessDirty = false;
  }
  if (command.indexOf("strip_length=") >= 0) {
    configDirtyMask &= ~CONFIG_DIRTY_STRIP;
  }
  if (command.indexOf("smoothing=") >= 0) {
    configDirtyMask &= ~CONFIG_DIRTY_SMOOTH;
  }
  if (command.indexOf("accel_range=") >= 0) {
    configDirtyMask &= ~CONFIG_DIRTY_ACCEL;
  }
  if (command.indexOf("gyro_range=") >= 0) {
    configDirtyMask &= ~CONFIG_DIRTY_GYRO;
  }
  if (command.indexOf("pattern=") >= 0 && command.indexOf("enabled_mask=") < 0 &&
      command.indexOf("inverted_mask=") < 0) {
    patternDirty = false;
  }
  if (command.indexOf("sync_enabled=") >= 0) {
    syncDirtyMask &= ~SYNC_DIRTY_ENABLED;
  }
  if (command.indexOf("sync_group=") >= 0) {
    syncDirtyMask &= ~SYNC_DIRTY_GROUP;
  }
  if (command.indexOf("sync_role=") >= 0) {
    syncDirtyMask &= ~SYNC_DIRTY_ROLE;
  }
  if (command.indexOf("sync_loss_behavior=") >= 0) {
    syncDirtyMask &= ~SYNC_DIRTY_LOSS;
  }
  if (command.indexOf("wireless_enabled=") >= 0) {
    wirelessDirtyMask &= ~WIRELESS_DIRTY_ENABLED;
  }
  if (command.indexOf("wireless_profile=") >= 0) {
    wirelessDirtyMask &= ~WIRELESS_DIRTY_PROFILE;
  }
  enqueueNk4RefreshForSet(command);
  app.dirty = true;
}

void markTransferCompleteSoundPending()
{
  transferCompleteSoundPending = true;
}

void playTransferCompleteIfPending(const String& statusText)
{
  if (!transferCompleteSoundPending) {
    return;
  }
  transferCompleteSoundPending = false;
  setStatus(statusText, COLOR_OK);
  soundManager.playTransferComplete();
}

void fallbackToLegacy();

void requestControllerBattery(bool force = false)
{
  unsigned long now = millis();
  if (!force && now - lastControllerBatteryReadMs < CONTROLLER_BATTERY_POLL_MS) {
    return;
  }
  lastControllerBatteryReadMs = now;
  Serial.println("controller battery request");
  sendCommand(NightKiteCommands::refreshBattery(), false);
}

void pollCommandQueue()
{
  if (nk4Pending) {
    if (!app.usbConnected || pendingNk4.generation != connectionGeneration) {
      nk4Pending = false;
    } else if (millis() - pendingNk4.sentAt > NK4_COMMAND_TIMEOUT_MS) {
      String timedOut = pendingNk4.command;
      nk4Pending = false;
      if (app.protocolMode == ProtocolMode::Probing) {
        fallbackToLegacy();
        return;
      }
      app.controllerError = true;
      setStatus("NK4 timeout", COLOR_ERR);
      Serial.print("NK4 timeout: ");
      Serial.println(timedOut);
    }
    return;
  }

  if (commandQueue.empty()) {
    if (patternSyncInProgress) {
      patternSyncInProgress = false;
      app.patternEditsPending = false;
      playTransferCompleteIfPending("Pattern states sent");
    } else {
      playTransferCompleteIfPending("Transfer complete");
    }
    return;
  }
  if (millis() - lastCommandSendMs < COMMAND_SEND_INTERVAL_MS) {
    return;
  }
  if (!app.usbConnected) {
    commandQueue.clear();
    patternSyncInProgress = false;
    transferCompleteSoundPending = false;
    app.dirty = true;
    return;
  }
  while (!commandQueue.empty() && commandQueue.front().generation != connectionGeneration) {
    commandQueue.erase(commandQueue.begin());
  }
  if (commandQueue.empty()) {
    return;
  }
  CommandQueueEntry entry = commandQueue.front();
  commandQueue.erase(commandQueue.begin());
  String wireCommand = entry.command;
  if (entry.nk4Raw) {
    entry.seq = nextNk4Seq++;
    wireCommand = "NK4 seq=" + String(entry.seq) + " " + entry.command;
    entry.sentAt = millis();
    pendingNk4 = entry;
    nk4Pending = true;
  }
  transport.sendLine(wireCommand);
  lastCommandSendMs = millis();
  app.lastCommand = wireCommand;
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

String controllerBatteryVoltageText()
{
  if (isnan(app.settings.controllerBatteryVoltage)) {
    return "";
  }
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%.2fV", app.settings.controllerBatteryVoltage);
  return String(buffer);
}

String controllerBatteryText()
{
  String percent = app.settings.controllerBatteryPercent >= 0 ? String(app.settings.controllerBatteryPercent) + "%" : "";
  String voltage = controllerBatteryVoltageText();
  if (percent.length() > 0 && voltage.length() > 0) {
    return percent + " " + voltage;
  }
  if (percent.length() > 0) {
    return percent;
  }
  if (voltage.length() > 0) {
    return voltage;
  }
  return "--";
}

uint8_t configFieldMask(int field)
{
  switch (field) {
    case 0:
      return CONFIG_DIRTY_STRIP;
    case 1:
      return CONFIG_DIRTY_SMOOTH;
    case 2:
      return CONFIG_DIRTY_ACCEL;
    case 3:
      return CONFIG_DIRTY_GYRO;
    case 4:
      return CONFIG_DIRTY_AUTOPLAY;
    case 5:
      return CONFIG_DIRTY_INTERVAL;
    default:
      return 0;
  }
}

bool currentCardHasDirtyDraft()
{
  switch (static_cast<Card>(app.selectedCard)) {
    case Card::Brightness:
      return brightnessDirty;
    case Card::ActivePattern:
      return patternDirty;
    case Card::Config:
      return configDirtyMask != 0;
    case Card::Play:
      return playDirtyMask != 0;
    case Card::Sync:
      return syncDirtyMask != 0;
    case Card::Wireless:
      return wirelessDirtyMask != 0;
    default:
      return false;
  }
}

int dirtyDraftCount()
{
  int count = 0;
  count += brightnessDirty ? 1 : 0;
  count += patternDirty ? 1 : 0;
  count += configDirtyMask != 0 ? 1 : 0;
  count += playDirtyMask != 0 ? 1 : 0;
  count += syncDirtyMask != 0 ? 1 : 0;
  count += wirelessDirtyMask != 0 ? 1 : 0;
  count += app.patternEditsPending ? 1 : 0;
  return count;
}

bool autoRefreshPaused()
{
  if (dirtyDraftCount() > 0 || nk4Pending || !commandQueue.empty()) {
    return true;
  }
  return millis() - lastUserInputMs < AUTO_REFRESH_IDLE_MS;
}

void discardDraftForCard(Card card)
{
  switch (card) {
    case Card::Brightness:
      brightnessDirty = false;
      draftBrightness = app.settings.brightness;
      editValue = showInt(app.settings.brightness);
      break;
    case Card::ActivePattern:
      patternDirty = false;
      draftActivePattern = app.settings.activePattern;
      editValue = showInt(app.settings.activePattern);
      break;
    case Card::Config:
      configDirtyMask = 0;
      draftStripLength = app.settings.stripLength;
      draftSmoothing = app.settings.smoothing;
      draftAccelRange = app.settings.accelRange;
      draftGyroRange = app.settings.gyroRange;
      draftAutoplayEnabled = app.settings.autoplayEnabled;
      draftAutoplayIntervalSeconds = app.settings.autoplayIntervalSeconds;
      break;
    case Card::Play:
      playDirtyMask = 0;
      draftPlayMode = app.play.playMode;
      draftBootMode = app.play.bootMode;
      draftPlayAutoplayEnabled = app.settings.autoplayEnabled;
      draftPlayAutoplayIntervalSeconds = app.settings.autoplayIntervalSeconds;
      break;
    case Card::Sync:
      syncDirtyMask = 0;
      draftSyncEnabled = app.sync.enabled;
      draftSyncGroup = app.sync.group;
      draftSyncRole = app.sync.role;
      draftSyncLossBehavior = app.sync.lossBehavior;
      break;
    case Card::Wireless:
      wirelessDirtyMask = 0;
      draftWirelessEnabled = app.wireless.enabled;
      draftWirelessProfile = app.wireless.profile;
      break;
    default:
      break;
  }
  app.dirty = true;
}

void discardCurrentDraft()
{
  discardDraftForCard(static_cast<Card>(app.selectedCard));
  setStatus("Edit cancelled", COLOR_WARN);
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
  String text = connectionToken();
  String name = app.controllerConnected ? shortText(controllerLabel(), 8) : "--";
  String play = app.protocolMode == ProtocolMode::Nk4 ? playToken() + "/" + roleToken() : "CTRL";
  String nk = "NK:--";
  if (app.controllerConnected && app.settings.hasControllerBattery) {
    nk = controllerBatteryText();
  }
  bool cliBusy = !commandQueue.empty() || patternSyncInProgress;
  String queueText = "Q:" + String(commandQueue.size());
  uint16_t queueColor = cliBusy ? COLOR_WARN : COLOR_MUTED;
  drawTextFit(text, 3, 4, 42, app.controllerError ? COLOR_WARN : COLOR_TEXT, COLOR_PANEL_DARK);
  drawTextFit(name, 48, 4, 42, app.controllerConnected ? COLOR_ACCENT : COLOR_MUTED, COLOR_PANEL_DARK);
  drawTextFit(play, 93, 4, 43, app.protocolMode == ProtocolMode::Nk4 ? COLOR_OK : COLOR_MUTED, COLOR_PANEL_DARK);
  drawTextFit(nk, 139, 4, 60, app.settings.hasControllerBattery ? COLOR_OK : COLOR_MUTED, COLOR_PANEL_DARK);
  drawTextFit(String("L:") + cp, 201, 4, 36, app.cardputerCharging ? COLOR_OK : COLOR_ACCENT, COLOR_PANEL_DARK);
  if (cliBusy) {
    drawTextFit(queueText, 115, 4, 22, queueColor, COLOR_PANEL_DARK);
  }
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

void drawDeviceCard()
{
  String battery = app.settings.hasControllerBattery ? controllerBatteryText() : "--";
  drawTextFit("Device", 8, CONTENT_Y + 5, 90, COLOR_MUTED);
  drawTextFit(String(transportToken()) + " " + protocolToken(), 160, CONTENT_Y + 5, 70,
              app.protocolMode == ProtocolMode::Nk4 ? COLOR_OK : COLOR_WARN);
  drawStatusTile(8, CONTENT_Y + 22, 70, 25, "Name", app.controllerConnected ? shortText(controllerLabel(), 9) : "--",
                 COLOR_ACCENT);
  drawStatusTile(85, CONTENT_Y + 22, 70, 25, "Short", app.identity.shortId.length() ? app.identity.shortId : "--",
                 COLOR_TEXT);
  drawStatusTile(162, CONTENT_Y + 22, 70, 25, "Battery", battery, app.settings.hasControllerBattery ? COLOR_OK : COLOR_MUTED);
  drawTextFit("FW " + (app.identity.firmware.length() ? app.identity.firmware : "--"), 10, CONTENT_Y + 58, 108,
              COLOR_TEXT);
  drawTextFit("Proto " + (app.identity.protocol.length() ? app.identity.protocol : "--"), 125, CONTENT_Y + 58, 105,
              COLOR_TEXT);
  drawTextFit("HW " + (app.identity.hardware.length() ? app.identity.hardware : "--"), 10, CONTENT_Y + 74, 108,
              COLOR_TEXT);
  drawTextFit("Cfg " + app.diagnostics.configValid, 125, CONTENT_Y + 74, 105,
              app.diagnostics.configValid == "1" || app.diagnostics.configValid == "true" ? COLOR_OK : COLOR_MUTED);
  drawFooter("R refresh");
}

const char* const PLAY_MODES[] = {"manual", "autoplay", "sync"};
constexpr int PLAY_MODE_COUNT = sizeof(PLAY_MODES) / sizeof(PLAY_MODES[0]);
const char* const BOOT_MODES[] = {"last", "manual", "autoplay", "sync"};
constexpr int BOOT_MODE_COUNT = sizeof(BOOT_MODES) / sizeof(BOOT_MODES[0]);

int indexOfOption(const char* const* options, int count, const String& value)
{
  for (int i = 0; i < count; ++i) {
    if (value == options[i]) {
      return i;
    }
  }
  return 0;
}

String optionWithDelta(const char* const* options, int count, const String& value, int delta)
{
  if (count <= 0) {
    return value;
  }
  int index = indexOfOption(options, count, value) + delta;
  if (index < 0) {
    index = count - 1;
  } else if (index >= count) {
    index = 0;
  }
  return options[index];
}

void drawPlayCard()
{
  drawTextFit(String("Play") + (playDirtyMask ? "*" : ""), 8, CONTENT_Y + 5, 70, playDirtyMask ? COLOR_WARN : COLOR_MUTED);
  String unavailable = !app.usbConnected ? "No controller"
                       : (usbProbePending || app.protocolMode == ProtocolMode::Probing) ? "Detecting..."
                                                                                        : "NK4 required";
  String labels[] = {"Mode", "Boot", "Auto", "Interval"};
  String values[] = {
      (playDirtyMask & PLAY_DIRTY_MODE) ? draftPlayMode : app.play.playMode,
      (playDirtyMask & PLAY_DIRTY_BOOT) ? draftBootMode : app.play.bootMode,
      ((playDirtyMask & PLAY_DIRTY_AUTOPLAY) ? draftPlayAutoplayEnabled : app.settings.autoplayEnabled) ? "ON" : "OFF",
      showInt((playDirtyMask & PLAY_DIRTY_INTERVAL) ? draftPlayAutoplayIntervalSeconds
                                                     : app.settings.autoplayIntervalSeconds) +
          "s",
  };
  for (int i = 0; i < 4; ++i) {
    int x = 8 + (i % 2) * 116;
    int y = CONTENT_Y + 24 + (i / 2) * 34;
    bool active = i == app.selectedPlayField;
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(x, y, 108, 28, 3, bg);
    bool dirtyField = (playDirtyMask & (1 << i)) != 0;
    drawTextFit(String(labels[i]) + (dirtyField ? "*" : ""), x + 5, y + 6, 48, dirtyField ? COLOR_WARN : COLOR_MUTED, bg);
    drawTextFit(values[i], x + 48, y + 6, 55, active ? COLOR_TEXT : COLOR_ACCENT, bg);
  }
  drawFooter(app.protocolMode == ProtocolMode::Nk4 ? (playDirtyMask ? "PEND  ENTER set  DEL cancel" : "C field  W/S edit")
                                                   : unavailable);
}

const char* const SYNC_ROLES[] = {"standalone", "master", "follower"};
constexpr int SYNC_ROLE_COUNT = sizeof(SYNC_ROLES) / sizeof(SYNC_ROLES[0]);
const char* const SYNC_LOSS[] = {"continue_local", "fallback_autoplay", "warning_only"};
constexpr int SYNC_LOSS_COUNT = sizeof(SYNC_LOSS) / sizeof(SYNC_LOSS[0]);

void drawSyncCard()
{
  drawTextFit(String("Sync") + (syncDirtyMask ? "*" : ""), 8, CONTENT_Y + 5, 80, syncDirtyMask ? COLOR_WARN : COLOR_MUTED);
  String unavailable = !app.usbConnected ? "No controller"
                       : (usbProbePending || app.protocolMode == ProtocolMode::Probing) ? "Detecting..."
                                                                                        : "NK4 required";
  String labels[] = {"Enable", "Group", "Role", "State", "Lock", "Loss"};
  String values[] = {((syncDirtyMask & SYNC_DIRTY_ENABLED) ? draftSyncEnabled : app.sync.enabled) ? "ON" : "OFF",
                     showInt((syncDirtyMask & SYNC_DIRTY_GROUP) ? draftSyncGroup : app.sync.group),
                     (syncDirtyMask & SYNC_DIRTY_ROLE) ? draftSyncRole : app.sync.role, app.sync.state,
                     app.sync.locked ? "YES" : "NO", app.sync.lossBehavior};
  if (syncDirtyMask & SYNC_DIRTY_LOSS) {
    values[5] = draftSyncLossBehavior;
  }
  for (int i = 0; i < 6; ++i) {
    int x = 8 + (i % 3) * 76;
    int y = CONTENT_Y + 22 + (i / 3) * 34;
    bool editable = i == 0 || i == 1 || i == 2 || i == 5;
    bool active = editable && i == app.selectedSyncField;
    bool dirtyField = (i == 0 && (syncDirtyMask & SYNC_DIRTY_ENABLED)) ||
                      (i == 1 && (syncDirtyMask & SYNC_DIRTY_GROUP)) ||
                      (i == 2 && (syncDirtyMask & SYNC_DIRTY_ROLE)) ||
                      (i == 5 && (syncDirtyMask & SYNC_DIRTY_LOSS));
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(x, y, 70, 28, 3, bg);
    drawTextFit(String(labels[i]) + (dirtyField ? "*" : ""), x + 5, y + 6, 60, dirtyField ? COLOR_WARN : COLOR_MUTED, bg);
    drawTextFit(values[i], x + 5, y + 18, 60, active ? COLOR_TEXT : COLOR_ACCENT, bg);
  }
  drawFooter(app.protocolMode == ProtocolMode::Nk4 ? (syncDirtyMask ? "PEND  ENTER set  DEL cancel" : "C field  W/S edit")
                                                   : unavailable);
}

const char* const WIRELESS_PROFILES[] = {"long_range", "balanced", "fast_sync"};
constexpr int WIRELESS_PROFILE_COUNT = sizeof(WIRELESS_PROFILES) / sizeof(WIRELESS_PROFILES[0]);

const char* const SYNC_TEST_ACTIONS[] = {"Configure Master", "Configure Follower", "Save", "Refresh Sync",
                                         "Group", "Profile", "Name Master", "Name Follower", "Play SYNC"};
constexpr int SYNC_TEST_ACTION_COUNT = sizeof(SYNC_TEST_ACTIONS) / sizeof(SYNC_TEST_ACTIONS[0]);

String boolShort(bool value)
{
  return value ? "ON" : "OFF";
}

String lockedText()
{
  return app.sync.locked ? "LOCK" : "--";
}

String beaconAgeText()
{
  return app.sync.beaconAgeMs >= 0 ? String(app.sync.beaconAgeMs) + "ms" : "--";
}

String wirelessProfileText()
{
  if (app.wireless.profile == "long_range") {
    return "long";
  }
  if (app.wireless.profile == "balanced") {
    return "bal";
  }
  if (app.wireless.profile == "fast_sync") {
    return "fast";
  }
  return app.wireless.profile == "unknown" ? "--" : shortText(app.wireless.profile, 5);
}

String radioModeText()
{
  if (app.sync.radioMode == "beacon_master") {
    return "master";
  }
  if (app.sync.radioMode == "beacon_follower") {
    return "follower";
  }
  if (app.sync.radioMode == "gatt") {
    return "gatt";
  }
  if (app.sync.radioMode == "off") {
    return "off";
  }
  return app.sync.radioMode == "unknown" ? "--" : shortText(app.sync.radioMode, 8);
}

String selectedSyncTestProfile()
{
  if (syncTestProfile == "unknown" || syncTestProfile.length() == 0) {
    return "balanced";
  }
  return syncTestProfile;
}

int selectedSyncTestGroup()
{
  return syncTestGroup >= 1 ? syncTestGroup : 1;
}

bool requireNk4Controller()
{
  if (!app.usbConnected) {
    setStatus("No controller", COLOR_WARN);
    return false;
  }
  if (usbProbePending || app.protocolMode == ProtocolMode::Probing) {
    setStatus("Detecting protocol...", COLOR_ACCENT);
    return false;
  }
  if (app.protocolMode != ProtocolMode::Nk4) {
    setStatus("Firmware 4.0 / NK4 required", COLOR_WARN);
    return false;
  }
  return true;
}

void queueSyncTestRefresh()
{
  if (!requireNk4Controller()) {
    return;
  }
  enqueueCommandEntry("cmd=get section=sync", true);
  enqueueCommandEntry("cmd=sync_status", true);
  enqueueCommandEntry("cmd=get section=wireless", true);
  enqueueCommandEntry("cmd=status", true);
  setStatus("Sync refresh queued", COLOR_ACCENT);
}

void queueSyncTestRoleSetup(const char* role, const char* name)
{
  if (!requireNk4Controller()) {
    return;
  }
  int group = selectedSyncTestGroup();
  String profile = selectedSyncTestProfile();
  sendCommand(String("set name=") + name);
  sendCommand("set play_mode=sync");
  sendCommand(String("set sync_enabled=1 sync_group=") + group + " sync_role=" + role);
  sendCommand("set wireless_enabled=1 wireless_profile=" + profile);
  setStatus(String(role[0] == 'm' ? "Master" : "Follower") + " setup sent", COLOR_ACCENT);
}

void drawSyncTestCard()
{
  drawTextFit("Sync Test", 8, CONTENT_Y + 4, 88, COLOR_MUTED);
  drawTextFit("G" + String(selectedSyncTestGroup()) + " " + shortText(selectedSyncTestProfile(), 9), 124, CONTENT_Y + 4,
              108, COLOR_ACCENT);
  if (!app.usbConnected) {
    drawTextFit("No controller", 12, CONTENT_Y + 34, 160, COLOR_WARN);
    drawTextFit("Connect USB", 12, CONTENT_Y + 52, 120, COLOR_MUTED);
    drawFooter("Disconnected");
    return;
  }
  if (usbProbePending || app.protocolMode == ProtocolMode::Probing) {
    drawTextFit("Detecting...", 12, CONTENT_Y + 34, 150, COLOR_ACCENT);
    drawTextFit("Please wait", 12, CONTENT_Y + 52, 120, COLOR_MUTED);
    drawFooter("USB protocol detect");
    return;
  }
  if (app.protocolMode != ProtocolMode::Nk4) {
    drawTextFit("Firmware 4.0 / NK4", 12, CONTENT_Y + 34, 180, COLOR_WARN);
    drawTextFit("required", 12, CONTENT_Y + 52, 120, COLOR_WARN);
    drawFooter("USB NK4 required");
    return;
  }

  int visible = 4;
  int start = 0;
  if (app.selectedSyncTestAction >= visible) {
    start = app.selectedSyncTestAction - visible + 1;
  }
  for (int row = 0; row < visible && start + row < SYNC_TEST_ACTION_COUNT; ++row) {
    int idx = start + row;
    int y = CONTENT_Y + 18 + row * 18;
    bool active = idx == app.selectedSyncTestAction;
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(6, y, 112, 16, 2, bg);
    String label = SYNC_TEST_ACTIONS[idx];
    if (idx == 4) {
      label += " " + String(selectedSyncTestGroup());
    } else if (idx == 5) {
      label += " " + shortText(selectedSyncTestProfile(), 8);
    }
    drawTextFit(String(active ? "> " : "  ") + label, 10, y + 5, 104, active ? COLOR_TEXT : COLOR_MUTED, bg);
  }

  drawTextFit(shortText(controllerLabel(), 12) + " " + shortText(app.identity.shortId, 7), 124, CONTENT_Y + 18, 108,
              COLOR_TEXT);
  drawTextFit("P " + playToken() + " " + roleToken() + " G" + showInt(app.sync.group), 124, CONTENT_Y + 32, 108,
              COLOR_ACCENT);
  drawTextFit("S " + boolShort(app.sync.enabled) + " " + shortText(app.sync.state, 8) + " " + lockedText(), 124,
              CONTENT_Y + 46, 108, app.sync.locked ? COLOR_OK : COLOR_MUTED);
  drawTextFit("R " + radioModeText() + " W " + wirelessProfileText(), 124, CONTENT_Y + 62, 108,
              app.sync.radioMode == "gatt" ? COLOR_WARN : COLOR_TEXT);
  drawTextFit("TX " + String(app.sync.beaconTxCount) + " RX " + String(app.sync.beaconRxCount),
              124, CONTENT_Y + 76, 108, COLOR_TEXT);
  drawTextFit("E " + String(app.sync.beaconCrcErrors) + "/" + String(app.sync.beaconGroupMismatch) + " A" +
                  beaconAgeText(),
              124, CONTENT_Y + 90, 108, (app.sync.beaconCrcErrors || app.sync.beaconGroupMismatch) ? COLOR_WARN : COLOR_MUTED);

  String hint = app.sync.radioMode == "gatt" ? "GATT active - beacon off"
                : app.sync.role == "master" ? "Master: no BLE client"
                : app.sync.role == "follower" ? "Follower: expect RX"
                                              : "USB config only";
  drawFooter(hint);
}

void drawWirelessCard()
{
  drawTextFit(String("Wireless") + (wirelessDirtyMask ? "*" : ""), 8, CONTENT_Y + 5, 90,
              wirelessDirtyMask ? COLOR_WARN : COLOR_MUTED);
  String unavailable = !app.usbConnected ? "No controller"
                       : (usbProbePending || app.protocolMode == ProtocolMode::Probing) ? "Detecting..."
                                                                                        : "NK4 required";
  String labels[] = {"Enable", "Profile", "Radio", "TX", "RX", "CRC"};
  String values[] = {((wirelessDirtyMask & WIRELESS_DIRTY_ENABLED) ? draftWirelessEnabled : app.wireless.enabled) ? "ON" : "OFF",
                     (wirelessDirtyMask & WIRELESS_DIRTY_PROFILE) ? draftWirelessProfile : app.wireless.profile, app.sync.radioMode,
                     String(app.sync.beaconTxCount), String(app.sync.beaconRxCount), String(app.sync.beaconCrcErrors)};
  for (int i = 0; i < 6; ++i) {
    int x = 8 + (i % 3) * 76;
    int y = CONTENT_Y + 20 + (i / 3) * 30;
    bool editable = i == 0 || i == 1;
    bool active = editable && i == app.selectedWirelessField;
    bool dirtyField = (i == 0 && (wirelessDirtyMask & WIRELESS_DIRTY_ENABLED)) ||
                      (i == 1 && (wirelessDirtyMask & WIRELESS_DIRTY_PROFILE));
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(x, y, 70, 24, 3, bg);
    drawTextFit(String(labels[i]) + (dirtyField ? "*" : ""), x + 5, y + 5, 60, dirtyField ? COLOR_WARN : COLOR_MUTED, bg);
    drawTextFit(values[i], x + 5, y + 16, 60, active ? COLOR_TEXT : COLOR_ACCENT, bg);
  }
  drawTextFit(String("BLE ") + (app.wireless.bleSupported ? "yes" : "--") + " " + shortText(app.wireless.bleName, 12), 10,
              CONTENT_Y + 82, 218, COLOR_TEXT);
  drawFooter(app.protocolMode == ProtocolMode::Nk4
                 ? (wirelessDirtyMask ? "PEND  ENTER set  DEL cancel" : "C field  W/S edit")
                 : unavailable);
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
  int value = brightnessDirty ? draftBrightness : app.settings.brightness;
  drawTitle(String("Brightness") + (brightnessDirty ? "*" : ""));
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
  drawFooter(brightnessDirty ? "PEND  ENTER set  DEL cancel" : "W/S edit  ENTER set");
}

void drawPatternCard()
{
  int value = patternDirty ? draftActivePattern : app.settings.activePattern;
  ensurePatternModel();
  bool cycle = value >= 1 && value <= PATTERN_COUNT ? app.settings.patterns[value - 1].cycleEnabled : false;
  bool inverted = value >= 1 && value <= PATTERN_COUNT ? app.settings.patterns[value - 1].inverted : false;
  drawTitle(String("Pattern") + (patternDirty ? "*" : ""));
  char num[8];
  snprintf(num, sizeof(num), "%02d", value > 0 ? value : 0);
  drawBigValue(value > 0 ? String(num) : "--", CONTENT_Y + 27);
  drawTextFit(patternName(value), 10, CONTENT_Y + 69, 145, COLOR_TEXT);
  drawTextFit(String("Cycle ") + (cycle ? "ON" : "OFF"), 158, CONTENT_Y + 57, 72, cycle ? COLOR_OK : COLOR_MUTED);
  drawTextFit(String("Inv ") + (inverted ? "ON" : "OFF"), 158, CONTENT_Y + 72, 72, inverted ? COLOR_WARN : COLOR_MUTED);
  drawFooter(patternDirty ? "PEND  ENTER set  DEL cancel" : "W/S edit  C cycle  I invert");
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
  drawTextFit(String("Config") + (configDirtyMask ? "*" : ""), 8, CONTENT_Y + 6, 120,
              configDirtyMask ? COLOR_WARN : COLOR_MUTED);
  for (int i = 0; i < 6; ++i) {
    int x = 8 + (i % 3) * 76;
    int y = CONTENT_Y + 25 + (i / 3) * 34;
    int w = 70;
    bool active = i == app.selectedConfigField;
    bool dirtyField = (configDirtyMask & configFieldMask(i)) != 0;
    uint16_t bg = active ? COLOR_ACCENT_DARK : COLOR_PANEL_DARK;
    uiCanvas.fillRoundRect(x, y, w, 28, 3, bg);
    drawTextFit(String(labels[i]) + (dirtyField ? "*" : ""), x + 5, y + 6, w - 10,
                dirtyField ? COLOR_WARN : COLOR_MUTED, bg);
    drawTextFit(values[i], x + 5, y + 18, w - 10, active ? COLOR_TEXT : COLOR_ACCENT, bg);
  }
  drawFooter(configDirtyMask ? "PEND  ENTER set  DEL cancel" : "C field  W/S edit");
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

void drawSplashScreen()
{
  auto& d = uiCanvas;
  d.fillScreen(COLOR_BG);
  d.setTextDatum(top_center);
  d.setFont(&fonts::Font4);
  d.setTextColor(COLOR_ACCENT, COLOR_BG);
  d.drawString("NightKite", SCREEN_W / 2, 20);
  d.setFont(&fonts::Font2);
  d.setTextColor(COLOR_TEXT, COLOR_BG);
  d.drawString("Link", SCREEN_W / 2, 52);
  d.setFont(&fonts::Font0);
  d.setTextColor(COLOR_MUTED, COLOR_BG);
  d.drawString("Cardputer-Adv", SCREEN_W / 2, 78);
  d.drawString("USB Configurator", SCREEN_W / 2, 93);
  d.setTextColor(COLOR_ACCENT, COLOR_BG);
  d.drawString("Loading...", SCREEN_W / 2, 112);
  d.setTextDatum(top_left);
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
  if (splashActive) {
    drawSplashScreen();
    if (canvasReady) {
      uiCanvas.pushSprite(0, 0);
    }
    app.dirty = false;
    return;
  }

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
      case Card::Device:
        drawDeviceCard();
        break;
      case Card::Play:
        drawPlayCard();
        break;
      case Card::Sync:
        drawSyncCard();
        break;
      case Card::Wireless:
        drawWirelessCard();
        break;
      case Card::SyncTest:
        drawSyncTestCard();
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
  file.println("  \"profile_version\": 2,");
  file.println("  \"project\": \"NightKite Link\",");
  file.println("  \"target\": \"NightKite Multi\",");
  file.println("  \"settings\": {");
  file.print("    \"device_name\": ");
  writeJsonString(file, app.identity.name);
  file.println(",");
  file.printf("    \"brightness\": %d,\n", app.settings.brightness);
  file.printf("    \"strip_length\": %d,\n", app.settings.stripLength);
  file.printf("    \"active_pattern\": %d,\n", app.settings.activePattern);
  file.printf("    \"smoothing\": %d,\n", app.settings.smoothing);
  file.printf("    \"accel_range\": %d,\n", app.settings.accelRange);
  file.printf("    \"gyro_range\": %d,\n", app.settings.gyroRange);
  file.print("    \"play_mode\": ");
  writeJsonString(file, app.play.playMode);
  file.println(",");
  file.print("    \"boot_mode\": ");
  writeJsonString(file, app.play.bootMode);
  file.println(",");
  file.printf("    \"sync_enabled\": %s,\n", app.sync.enabled ? "true" : "false");
  file.printf("    \"sync_group\": %d,\n", app.sync.group);
  file.print("    \"sync_role\": ");
  writeJsonString(file, app.sync.role);
  file.println(",");
  file.print("    \"sync_master_uid\": ");
  writeJsonString(file, app.sync.masterUid);
  file.println(",");
  file.print("    \"sync_loss_behavior\": ");
  writeJsonString(file, app.sync.lossBehavior);
  file.println(",");
  file.printf("    \"wireless_enabled\": %s,\n", app.wireless.enabled ? "true" : "false");
  file.print("    \"wireless_profile\": ");
  writeJsonString(file, app.wireless.profile);
  file.println(",");
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

String jsonStringValue(const String& json, const char* key, const String& fallback)
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
  if (start >= json.length() || json[start] != '"') {
    return fallback;
  }
  ++start;
  String value;
  while (start < json.length()) {
    char c = json[start++];
    if (c == '\\' && start < json.length()) {
      value += json[start++];
    } else if (c == '"') {
      break;
    } else {
      value += c;
    }
  }
  return value;
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
  loaded.deviceName = jsonStringValue(json, "device_name", loaded.deviceName);
  loaded.playMode = jsonStringValue(json, "play_mode", loaded.playMode);
  loaded.bootMode = jsonStringValue(json, "boot_mode", loaded.bootMode);
  loaded.syncEnabled = jsonBool(json, "sync_enabled", loaded.syncEnabled);
  loaded.syncGroup = jsonInt(json, "sync_group", loaded.syncGroup);
  loaded.syncRole = jsonStringValue(json, "sync_role", loaded.syncRole);
  loaded.syncMasterUid = jsonStringValue(json, "sync_master_uid", loaded.syncMasterUid);
  loaded.syncLossBehavior = jsonStringValue(json, "sync_loss_behavior", loaded.syncLossBehavior);
  loaded.wirelessEnabled = jsonBool(json, "wireless_enabled", loaded.wirelessEnabled);
  loaded.wirelessProfile = jsonStringValue(json, "wireless_profile", loaded.wirelessProfile);
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
  loaded.deviceName = jsonStringValue(json, "device_name", loaded.deviceName);
  loaded.playMode = jsonStringValue(json, "play_mode", loaded.playMode);
  loaded.bootMode = jsonStringValue(json, "boot_mode", loaded.bootMode);
  loaded.syncEnabled = jsonBool(json, "sync_enabled", loaded.syncEnabled);
  loaded.syncGroup = jsonInt(json, "sync_group", loaded.syncGroup);
  loaded.syncRole = jsonStringValue(json, "sync_role", loaded.syncRole);
  loaded.syncMasterUid = jsonStringValue(json, "sync_master_uid", loaded.syncMasterUid);
  loaded.syncLossBehavior = jsonStringValue(json, "sync_loss_behavior", loaded.syncLossBehavior);
  loaded.wirelessEnabled = jsonBool(json, "wireless_enabled", loaded.wirelessEnabled);
  loaded.wirelessProfile = jsonStringValue(json, "wireless_profile", loaded.wirelessProfile);
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
  markTransferCompleteSoundPending();
  if (app.protocolMode == ProtocolMode::Nk4) {
    if (app.loadedProfile.deviceName.length() > 0) {
      sendCommand("set name=" + app.loadedProfile.deviceName);
    }
    sendCommand(NightKiteCommands::setBrightness(app.loadedProfile.brightness));
    sendCommand(NightKiteCommands::setStripLength(app.loadedProfile.stripLength));
    sendCommand(NightKiteCommands::setPattern(app.loadedProfile.activePattern));
    sendCommand(NightKiteCommands::setSmoothing(app.loadedProfile.smoothing));
    sendCommand(NightKiteCommands::setAccelRange(app.loadedProfile.accelRange));
    sendCommand(NightKiteCommands::setGyroRange(app.loadedProfile.gyroRange));
    sendCommand(String("set autoplay=") + (app.loadedProfile.autoplayEnabled ? "1" : "0"));
    sendCommand("set autoplay_interval=" + String(app.loadedProfile.autoplayIntervalSeconds));
    if (app.loadedProfile.playMode != "unknown") {
      sendCommand("set play_mode=" + app.loadedProfile.playMode);
    }
    if (app.loadedProfile.bootMode != "unknown") {
      sendCommand("set boot_mode=" + app.loadedProfile.bootMode);
    }
    sendCommand("set enabled_mask=" + String(app.loadedProfile.enabledPatternMask));
    sendCommand("set inverted_mask=" + String(app.loadedProfile.invertedPatternMask));
    sendCommand(String("set sync_enabled=") + (app.loadedProfile.syncEnabled ? "1" : "0"));
    if (app.loadedProfile.syncGroup >= 0) {
      sendCommand("set sync_group=" + String(app.loadedProfile.syncGroup));
    }
    if (app.loadedProfile.syncRole != "unknown") {
      sendCommand("set sync_role=" + app.loadedProfile.syncRole);
    }
    if (app.loadedProfile.syncMasterUid.length() > 0) {
      sendCommand("set sync_master_uid=" + app.loadedProfile.syncMasterUid);
    }
    if (app.loadedProfile.syncLossBehavior != "unknown") {
      sendCommand("set sync_loss_behavior=" + app.loadedProfile.syncLossBehavior);
    }
    sendCommand(String("set wireless_enabled=") + (app.loadedProfile.wirelessEnabled ? "1" : "0"));
    if (app.loadedProfile.wirelessProfile != "unknown") {
      sendCommand("set wireless_profile=" + app.loadedProfile.wirelessProfile);
    }
    setStatus("Profile queued", COLOR_ACCENT);
    return;
  }
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
  setStatus("Profile queued", COLOR_ACCENT);
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

String nk4FriendlyError(const String& code)
{
  if (code == "unsupported") {
    return "Unsupported";
  }
  if (code == "invalid_value" || code == "range_error") {
    return "Invalid value";
  }
  if (code == "busy" || code == "sync_busy") {
    return "Busy";
  }
  if (code == "timeout") {
    return "Timeout";
  }
  if (code == "not_ready") {
    return "Not ready";
  }
  if (code == "save_failed") {
    return "Save failed";
  }
  if (code == "sync_not_armed") {
    return "Sync not armed";
  }
  if (code == "sync_too_late") {
    return "Sync too late";
  }
  return code.length() > 0 ? code : "NK4 error";
}

void applyNk4Fields(const String& parsed)
{
  parseStringField(parsed, "uid", app.identity.uid);
  parseStringField(parsed, "device_uid", app.identity.uid);
  parseStringField(parsed, "short_id", app.identity.shortId);
  parseStringField(parsed, "name", app.identity.name);
  parseStringField(parsed, "device_name", app.identity.name);
  parseStringField(parsed, "fw", app.identity.firmware);
  parseStringField(parsed, "firmware", app.identity.firmware);
  parseStringField(parsed, "firmware_version", app.identity.firmware);
  parseStringField(parsed, "proto", app.identity.protocol);
  parseStringField(parsed, "protocol", app.identity.protocol);
  parseStringField(parsed, "protocol_version", app.identity.protocol);
  parseStringField(parsed, "hw", app.identity.hardware);
  parseStringField(parsed, "hardware", app.identity.hardware);
  parseStringField(parsed, "hardware_id", app.identity.hardware);
  parseStringField(parsed, "caps", app.identity.caps);

  if (app.identity.caps.length() > 0) {
    app.capabilities.play = app.identity.caps.indexOf("play") >= 0;
    app.capabilities.sync = app.identity.caps.indexOf("sync") >= 0;
    app.capabilities.wireless = app.identity.caps.indexOf("wireless") >= 0;
    app.capabilities.ble = app.identity.caps.indexOf("ble") >= 0;
    app.capabilities.syncRadio = app.identity.caps.indexOf("sync_radio") >= 0 || app.identity.caps.indexOf("beacon") >= 0;
  }

  parseIntField(parsed, "pattern", app.settings.activePattern);
  parseIntField(parsed, "brightness", app.settings.brightness);
  parseIntField(parsed, "strip_length", app.settings.stripLength);
  parseIntField(parsed, "smoothing", app.settings.smoothing);
  parseIntField(parsed, "accel_range", app.settings.accelRange);
  parseIntField(parsed, "gyro_range", app.settings.gyroRange);
  parseIntField(parsed, "autoplay_interval", app.settings.autoplayIntervalSeconds);
  parseStringField(parsed, "boot_calibration", app.settings.bootCalibration);
  parseStringField(parsed, "fps", app.settings.fps);
  parseStringField(parsed, "imu", app.diagnostics.imu);
  parseStringField(parsed, "boot_stage", app.diagnostics.bootStage);
  parseStringField(parsed, "config_valid", app.diagnostics.configValid);
  parseIntField(parsed, "config_version", app.diagnostics.configVersion);
  hasBoolKey(parsed, "config_repaired", app.diagnostics.configRepaired);
  hasBoolKey(parsed, "safe_boot", app.diagnostics.safeBoot);

  String autoplay = valueForKey(parsed, "autoplay");
  if (autoplay.length() > 0) {
    app.settings.autoplayEnabled = parseBoolText(autoplay);
  }
  parseStringField(parsed, "play_mode", app.play.playMode);
  parseStringField(parsed, "boot_mode", app.play.bootMode);
  app.settings.deviceName = app.identity.name;
  app.settings.playMode = app.play.playMode;
  app.settings.bootMode = app.play.bootMode;

  String enabledMask = valueForKey(parsed, "enabled_mask");
  String invertedMask = valueForKey(parsed, "inverted_mask");
  if (enabledMask.length() > 0 || invertedMask.length() > 0) {
    applyPatternMasks(parseUint32Text(enabledMask, app.settings.enabledPatternMask),
                      parseUint32Text(invertedMask, app.settings.invertedPatternMask), enabledMask.length() > 0,
                      invertedMask.length() > 0);
  }
  parsePatternStates(parsed);
  parseControllerBattery(parsed);

  bool boolValue = false;
  if (hasBoolKey(parsed, "sync_enabled", boolValue)) {
    app.sync.supported = true;
    app.sync.enabled = boolValue;
    app.settings.syncEnabled = boolValue;
  }
  parseIntField(parsed, "sync_group", app.sync.group);
  parseIntField(parsed, "group", app.sync.group);
  parseStringField(parsed, "sync_role", app.sync.role);
  parseStringField(parsed, "role", app.sync.role);
  parseStringField(parsed, "sync_master_uid", app.sync.masterUid);
  parseStringField(parsed, "master_uid", app.sync.masterUid);
  parseStringField(parsed, "sync_loss_behavior", app.sync.lossBehavior);
  parseStringField(parsed, "loss_behavior", app.sync.lossBehavior);
  app.settings.syncGroup = app.sync.group;
  app.settings.syncRole = app.sync.role;
  app.settings.syncMasterUid = app.sync.masterUid;
  app.settings.syncLossBehavior = app.sync.lossBehavior;
  parseStringField(parsed, "sync_state", app.sync.state);
  if (hasBoolKey(parsed, "sync_locked", boolValue)) {
    app.sync.locked = boolValue;
  }
  parseIntField(parsed, "last_seq", app.sync.lastSeq);
  parseIntField(parsed, "drift_ms", app.sync.driftMs);
  if (hasBoolKey(parsed, "beacon_tx", boolValue)) {
    app.sync.beaconTx = boolValue;
  }
  if (hasBoolKey(parsed, "beacon_rx", boolValue)) {
    app.sync.beaconRx = boolValue;
  }
  app.sync.beaconTxCount = parseUlongText(valueForKey(parsed, "beacon_tx_count"), app.sync.beaconTxCount);
  app.sync.beaconRxCount = parseUlongText(valueForKey(parsed, "beacon_rx_count"), app.sync.beaconRxCount);
  app.sync.beaconCrcErrors = parseUlongText(valueForKey(parsed, "beacon_crc_errors"), app.sync.beaconCrcErrors);
  app.sync.beaconGroupMismatch =
      parseUlongText(valueForKey(parsed, "beacon_group_mismatch"), app.sync.beaconGroupMismatch);
  parseIntField(parsed, "beacon_age_ms", app.sync.beaconAgeMs);
  parseIntField(parsed, "last_beacon_ms", app.sync.beaconAgeMs);
  parseStringField(parsed, "radio_mode", app.sync.radioMode);

  if (hasBoolKey(parsed, "wireless_enabled", boolValue)) {
    app.wireless.supported = true;
    app.wireless.enabled = boolValue;
    app.settings.wirelessEnabled = boolValue;
  }
  parseStringField(parsed, "wireless_profile", app.wireless.profile);
  app.settings.wirelessProfile = app.wireless.profile;
  if (hasBoolKey(parsed, "ble_supported", boolValue)) {
    app.wireless.bleSupported = boolValue;
  }
  if (hasBoolKey(parsed, "ble_enabled", boolValue)) {
    app.wireless.bleEnabled = boolValue;
  }
  if (hasBoolKey(parsed, "ble_initialized", boolValue)) {
    app.wireless.bleInitialized = boolValue;
  }
  if (hasBoolKey(parsed, "ble_advertising", boolValue)) {
    app.wireless.bleAdvertising = boolValue;
  }
  if (hasBoolKey(parsed, "ble_connected", boolValue)) {
    app.wireless.bleConnected = boolValue;
  }
  if (hasBoolKey(parsed, "ble_gatt", boolValue)) {
    app.wireless.bleGatt = boolValue;
  }
  parseStringField(parsed, "ble_name", app.wireless.bleName);
  parseStringField(parsed, "wifi", app.wireless.wifi);
  if (hasBoolKey(parsed, "sync_radio_supported", boolValue)) {
    app.wireless.syncRadioSupported = boolValue;
  }
  if (hasBoolKey(parsed, "sync_radio_active", boolValue)) {
    app.wireless.syncRadioActive = boolValue;
  }

  if (app.sync.group >= 0 || app.sync.role != "unknown" || app.sync.state != "unknown") {
    app.sync.supported = true;
  }
  if (app.wireless.profile != "unknown" || app.wireless.bleName.length() > 0) {
    app.wireless.supported = true;
  }
  syncEditFromCard();
}

bool parseNk4Line(const String& parsed)
{
  if (!parsed.startsWith("NK4 ")) {
    return false;
  }

  bool isEvent = parsed.indexOf(" event=") >= 0;
  int seq = -1;
  String seqText = valueForKey(parsed, "seq");
  if (seqText.length() > 0) {
    seq = seqText.toInt();
  }

  bool isOk = parsed.indexOf(" ok") >= 0;
  bool isErr = parsed.indexOf(" err") >= 0;

  String matchedCommand;
  if (nk4Pending && seq >= 0 && seq == pendingNk4.seq) {
    matchedCommand = pendingNk4.command;
    nk4Pending = false;
  }

  if (seq >= 0 && !isEvent && matchedCommand.length() == 0) {
    Serial.print("Ignoring stale NK4 response: ");
    Serial.println(parsed);
    return true;
  }

  app.controllerConnected = true;
  app.controllerError = false;
  lastRxMs = millis();
  app.lastResponse = parsed;

  if (isOk || isEvent) {
    if (matchedCommand.length() > 0) {
      handleNk4CommandOk(matchedCommand);
    }
    applyNk4Fields(parsed);
    if (app.protocolMode == ProtocolMode::Probing) {
      app.protocolMode = ProtocolMode::Nk4;
      app.controllerError = false;
      commandQueue.clear();
      setStatus("USB NK4 detected", COLOR_OK);
      sendCommand(NightKiteCommands::refreshAll(), false);
    } else if (matchedCommand == "cmd=save") {
      setStatus("Saved", COLOR_OK);
    } else if (isEvent) {
      setStatus(shortText(parsed, 34), COLOR_MUTED);
    } else {
      setStatus(shortText(parsed, 34), COLOR_OK);
    }
  } else if (isErr) {
    String code = valueForKey(parsed, "code");
    String msg = valueForKey(parsed, "msg");
    setStatus(msg.length() > 0 ? msg : nk4FriendlyError(code), code == "unsupported" ? COLOR_WARN : COLOR_ERR);
    app.controllerError = code != "unsupported";
  } else {
    applyNk4Fields(parsed);
  }

  app.dirty = true;
  return true;
}

void parseNightKiteLine(const String& line)
{
  String parsed = stripCliPrompt(line);
  app.lastResponse = parsed;

  if (parseNk4Line(parsed)) {
    return;
  }

  if (app.protocolMode == ProtocolMode::Probing) {
    return;
  }

  if (parsed.startsWith("OK ")) {
    if (app.protocolMode == ProtocolMode::Unknown) {
      app.protocolMode = ProtocolMode::Legacy;
    }
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
    parseControllerBattery(parsed);
    parsePatternStates(parsed);
    setStatus(shortText(parsed, 34), COLOR_OK);
  } else if (parsed.startsWith("ERR")) {
    if (app.protocolMode == ProtocolMode::Unknown) {
      app.protocolMode = ProtocolMode::Legacy;
    }
    app.controllerConnected = true;
    app.controllerError = true;
    lastRxMs = millis();
    setStatus(shortText(parsed, 34), COLOR_ERR);
  } else if (parsed.startsWith("INFO") || parsed.startsWith("[NightKite CLI]")) {
    if (app.protocolMode == ProtocolMode::Unknown) {
      app.protocolMode = ProtocolMode::Legacy;
    }
    app.controllerConnected = true;
    lastRxMs = millis();
    setStatus(shortText(parsed, 34), COLOR_MUTED);
  } else if (parsed.indexOf("battery") >= 0 || parsed.indexOf("Battery") >= 0 || parsed.indexOf("BATTERY") >= 0) {
    app.controllerConnected = true;
    lastRxMs = millis();
    parseControllerBattery(parsed);
  }
  syncEditFromCard();
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

void resetControllerSession()
{
  ++connectionGeneration;
  transport.clearBuffers();
  rxLine = "";
  app.controllerConnected = false;
  app.controllerError = false;
  app.settings.hasControllerBattery = false;
  app.settings.controllerBatteryPercent = -1;
  app.settings.controllerBatteryVoltage = NAN;
  app.protocolMode = ProtocolMode::Unknown;
  app.transportMode = TransportMode::Usb;
  app.identity = ControllerIdentity{};
  app.capabilities = ControllerCapabilities{};
  app.play = PlayState{};
  app.sync = SyncState{};
  app.wireless = WirelessState{};
  app.diagnostics = DiagnosticsState{};
  brightnessDirty = false;
  patternDirty = false;
  configDirtyMask = 0;
  playDirtyMask = 0;
  syncDirtyMask = 0;
  wirelessDirtyMask = 0;
  app.patternEditsPending = false;
  commandQueue.clear();
  nk4Pending = false;
  pendingNk4 = CommandQueueEntry{};
  nk4MachineSent = false;
  nk4HelloSent = false;
  patternSyncInProgress = false;
  transferCompleteSoundPending = false;
  app.lastCommand = "";
  app.lastResponse = "";
  lastCommandSendMs = 0;
  lastPollMs = millis();
  lastRxMs = millis();
  lastControllerBatteryReadMs = 0;
  lastSyncTestStatusPollMs = 0;
  lastSyncTestWirelessPollMs = 0;
}

void beginNk4Probe()
{
  resetControllerSession();
  app.protocolMode = ProtocolMode::Probing;
  usbProbePending = false;
  nk4ProbeStartMs = millis();
  nk4MachineSentMs = 0;
  setStatus("Detecting protocol...", COLOR_ACCENT);
}

void fallbackToLegacy()
{
  app.protocolMode = ProtocolMode::Legacy;
  nk4Pending = false;
  commandQueue.clear();
  setStatus("USB legacy mode", COLOR_WARN);
  sendCommand(NightKiteCommands::refreshAll(), false);
  requestControllerBattery(true);
}

void pollNk4Probe()
{
  if (!app.usbConnected || app.protocolMode != ProtocolMode::Probing) {
    return;
  }
  unsigned long now = millis();
  if (!nk4MachineSent) {
    transport.sendLine("protocol machine");
    nk4MachineSent = true;
    nk4MachineSentMs = now;
    return;
  }
  if (!nk4HelloSent && now - nk4MachineSentMs >= NK4_MACHINE_DELAY_MS) {
    pendingNk4 = CommandQueueEntry{};
    pendingNk4.command = "cmd=hello";
    pendingNk4.nk4Raw = true;
    pendingNk4.seq = nextNk4Seq++;
    pendingNk4.sentAt = now;
    pendingNk4.generation = connectionGeneration;
    nk4Pending = true;
    nk4HelloSent = true;
    transport.sendLine("NK4 seq=" + String(pendingNk4.seq) +
                       " cmd=hello client=nightkite-link proto_min=4 proto_max=4");
    app.lastCommand = "NK4 hello";
    app.dirty = true;
    return;
  }
  if (nk4HelloSent && now - nk4ProbeStartMs > NK4_PROBE_TIMEOUT_MS) {
    fallbackToLegacy();
  }
}

void pollTransport()
{
  bool wasUsbConnected = app.usbConnected;
  app.usbConnected = transport.connected();
  if (app.usbConnected != wasUsbConnected) {
    if (!app.usbConnected) {
      resetControllerSession();
      usbProbePending = false;
      transferCompleteSoundPending = false;
      setStatus("Disconnected", COLOR_WARN);
    } else {
      resetControllerSession();
      usbProbePending = true;
      usbConnectedSinceMs = millis();
      setStatus("Connecting...", COLOR_ACCENT);
    }
    app.dirty = true;
  }

  if (app.usbConnected && usbProbePending) {
    if (millis() - usbConnectedSinceMs < USB_RECONNECT_STABLE_MS) {
      transport.clearBuffers();
      return;
    }
    beginNk4Probe();
  }

  String line;
  while (transport.readLine(line)) {
    parseNightKiteLine(line);
  }

  pollNk4Probe();

  if (app.controllerConnected && millis() - lastRxMs > LINK_STALE_MS) {
    resetControllerSession();
    if (app.usbConnected) {
      usbProbePending = true;
      usbConnectedSinceMs = millis();
      setStatus("Reconnecting...", COLOR_WARN);
    } else {
      setStatus("Disconnected", COLOR_WARN);
    }
    app.dirty = true;
    return;
  }

  if (app.usbConnected && app.controllerConnected) {
    requestControllerBattery();
  }

  if (app.usbConnected && app.protocolMode == ProtocolMode::Nk4 && static_cast<Card>(app.selectedCard) == Card::SyncTest &&
      !autoRefreshPaused()) {
    unsigned long now = millis();
    if (now - lastSyncTestStatusPollMs > SYNC_TEST_STATUS_POLL_MS) {
      lastSyncTestStatusPollMs = now;
      enqueueCommandEntry("cmd=sync_status", true);
    }
    if (now - lastSyncTestWirelessPollMs > SYNC_TEST_WIRELESS_POLL_MS) {
      lastSyncTestWirelessPollMs = now;
      enqueueCommandEntry("cmd=get section=wireless", true);
    }
  }

  if (millis() - lastPollMs > AUTO_STATUS_POLL_MS) {
    lastPollMs = millis();
    if (app.usbConnected && app.protocolMode != ProtocolMode::Probing && !autoRefreshPaused()) {
      if (app.protocolMode == ProtocolMode::Nk4) {
        enqueueCommandEntry("cmd=status", true);
      } else {
        sendCommand(NightKiteCommands::refreshAll(), false);
      }
    }
  }
}

void syncEditFromCard()
{
  switch (static_cast<Card>(app.selectedCard)) {
    case Card::Brightness:
      if (!brightnessDirty) {
        draftBrightness = app.settings.brightness;
        editValue = showInt(app.settings.brightness);
      }
      break;
    case Card::Play:
      if ((playDirtyMask & PLAY_DIRTY_MODE) == 0) {
        draftPlayMode = app.play.playMode;
      }
      if ((playDirtyMask & PLAY_DIRTY_BOOT) == 0) {
        draftBootMode = app.play.bootMode;
      }
      if ((playDirtyMask & PLAY_DIRTY_AUTOPLAY) == 0) {
        draftPlayAutoplayEnabled = app.settings.autoplayEnabled;
      }
      if ((playDirtyMask & PLAY_DIRTY_INTERVAL) == 0) {
        draftPlayAutoplayIntervalSeconds = app.settings.autoplayIntervalSeconds;
      }
      editValue = "";
      break;
    case Card::Sync:
      if ((syncDirtyMask & SYNC_DIRTY_ENABLED) == 0) {
        draftSyncEnabled = app.sync.enabled;
      }
      if ((syncDirtyMask & SYNC_DIRTY_GROUP) == 0) {
        draftSyncGroup = app.sync.group;
      }
      if ((syncDirtyMask & SYNC_DIRTY_ROLE) == 0) {
        draftSyncRole = app.sync.role;
      }
      if ((syncDirtyMask & SYNC_DIRTY_LOSS) == 0) {
        draftSyncLossBehavior = app.sync.lossBehavior;
      }
      editValue = "";
      break;
    case Card::Wireless:
      if ((wirelessDirtyMask & WIRELESS_DIRTY_ENABLED) == 0) {
        draftWirelessEnabled = app.wireless.enabled;
      }
      if ((wirelessDirtyMask & WIRELESS_DIRTY_PROFILE) == 0) {
        draftWirelessProfile = app.wireless.profile;
      }
      editValue = "";
      break;
    case Card::Config:
      editValue = "";
      if ((configDirtyMask & CONFIG_DIRTY_STRIP) == 0) {
        draftStripLength = app.settings.stripLength;
      }
      if ((configDirtyMask & CONFIG_DIRTY_SMOOTH) == 0) {
        draftSmoothing = app.settings.smoothing;
      }
      if ((configDirtyMask & CONFIG_DIRTY_ACCEL) == 0) {
        draftAccelRange = app.settings.accelRange;
      }
      if ((configDirtyMask & CONFIG_DIRTY_GYRO) == 0) {
        draftGyroRange = app.settings.gyroRange;
      }
      if ((configDirtyMask & CONFIG_DIRTY_AUTOPLAY) == 0) {
        draftAutoplayEnabled = app.settings.autoplayEnabled;
      }
      if ((configDirtyMask & CONFIG_DIRTY_INTERVAL) == 0) {
        draftAutoplayIntervalSeconds = app.settings.autoplayIntervalSeconds;
      }
      break;
    case Card::ActivePattern:
      if (!patternDirty) {
        draftActivePattern = app.settings.activePattern;
        editValue = showInt(app.settings.activePattern);
      }
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
  requestControllerBattery(true);
  if (app.protocolMode == ProtocolMode::Nk4) {
    if (static_cast<Card>(app.selectedCard) == Card::Device || static_cast<Card>(app.selectedCard) == Card::Status) {
      sendCommand(NightKiteCommands::refreshAll());
    } else if (static_cast<Card>(app.selectedCard) == Card::Play) {
      enqueueCommandEntry("cmd=get section=play", true);
      enqueueCommandEntry("cmd=status", true);
    } else if (static_cast<Card>(app.selectedCard) == Card::Sync) {
      enqueueCommandEntry("cmd=get section=sync", true);
      enqueueCommandEntry("cmd=sync_status", true);
    } else if (static_cast<Card>(app.selectedCard) == Card::Wireless) {
      enqueueCommandEntry("cmd=get section=wireless", true);
      enqueueCommandEntry("cmd=ble_status", true);
    } else if (static_cast<Card>(app.selectedCard) == Card::SyncTest) {
      queueSyncTestRefresh();
    } else if (static_cast<Card>(app.selectedCard) == Card::PatternList ||
               static_cast<Card>(app.selectedCard) == Card::PatternBulk) {
      enqueueCommandEntry("cmd=get section=patterns", true);
    } else if (static_cast<Card>(app.selectedCard) == Card::Calibration) {
      enqueueCommandEntry("cmd=timing", true);
      enqueueCommandEntry("cmd=sensor", true);
    } else {
      sendCommand(NightKiteCommands::refreshAll());
    }
  } else if (static_cast<Card>(app.selectedCard) == Card::PatternList || static_cast<Card>(app.selectedCard) == Card::PatternBulk) {
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
    case Card::Play:
      if (app.protocolMode != ProtocolMode::Nk4) {
        setStatus("NK4 required", COLOR_WARN);
      } else if (app.selectedPlayField == 0) {
        if ((playDirtyMask & PLAY_DIRTY_MODE) == 0) {
          draftPlayMode = app.play.playMode;
        }
        draftPlayMode = optionWithDelta(PLAY_MODES, PLAY_MODE_COUNT, draftPlayMode, delta);
        playDirtyMask |= PLAY_DIRTY_MODE;
      } else if (app.selectedPlayField == 1) {
        if ((playDirtyMask & PLAY_DIRTY_BOOT) == 0) {
          draftBootMode = app.play.bootMode;
        }
        draftBootMode = optionWithDelta(BOOT_MODES, BOOT_MODE_COUNT, draftBootMode, delta);
        playDirtyMask |= PLAY_DIRTY_BOOT;
      } else if (app.selectedPlayField == 2) {
        if ((playDirtyMask & PLAY_DIRTY_AUTOPLAY) == 0) {
          draftPlayAutoplayEnabled = app.settings.autoplayEnabled;
        }
        draftPlayAutoplayEnabled = !draftPlayAutoplayEnabled;
        playDirtyMask |= PLAY_DIRTY_AUTOPLAY;
      } else {
        if ((playDirtyMask & PLAY_DIRTY_INTERVAL) == 0) {
          draftPlayAutoplayIntervalSeconds = app.settings.autoplayIntervalSeconds;
        }
        draftPlayAutoplayIntervalSeconds =
            wrappedValue(autoplayIntervalLevels, AUTOPLAY_INTERVAL_LEVEL_COUNT, draftPlayAutoplayIntervalSeconds, delta);
        playDirtyMask |= PLAY_DIRTY_INTERVAL;
      }
      break;
    case Card::Sync:
      if (app.protocolMode != ProtocolMode::Nk4) {
        setStatus("NK4 required", COLOR_WARN);
      } else if (app.selectedSyncField == 0) {
        if ((syncDirtyMask & SYNC_DIRTY_ENABLED) == 0) {
          draftSyncEnabled = app.sync.enabled;
        }
        draftSyncEnabled = !draftSyncEnabled;
        syncDirtyMask |= SYNC_DIRTY_ENABLED;
      } else if (app.selectedSyncField == 1) {
        if ((syncDirtyMask & SYNC_DIRTY_GROUP) == 0) {
          draftSyncGroup = app.sync.group;
        }
        draftSyncGroup = wrapRange(draftSyncGroup < 0 ? 1 : draftSyncGroup, 1, 255, 1, delta);
        syncDirtyMask |= SYNC_DIRTY_GROUP;
      } else if (app.selectedSyncField == 2) {
        if ((syncDirtyMask & SYNC_DIRTY_ROLE) == 0) {
          draftSyncRole = app.sync.role;
        }
        draftSyncRole = optionWithDelta(SYNC_ROLES, SYNC_ROLE_COUNT, draftSyncRole, delta);
        syncDirtyMask |= SYNC_DIRTY_ROLE;
      } else if (app.selectedSyncField == 5) {
        if ((syncDirtyMask & SYNC_DIRTY_LOSS) == 0) {
          draftSyncLossBehavior = app.sync.lossBehavior;
        }
        draftSyncLossBehavior = optionWithDelta(SYNC_LOSS, SYNC_LOSS_COUNT, draftSyncLossBehavior, delta);
        syncDirtyMask |= SYNC_DIRTY_LOSS;
      }
      break;
    case Card::Wireless:
      if (app.protocolMode != ProtocolMode::Nk4) {
        setStatus("NK4 required", COLOR_WARN);
      } else if (app.selectedWirelessField == 0) {
        if ((wirelessDirtyMask & WIRELESS_DIRTY_ENABLED) == 0) {
          draftWirelessEnabled = app.wireless.enabled;
        }
        draftWirelessEnabled = !draftWirelessEnabled;
        wirelessDirtyMask |= WIRELESS_DIRTY_ENABLED;
      } else {
        if ((wirelessDirtyMask & WIRELESS_DIRTY_PROFILE) == 0) {
          draftWirelessProfile = app.wireless.profile;
        }
        draftWirelessProfile = optionWithDelta(WIRELESS_PROFILES, WIRELESS_PROFILE_COUNT, draftWirelessProfile, delta);
        wirelessDirtyMask |= WIRELESS_DIRTY_PROFILE;
      }
      break;
    case Card::SyncTest:
      app.selectedSyncTestAction = constrain(app.selectedSyncTestAction + delta, 0, SYNC_TEST_ACTION_COUNT - 1);
      break;
    case Card::Brightness:
      if (!brightnessDirty) {
        draftBrightness = app.settings.brightness;
      }
      draftBrightness = wrappedValue(brightnessLevels, BRIGHTNESS_LEVEL_COUNT, draftBrightness, delta);
      editValue = String(draftBrightness);
      brightnessDirty = true;
      break;
    case Card::Config:
      if (app.selectedConfigField == 0) {
        if ((configDirtyMask & CONFIG_DIRTY_STRIP) == 0) {
          draftStripLength = app.settings.stripLength;
        }
        draftStripLength = wrapRange(draftStripLength, 10, 35, 1, delta);
        configDirtyMask |= CONFIG_DIRTY_STRIP;
      } else if (app.selectedConfigField == 1) {
        if ((configDirtyMask & CONFIG_DIRTY_SMOOTH) == 0) {
          draftSmoothing = app.settings.smoothing;
        }
        draftSmoothing = wrappedValue(smoothingLevels, SMOOTHING_LEVEL_COUNT, draftSmoothing, delta);
        configDirtyMask |= CONFIG_DIRTY_SMOOTH;
      } else if (app.selectedConfigField == 2) {
        if ((configDirtyMask & CONFIG_DIRTY_ACCEL) == 0) {
          draftAccelRange = app.settings.accelRange;
        }
        draftAccelRange = wrappedValue(accelRangeLevels, ACCEL_RANGE_LEVEL_COUNT, draftAccelRange, delta);
        configDirtyMask |= CONFIG_DIRTY_ACCEL;
      } else if (app.selectedConfigField == 3) {
        if ((configDirtyMask & CONFIG_DIRTY_GYRO) == 0) {
          draftGyroRange = app.settings.gyroRange;
        }
        draftGyroRange = wrappedValue(gyroRangeLevels, GYRO_RANGE_LEVEL_COUNT, draftGyroRange, delta);
        configDirtyMask |= CONFIG_DIRTY_GYRO;
      } else if (app.selectedConfigField == 4) {
        if ((configDirtyMask & CONFIG_DIRTY_AUTOPLAY) == 0) {
          draftAutoplayEnabled = app.settings.autoplayEnabled;
        }
        draftAutoplayEnabled = !draftAutoplayEnabled;
        configDirtyMask |= CONFIG_DIRTY_AUTOPLAY;
      } else {
        if ((configDirtyMask & CONFIG_DIRTY_INTERVAL) == 0) {
          draftAutoplayIntervalSeconds = app.settings.autoplayIntervalSeconds;
        }
        draftAutoplayIntervalSeconds =
            wrappedValue(autoplayIntervalLevels, AUTOPLAY_INTERVAL_LEVEL_COUNT, draftAutoplayIntervalSeconds, delta);
        configDirtyMask |= CONFIG_DIRTY_INTERVAL;
      }
      break;
    case Card::ActivePattern:
      if (!patternDirty) {
        draftActivePattern = app.settings.activePattern;
      }
      draftActivePattern = wrapRange(draftActivePattern, 1, PATTERN_COUNT, 1, delta);
      editValue = String(draftActivePattern);
      patternDirty = true;
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
    case Card::Status:
    case Card::Device:
    default:
      break;
  }
  app.dirty = true;
}

void applyCurrentCard()
{
  switch (static_cast<Card>(app.selectedCard)) {
    case Card::Status:
    case Card::Device:
      refreshCurrentCard();
      break;
    case Card::Play:
      if (!requireNk4Controller()) {
        break;
      } else if (playDirtyMask == 0) {
        setStatus("No edit pending", COLOR_MUTED);
      } else {
        if (playDirtyMask & PLAY_DIRTY_MODE) {
          sendCommand("set play_mode=" + draftPlayMode);
        }
        if (playDirtyMask & PLAY_DIRTY_BOOT) {
          sendCommand("set boot_mode=" + draftBootMode);
        }
        if (playDirtyMask & PLAY_DIRTY_AUTOPLAY) {
          sendCommand(String("set autoplay=") + (draftPlayAutoplayEnabled ? "1" : "0"));
        }
        if (playDirtyMask & PLAY_DIRTY_INTERVAL) {
          sendCommand("set autoplay_interval=" + String(draftPlayAutoplayIntervalSeconds));
        }
      }
      break;
    case Card::Sync:
      if (!requireNk4Controller()) {
        break;
      } else if (syncDirtyMask == 0) {
        setStatus("No edit pending", COLOR_MUTED);
      } else {
        if (syncDirtyMask & SYNC_DIRTY_ENABLED) {
          sendCommand(String("set sync_enabled=") + (draftSyncEnabled ? "1" : "0"));
        }
        if (syncDirtyMask & SYNC_DIRTY_GROUP) {
          sendCommand("set sync_group=" + String(draftSyncGroup));
        }
        if (syncDirtyMask & SYNC_DIRTY_ROLE) {
          sendCommand("set sync_role=" + draftSyncRole);
        }
        if (syncDirtyMask & SYNC_DIRTY_LOSS) {
          sendCommand("set sync_loss_behavior=" + draftSyncLossBehavior);
        }
      }
      break;
    case Card::Wireless:
      if (!requireNk4Controller()) {
        break;
      } else if (wirelessDirtyMask == 0) {
        setStatus("No edit pending", COLOR_MUTED);
      } else {
        if (wirelessDirtyMask & WIRELESS_DIRTY_ENABLED) {
          sendCommand(String("set wireless_enabled=") + (draftWirelessEnabled ? "1" : "0"));
        }
        if (wirelessDirtyMask & WIRELESS_DIRTY_PROFILE) {
          sendCommand("set wireless_profile=" + draftWirelessProfile);
        }
      }
      break;
    case Card::SyncTest:
      if (app.selectedSyncTestAction == 4) {
        syncTestGroup = wrapRange(selectedSyncTestGroup(), 1, 4, 1, 1);
        setStatus("Sync group " + String(syncTestGroup), COLOR_ACCENT);
      } else if (app.selectedSyncTestAction == 5) {
        syncTestProfile = optionWithDelta(WIRELESS_PROFILES, WIRELESS_PROFILE_COUNT, selectedSyncTestProfile(), 1);
        setStatus("Wireless " + syncTestProfile, COLOR_ACCENT);
      } else if (!requireNk4Controller()) {
        break;
      } else if (app.selectedSyncTestAction == 0) {
        queueSyncTestRoleSetup("master", "NK-Master");
      } else if (app.selectedSyncTestAction == 1) {
        queueSyncTestRoleSetup("follower", "NK-Follower");
      } else if (app.selectedSyncTestAction == 2) {
        sendCommand("save");
        setStatus("Save queued", COLOR_ACCENT);
      } else if (app.selectedSyncTestAction == 3) {
        queueSyncTestRefresh();
      } else if (app.selectedSyncTestAction == 6) {
        sendCommand("set name=NK-Master");
        setStatus("Name Master sent", COLOR_ACCENT);
      } else if (app.selectedSyncTestAction == 7) {
        sendCommand("set name=NK-Follower");
        setStatus("Name Follower sent", COLOR_ACCENT);
      } else if (app.selectedSyncTestAction == 8) {
        sendCommand("set play_mode=sync");
        setStatus("Play SYNC sent", COLOR_ACCENT);
      }
      break;
    case Card::Brightness:
      if (!brightnessDirty) {
        setStatus("No edit pending", COLOR_MUTED);
      } else {
        sendCommand(NightKiteCommands::setBrightness(draftBrightness));
        if (app.protocolMode != ProtocolMode::Nk4) {
          brightnessDirty = false;
        }
      }
      break;
    case Card::Config:
      if (configDirtyMask == 0) {
        setStatus("No edit pending", COLOR_MUTED);
      }
      {
        uint8_t pendingConfigDirty = configDirtyMask;
        if (pendingConfigDirty & CONFIG_DIRTY_STRIP) {
          sendCommand(NightKiteCommands::setStripLength(draftStripLength));
          if (app.protocolMode != ProtocolMode::Nk4) {
            configDirtyMask &= ~CONFIG_DIRTY_STRIP;
          }
        }
        if (pendingConfigDirty & CONFIG_DIRTY_SMOOTH) {
          sendCommand(NightKiteCommands::setSmoothing(draftSmoothing));
          if (app.protocolMode != ProtocolMode::Nk4) {
            configDirtyMask &= ~CONFIG_DIRTY_SMOOTH;
          }
        }
        if (pendingConfigDirty & CONFIG_DIRTY_ACCEL) {
          sendCommand(NightKiteCommands::setAccelRange(draftAccelRange));
          if (app.protocolMode != ProtocolMode::Nk4) {
            configDirtyMask &= ~CONFIG_DIRTY_ACCEL;
          }
        }
        if (pendingConfigDirty & CONFIG_DIRTY_GYRO) {
          sendCommand(NightKiteCommands::setGyroRange(draftGyroRange));
          if (app.protocolMode != ProtocolMode::Nk4) {
            configDirtyMask &= ~CONFIG_DIRTY_GYRO;
          }
        }
        if (pendingConfigDirty & CONFIG_DIRTY_AUTOPLAY) {
          sendCommand(NightKiteCommands::setAutoplay(draftAutoplayEnabled));
          if (app.protocolMode != ProtocolMode::Nk4) {
            configDirtyMask &= ~CONFIG_DIRTY_AUTOPLAY;
          }
        }
        if (pendingConfigDirty & CONFIG_DIRTY_INTERVAL) {
          sendCommand(NightKiteCommands::setAutoplayInterval(draftAutoplayIntervalSeconds));
          if (app.protocolMode != ProtocolMode::Nk4) {
            configDirtyMask &= ~CONFIG_DIRTY_INTERVAL;
          }
        }
      }
      app.dirty = true;
      break;
    case Card::ActivePattern:
      if (!patternDirty) {
        setStatus("No edit pending", COLOR_MUTED);
      } else {
        sendCommand(NightKiteCommands::setPattern(draftActivePattern));
        if (app.protocolMode != ProtocolMode::Nk4) {
          patternDirty = false;
        }
      }
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
  markTransferCompleteSoundPending();
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
  transferCompleteSoundPending = false;
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
        soundManager.playSuccess();
        setStatus("Flash complete", COLOR_OK);
        setFlashState(FlashUiState::Success, "Flash complete");
      } else {
        app.flash.errorMessage = progress.message.length() > 0 ? progress.message : uf2Flasher.resultMessage();
        soundManager.playError();
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
  bool cycleChanged = detailCycle != pattern.cycleEnabled;
  bool invertChanged = detailInvert != pattern.inverted;
  pattern.cycleEnabled = detailCycle;
  pattern.inverted = detailInvert;
  if (cycleChanged) {
    sendCommand(NightKiteCommands::setPatternCycle(pattern.id, detailCycle));
  }
  if (invertChanged) {
    sendCommand(NightKiteCommands::setPatternInvert(pattern.id, detailInvert));
  }
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
      markTransferCompleteSoundPending();
      applyPatternMasks(ALL_PATTERN_MASK, 0, true, false);
      sendCommand(NightKiteCommands::setAllCycle(true));
      break;
    case 2:
      markTransferCompleteSoundPending();
      applyPatternMasks(0, 0, true, false);
      sendCommand(NightKiteCommands::setAllCycle(false));
      break;
    case 3:
      markTransferCompleteSoundPending();
      applyPatternMasks(0, ALL_PATTERN_MASK, false, true);
      sendCommand(NightKiteCommands::setAllInvert(true));
      break;
    case 4:
      markTransferCompleteSoundPending();
      applyPatternMasks(0, 0, false, true);
      sendCommand(NightKiteCommands::setAllInvert(false));
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
  if ((c == 'c' || c == 'C') && static_cast<Card>(app.selectedCard) == Card::Play) {
    app.selectedPlayField = (app.selectedPlayField + 1) % 4;
    app.dirty = true;
    return;
  }
  if ((c == 'c' || c == 'C') && static_cast<Card>(app.selectedCard) == Card::Sync) {
    const int editable[] = {0, 1, 2, 5};
    int current = 0;
    for (int i = 0; i < 4; ++i) {
      if (app.selectedSyncField == editable[i]) {
        current = i;
        break;
      }
    }
    app.selectedSyncField = editable[(current + 1) % 4];
    app.dirty = true;
    return;
  }
  if ((c == 'c' || c == 'C') && static_cast<Card>(app.selectedCard) == Card::Wireless) {
    app.selectedWirelessField = app.selectedWirelessField == 0 ? 1 : 0;
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

bool textInputActive()
{
  return mode == Mode::ProfileNameInput;
}

bool isPageChangeChar(char c)
{
  switch (c) {
    case 'a':
    case 'A':
    case 'd':
    case 'D':
    case ',':
    case '<':
    case '/':
    case '?':
      return true;
    default:
      return false;
  }
}

bool isLocalNavigationChar(char c)
{
  switch (c) {
    case 'w':
    case 'W':
    case 's':
    case 'S':
    case '.':
    case '>':
    case ';':
    case ':':
      return true;
    default:
      return false;
  }
}

void playKeyboardSound(const Keyboard_Class::KeysState& status)
{
  if (flashCopyAudioQuiet()) {
    return;
  }
  if (status.enter) {
    soundManager.playConfirm();
    return;
  }
  if (status.del || status.opt) {
    soundManager.playCancel();
    return;
  }
  if (textInputActive()) {
    if (!status.word.empty() || status.space) {
      soundManager.playTextKey();
    }
    return;
  }
  if (status.tab) {
    soundManager.playPageChange();
    return;
  }
  for (char c : status.word) {
    if (isPageChangeChar(c)) {
      soundManager.playPageChange();
      return;
    }
    if (isLocalNavigationChar(c)) {
      soundManager.playNavigate();
      return;
    }
  }
  if (!status.word.empty() || status.space) {
    soundManager.playTextKey();
  }
}

void handleKeyboard()
{
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return;
  }

  lastUserInputMs = millis();
  auto& status = M5Cardputer.Keyboard.keysState();
  playKeyboardSound(status);

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
    } else if (currentCardHasDirtyDraft()) {
      discardCurrentDraft();
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
  soundManager.begin();
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
  splashStartMs = millis();
  splashActive = true;
  startupSoundPlayed = false;
  app.dirty = true;
  Serial.println("startup: splash");
}

void loop()
{
  M5Cardputer.update();
  soundManager.update();

  if (splashActive) {
    unsigned long elapsed = millis() - splashStartMs;
    if (!startupSoundPlayed && elapsed >= STARTUP_SOUND_DELAY_MS) {
      Serial.println("sound: startup");
      soundManager.playStartup();
      startupSoundPlayed = true;
    }
    if (elapsed >= SPLASH_DURATION_MS) {
      splashActive = false;
      Serial.println("startup: done");
      sendCommand(NightKiteCommands::refreshAll());
      app.dirty = true;
    }
    render();
    delay(2);
    return;
  }

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
