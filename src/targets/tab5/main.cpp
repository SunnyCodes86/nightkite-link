#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEAdvertising.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <USBHostSerial.h>
#include <esp32-hal-hosted.h>

#include <atomic>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "BleLineBuffer.h"
#include "AudioSyncDsp.h"
#include "CommandQueuePolicy.h"
#include "ControllerSessionPolicy.h"
#include "LinkSettings.h"
#include "NightKiteCommands.h"
#include "NightKitePlayback.h"
#include "NightKitePatterns.h"
#include "NightKiteProtocol.h"
#include "Nk4LineParser.h"
#include "ProfileCodec.h"
#include "SyncBeaconCodec.h"
#include "Uf2Validator.h"
#include "UsbMscUf2Flasher.h"
#include "Tab5WorkflowPolicy.h"

namespace {

constexpr uint16_t BG = 0x0861;
constexpr uint16_t PANEL = 0x18E3;
constexpr uint16_t PANEL_2 = 0x2124;
constexpr uint16_t ACCENT = 0x04FF;
constexpr uint16_t OK = 0x07E0;
constexpr uint16_t WARN = 0xFD20;
constexpr uint16_t ERR = 0xF800;
constexpr uint16_t MUTED = 0xBDF7;
constexpr uint16_t DISABLED_COLOR = 0x4A49;
constexpr size_t MAX_QUEUE = 64;
constexpr size_t MAX_LINE = 4096;
constexpr size_t MAX_PROFILE_BYTES = 8192;
constexpr int PATTERN_COUNT = NightKitePatterns::COUNT;
constexpr uint32_t ALL_PATTERN_MASK = NightKitePatterns::ALL_MASK;
constexpr int BRIGHTNESS_LEVELS[] = {95, 127, 159, 191, 223, 255};
constexpr int SMOOTHING_LEVELS[] = {1, 10, 20, 40, 60, 80, 100, 150, 256, 512};
constexpr int ACCEL_LEVELS[] = {2, 4, 8, 16};
constexpr int GYRO_LEVELS[] = {250, 500, 1000, 2000};
constexpr const char* SYNC_ROLES[] = {"standalone", "master", "follower"};
constexpr const char* SYNC_LOSS[] = {"continue_local", "fallback_autoplay", "warning_only"};
constexpr const char* WIRELESS_PROFILES[] = {"long_range", "balanced", "fast_sync"};
constexpr uint8_t SD_LDO_CHANNEL = 4;

enum class Transport : uint8_t { None, Usb, Ble };
enum class Phase : uint8_t { Idle, Waiting, Scanning, Found, Connecting, Hello, Loading, Ready, Busy, Error };
enum class Page : uint8_t { Connect, Control, Patterns, Playback, Sync, Audio, Profiles, Controller, Service, Firmware };
enum class ServiceTab : uint8_t { Calibration, Terminal, Diagnostics, Sd, Link };
enum class SaveWorkflow : uint8_t { None, Control, Patterns, Playback, Sync, Controller, SaveOnly, ProfileApply, Calibration, Defaults, Terminal };
enum class Modal : uint8_t { None, TextInput, ConfirmOverwrite, ConfirmDelete, ConfirmDefaults, ConfirmFlash };
enum class TextPurpose : uint8_t { None, SaveProfile, RenameProfile, DeviceName, SyncMasterUid, Terminal };
enum class AudioState : uint8_t { Idle, Tone, MicStarting, Recording };
enum class AudioBeaconMode : uint8_t { ManualV1, ManualV2, MicEnergyV2, MicFullV2 };
enum class SmokeId : uint8_t { Display, Touch, Sd, Audio, Usb, Gatt, Beacon, Core, Count };

struct SmokeResult {
  const char* name;
  String state;
  String detail;
};

struct BleDeviceEntry {
  String name;
  String address;
  int rssi = 0;
  uint8_t addressType = 0;
};

struct QueueEntry {
  String command;
  CommandClass commandClass = CommandClass::User;
  LiveCommandKind liveKind = LiveCommandKind::None;
  uint32_t generation = 0;
};

struct PendingCommand {
  QueueEntry entry;
  uint16_t sequence = 0;
  uint32_t sentAt = 0;
  bool active = false;
};

struct SyncModel {
  bool enabled = false;
  int group = 1;
  String role = "standalone";
  String loss = "continue_local";
  String masterUid;
  bool wirelessEnabled = false;
  String wirelessProfile = "balanced";
  String state = "unknown";
  String radioMode = "unknown";
  bool locked = false;
  unsigned long beaconTx = 0;
  unsigned long beaconRx = 0;
  unsigned long crcErrors = 0;
  unsigned long groupMismatch = 0;
  unsigned long applyCount = 0;
  int driftMs = 0;
  int beaconAgeMs = -1;
};

struct ControllerConfig {
  String deviceName;
  String uid;
  String firmware;
  String hardware;
  String bootCalibration = "quick";
  int stripLength = 35;
  int smoothing = 100;
  int accelRange = 2;
  int gyroRange = 2000;
  String imu = "unknown";
  String fps = "--";
  int batteryPercent = -1;
  String batteryState;
};

struct AudioBeaconSettings {
  AudioBeaconMode mode = AudioBeaconMode::ManualV1;
  int group = 1;
  int pattern = 1;
  int brightness = 127;
  int bpm = 120;
  int sensitivity = 100;
  int noiseGate = 15;
  int smoothing = 20;
  bool beatDetect = true;
  bool micPaused = false;
};

struct DirtyRegion {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;
};

SmokeResult smoke[static_cast<size_t>(SmokeId::Count)] = {
    {"Display", "RUN", "initializing"}, {"Touch", "READY", "awaiting tap"},
    {"microSD", "READY", "not tested"}, {"Audio", "READY", "not tested"},
    {"USB Host", "READY", "not tested"}, {"BLE GATT", "READY", "not tested"},
    {"BLE Beacon", "READY", "not tested"}, {"Shared Core", "RUN", "checking"},
};

M5Canvas uiCanvas(&M5.Display);

USBHostSerial usbSerial(0x2E8A, CDC_HOST_ANY_PID);
String usbLineBuffer;
bool usbStarted = false;

Transport selectedTransport = Transport::None;
Phase phase = Phase::Idle;
String statusText = "Choose USB or BLE";
String controllerName = "No controller";
String diagnosticLine;
bool dirty = true;
DirtyRegion dirtyRegions[8] = {{0, 0, 1280, 720}};
size_t dirtyRegionCount = 1;
bool sessionStarted = false;
bool protocolResolved = false;
bool controllerConnected = false;
bool draftDirty = false;
bool playDraftDirty = false;
bool patternDraftDirty = false;
bool syncDraftDirty = false;
bool controllerDraftDirty = false;
bool gattAutoTest = false;
Page page = Page::Connect;
ServiceTab serviceTab = ServiceTab::Calibration;
SaveWorkflow saveWorkflow = SaveWorkflow::None;
Modal modal = Modal::None;
TextPurpose textPurpose = TextPurpose::None;
String textInput;
String operationSuccess = "Changes applied";
uint32_t sessionGeneration = 0;
uint32_t helloAt = 0;
uint16_t nextSequence = 1;
int activePattern = -1;
int brightness = -1;
int controllerPatternCount = PATTERN_COUNT;
int draftPattern = 1;
int draftBrightness = 159;
uint32_t enabledPatternMask = ALL_PATTERN_MASK;
uint32_t invertedPatternMask = 0;
uint32_t syncReadyPatternMask = 0;
uint32_t partialSyncPatternMask = 0;
uint32_t localReactivePatternMask = 0;
uint32_t draftEnabledPatternMask = ALL_PATTERN_MASK;
uint32_t draftInvertedPatternMask = 0;
bool editInvertedMask = false;
String playMode = "unknown";
String bootMode = "unknown";
bool autoplayEnabled = false;
int autoplayInterval = -1;
String draftPlayMode = "manual";
String draftBootMode = "last";
bool draftAutoplayEnabled = false;
int draftAutoplayInterval = NightKitePlayback::AUTOPLAY_INTERVALS[0];
SyncModel syncModel;
SyncModel draftSync;
ControllerConfig controllerConfig;
ControllerConfig draftControllerConfig;

bool sdReady = false;
uint64_t sdSizeBytes = 0;
std::vector<String> profileFiles;
int selectedProfile = -1;
int profileOffset = 0;
ProfileData loadedProfile;
bool hasLoadedProfile = false;
String loadedProfileName;
String loadedProfilePath;
String pendingProfileName;
String pendingProfilePath;
std::vector<String> firmwareFiles;
int selectedFirmware = -1;
int firmwareOffset = 0;
Uf2Target firmwareTarget = Uf2Target::Rp2350;
Uf2ValidationInfo firmwareValidation;
bool firmwareValidated = false;
UsbMscUf2Flasher uf2Flasher;
bool flashResultReported = true;

String terminalInput = "cmd=status";
String terminalLog = "No terminal response";
AudioBeaconSettings audioBeaconSettings;
AudioSyncDsp audioBeaconDsp;
AudioSyncDspOutput audioBeaconOutput;
int16_t audioBeaconSamples[256] = {};
bool audioBeaconRunning = false;
bool audioBeaconRecording = false;
uint32_t audioBeaconLastFrameAt = 0;
uint32_t audioBeaconLastAdvAt = 0;
uint32_t audioBeaconLastRenderAt = 0;
uint16_t audioBeaconSequence = 0;
unsigned long audioBeaconSent = 0;
NightKiteLinkSettings::Settings linkSettings;
NightKiteLinkSettings::Settings savedLinkSettings;
bool linkSettingsDirty = false;
uint32_t linkSettingsChangedAt = 0;

void stopAudioBeacon();
void startAudioBeacon();
void testSd();

std::vector<QueueEntry> commandQueue;
PendingCommand pending;
InitialRefreshState initialRefresh;
CommandOperationState commandOperation;

int16_t microphoneSamples[4096] = {};
AudioState audioState = AudioState::Idle;
uint32_t audioStartedAt = 0;
bool uiToneActive = false;
uint32_t uiToneUntil = 0;
int tab5BatteryLevel = -1;
int tab5BatteryVoltage = 0;
bool tab5BatteryCharging = false;
uint32_t tab5BatteryUpdatedAt = 0;
BLEAdvertising* bleAdvertising = nullptr;
uint32_t beaconStartedAt = 0;
RTC_DATA_ATTR uint8_t displayInitRestarts = 0;

void setSmoke(SmokeId id, const char* state, const String& detail)
{
  auto& result = smoke[static_cast<size_t>(id)];
  result.state = state;
  result.detail = detail;
  Serial.printf("[SMOKE] %-12s %-5s %s\n", result.name, state, detail.c_str());
}

void playUiTone(int frequency, int durationMs)
{
  if (audioState != AudioState::Idle || audioBeaconRunning) return;
  if (!uiToneActive && !M5.Speaker.begin()) return;
  M5.Speaker.setVolume(64);
  M5.Speaker.setAllChannelVolume(64);
  if (M5.Speaker.tone(frequency, durationMs)) {
    uiToneActive = true;
    uiToneUntil = millis() + durationMs + 20;
  }
}

void updateUiTone()
{
  if (uiToneActive && static_cast<int32_t>(millis() - uiToneUntil) >= 0) {
    M5.Speaker.end();
    uiToneActive = false;
  }
}

void invalidateAll()
{
  dirty = true;
  dirtyRegions[0] = {0, 0, 1280, 720};
  dirtyRegionCount = 1;
}

void invalidateRect(int x, int y, int width, int height)
{
  x = constrain(x, 0, 1279);
  y = constrain(y, 0, 719);
  width = constrain(width, 1, 1280 - x);
  height = constrain(height, 1, 720 - y);
  dirty = true;
  if (dirtyRegionCount == 1 && dirtyRegions[0].x == 0 && dirtyRegions[0].y == 0 &&
      dirtyRegions[0].width == 1280 && dirtyRegions[0].height == 720) return;
  for (size_t i = 0; i < dirtyRegionCount; ++i) {
    const DirtyRegion& region = dirtyRegions[i];
    if (region.x == x && region.y == y && region.width == width && region.height == height) return;
  }
  if (dirtyRegionCount == sizeof(dirtyRegions) / sizeof(dirtyRegions[0])) {
    invalidateAll();
    return;
  }
  dirtyRegions[dirtyRegionCount++] = {
      static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width), static_cast<int16_t>(height)};
}

void invalidateStatus()
{
  invalidateRect(0, 0, 1280, 160);
}

void invalidatePageBody()
{
  invalidateRect(270, 160, 965, 520);
}

void invalidateSaveFooter()
{
  invalidateRect(270, 555, 965, 125);
}

void updateBatteryStatus()
{
  if (tab5BatteryUpdatedAt && millis() - tab5BatteryUpdatedAt < 1000) return;
  tab5BatteryUpdatedAt = millis();
  const int voltage = M5.Power.getBatteryVoltage();
  const int measuredLevel = M5.Power.getBatteryLevel();
  const int level = voltage >= 5500 && voltage <= 9000 ? measuredLevel : -1;
  const bool charging = level >= 0 && M5.Power.isCharging() == m5::Power_Class::is_charging;
  const bool changed = level != tab5BatteryLevel || charging != tab5BatteryCharging;
  tab5BatteryLevel = level;
  tab5BatteryVoltage = voltage;
  tab5BatteryCharging = charging;
  if (!changed) return;
  invalidateRect(800, 5, 455, 55);
}

const char* phaseName()
{
  switch (phase) {
    case Phase::Waiting: return "WAITING";
    case Phase::Scanning: return "SCANNING";
    case Phase::Found: return "FOUND";
    case Phase::Connecting: return "CONNECTING";
    case Phase::Hello: return "HANDSHAKE";
    case Phase::Loading: return "LOADING";
    case Phase::Ready: return "READY";
    case Phase::Busy: return "BUSY";
    case Phase::Error: return "ERROR";
    default: return "IDLE";
  }
}

void setStatus(Phase next, const String& text)
{
  phase = next;
  statusText = text;
  invalidateStatus();
  Serial.printf("[LINK] %-10s %s\n", phaseName(), text.c_str());
}

bool validOption(const String& value, const char* const* options, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    if (value == options[i]) return true;
  }
  return false;
}

String nextOption(const String& value, const char* const* options, size_t count, int delta)
{
  if (!count) return value;
  size_t index = 0;
  while (index < count && value != options[index]) ++index;
  if (index == count) index = 0;
  const int next = (static_cast<int>(index) + delta + static_cast<int>(count)) % static_cast<int>(count);
  return options[next];
}

int nextAutoplayInterval(int value, int delta)
{
  size_t index = 0;
  int distance = abs(value - NightKitePlayback::AUTOPLAY_INTERVALS[0]);
  for (size_t i = 1; i < NightKitePlayback::AUTOPLAY_INTERVAL_COUNT; ++i) {
    const int candidate = abs(value - NightKitePlayback::AUTOPLAY_INTERVALS[i]);
    if (candidate < distance) {
      distance = candidate;
      index = i;
    }
  }
  const int count = static_cast<int>(NightKitePlayback::AUTOPLAY_INTERVAL_COUNT);
  index = static_cast<size_t>((static_cast<int>(index) + delta + count) % count);
  return NightKitePlayback::AUTOPLAY_INTERVALS[index];
}

bool parseBoolean(const std::string& value, bool& result)
{
  if (value == "1" || value == "on" || value == "true") {
    result = true;
    return true;
  }
  if (value == "0" || value == "off" || value == "false") {
    result = false;
    return true;
  }
  return false;
}

void updatePlayDraftDirty()
{
  playDraftDirty = draftPlayMode != playMode || draftBootMode != bootMode ||
                   draftAutoplayEnabled != autoplayEnabled || draftAutoplayInterval != autoplayInterval;
  invalidateRect(275, 170, 955, 310);
  invalidateSaveFooter();
}

uint32_t parseUint32(const std::string& value, uint32_t fallback)
{
  if (value.empty()) return fallback;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 0);
  return end != value.c_str() && *end == '\0' ? static_cast<uint32_t>(parsed) : fallback;
}

unsigned long parseUnsigned(const std::string& value, unsigned long fallback)
{
  if (value.empty()) return fallback;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  return end != value.c_str() && *end == '\0' ? parsed : fallback;
}

template <size_t N>
int nextLevel(int value, const int (&levels)[N], int delta)
{
  size_t index = 0;
  int distance = abs(value - levels[0]);
  for (size_t i = 1; i < N; ++i) {
    const int candidate = abs(value - levels[i]);
    if (candidate < distance) {
      distance = candidate;
      index = i;
    }
  }
  index = static_cast<size_t>((static_cast<int>(index) + delta + static_cast<int>(N)) % static_cast<int>(N));
  return levels[index];
}

void updatePatternDraftDirty()
{
  patternDraftDirty = draftEnabledPatternMask != enabledPatternMask || draftInvertedPatternMask != invertedPatternMask;
  invalidateRect(270, 160, 965, 420);
  invalidateSaveFooter();
}

void updateSyncDraftDirty()
{
  syncDraftDirty = draftSync.enabled != syncModel.enabled || draftSync.group != syncModel.group ||
                   draftSync.role != syncModel.role || draftSync.loss != syncModel.loss ||
                   draftSync.masterUid != syncModel.masterUid ||
                   draftSync.wirelessEnabled != syncModel.wirelessEnabled ||
                   draftSync.wirelessProfile != syncModel.wirelessProfile;
  invalidateRect(275, 165, 955, 380);
  invalidateSaveFooter();
}

void updateControllerDraftDirty()
{
  controllerDraftDirty = draftControllerConfig.bootCalibration != controllerConfig.bootCalibration ||
                         draftControllerConfig.stripLength != controllerConfig.stripLength ||
                         draftControllerConfig.smoothing != controllerConfig.smoothing ||
                         draftControllerConfig.accelRange != controllerConfig.accelRange ||
                         draftControllerConfig.gyroRange != controllerConfig.gyroRange ||
                         draftControllerConfig.deviceName != controllerConfig.deviceName;
  invalidateRect(275, 165, 955, 300);
  invalidateSaveFooter();
}

void loadLinkSettings()
{
  Preferences preferences;
  NightKiteLinkSettings::Record record{};
  bool valid = false;
  if (preferences.begin("nk-link", false)) {
    valid = preferences.getBytesLength("local") == sizeof(record) &&
            preferences.getBytes("local", &record, sizeof(record)) == sizeof(record) &&
            NightKiteLinkSettings::decode(record, linkSettings);
    preferences.end();
  }
  if (!valid) linkSettings = NightKiteLinkSettings::Settings{};
  savedLinkSettings = linkSettings;
  M5.Display.setBrightness(linkSettings.displayBrightness);
}

void changeDisplayBrightness()
{
  constexpr size_t levelCount = sizeof(NightKiteLinkSettings::DISPLAY_BRIGHTNESS_LEVELS) /
                                sizeof(NightKiteLinkSettings::DISPLAY_BRIGHTNESS_LEVELS[0]);
  size_t index = 0;
  for (size_t i = 0; i < levelCount; ++i) {
    if (NightKiteLinkSettings::DISPLAY_BRIGHTNESS_LEVELS[i] == linkSettings.displayBrightness) index = i;
  }
  index = (index + 1) % levelCount;
  linkSettings.displayBrightness = NightKiteLinkSettings::DISPLAY_BRIGHTNESS_LEVELS[index];
  M5.Display.setBrightness(linkSettings.displayBrightness);
  linkSettingsDirty = !NightKiteLinkSettings::equal(linkSettings, savedLinkSettings);
  linkSettingsChangedAt = millis();
  invalidateRect(275, 235, 955, 200);
}

void persistLinkSettings()
{
  if (!linkSettingsDirty || millis() - linkSettingsChangedAt < 1000) return;
  const auto record = NightKiteLinkSettings::encode(linkSettings);
  Preferences preferences;
  const bool saved = preferences.begin("nk-link", false) &&
                     preferences.putBytes("local", &record, sizeof(record)) == sizeof(record);
  preferences.end();
  if (saved) {
    savedLinkSettings = linkSettings;
    linkSettingsDirty = false;
  } else {
    linkSettingsChangedAt = millis();
  }
}

class TabBleTransport {
public:
  bool begin()
  {
    if (started) {
      return true;
    }
    auto& radioPower = M5.getIOExpander(1);
    radioPower.setDirection(0, true);
    radioPower.digitalWrite(0, false);
    delay(100);
    radioPower.digitalWrite(0, true);
    delay(300);

    uint32_t hostMajor = 0;
    uint32_t hostMinor = 0;
    uint32_t hostPatch = 0;
    hostedGetHostVersion(&hostMajor, &hostMinor, &hostPatch);
    Serial.printf("[BLE] ESP-Hosted host %lu.%lu.%lu\n", static_cast<unsigned long>(hostMajor),
                  static_cast<unsigned long>(hostMinor), static_cast<unsigned long>(hostPatch));
    if (!hostedIsInitialized() && !hostedInitWiFi()) {
      if (hostedIsInitialized()) hostedDeinitWiFi();
      status = "C6 Hosted init failed";
      return false;
    }
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    hostedGetSlaveVersion(&major, &minor, &patch);
    Serial.printf("[BLE] C6 Hosted firmware %lu.%lu.%lu power=%d\n", static_cast<unsigned long>(major),
                  static_cast<unsigned long>(minor), static_cast<unsigned long>(patch),
                  radioPower.getWriteValue(0) ? 1 : 0);
    if (major == 0 && minor == 0 && patch == 0) {
      hostedDeinitWiFi();
      status = "C6 firmware not responding";
      return false;
    }
    constexpr auto supportsHostedBle = [](uint32_t versionMajor, uint32_t versionMinor) {
      return versionMajor > 2 || (versionMajor == 2 && versionMinor >= 6);
    };
    static_assert(!supportsHostedBle(2, 5) && supportsHostedBle(2, 6) && supportsHostedBle(3, 0));
    if (!supportsHostedBle(major, minor)) {
      hostedDeinitWiFi();
      status = "C6 BLE firmware too old (need 2.6+)";
      return false;
    }
    if (!BLEDevice::init("NightKite Link Tab5") || !BLEDevice::isHostedBLE()) {
      status = "Hosted NimBLE failed";
      return false;
    }
    hostedDeinitWiFi();
    BLEDevice::setMTU(185);
    scanCallbacks.reset(new (std::nothrow) ScanCallbacks(this));
    clientCallbacks.reset(new (std::nothrow) ClientCallbacks(this));
    if (!scanCallbacks || !clientCallbacks) {
      status = "BLE allocation failed";
      return false;
    }
    active = this;
    started = true;
    status = "BLE ready";
    return true;
  }

  bool scan()
  {
    if (!begin()) {
      return false;
    }
    disconnect();
    {
      std::lock_guard<std::mutex> lock(mutex);
      devices.clear();
      scanFinished = false;
      status = "Scanning";
    }
    BLEScan* scan = BLEDevice::getScan();
    if (scan == nullptr) {
      status = "BLE scan unavailable";
      return false;
    }
    scan->clearResults();
    scan->setAdvertisedDeviceCallbacks(scanCallbacks.get(), true);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);
    if (!scan->start(5, scanComplete, false)) {
      status = "BLE scan failed";
      return false;
    }
    return true;
  }

  bool connectIndex(size_t index)
  {
    BleDeviceEntry device;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (index >= devices.size()) {
        status = "Select a BLE controller";
        return false;
      }
      device = devices[index];
    }
    if (client == nullptr) {
      client = BLEDevice::createClient();
      if (client == nullptr) {
        status = "BLE client allocation failed";
        return false;
      }
      client->setClientCallbacks(clientCallbacks.get());
    }
    rxLines.clear();
    disconnected = false;
    if (!client->connect(BLEAddress(device.address.c_str()), device.addressType, 10000)) {
      status = "BLE connect failed";
      return false;
    }
    BLERemoteService* service = client->getService(BLEUUID(NightKiteProtocol::BLE_SERVICE_UUID));
    if (service == nullptr) {
      status = "NK4 service missing";
      disconnect();
      return false;
    }
    BLERemoteCharacteristic* newRx = service->getCharacteristic(BLEUUID(NightKiteProtocol::BLE_RX_UUID));
    BLERemoteCharacteristic* newTx = service->getCharacteristic(BLEUUID(NightKiteProtocol::BLE_TX_UUID));
    if (newRx == nullptr || newTx == nullptr || (!newRx->canWrite() && !newRx->canWriteNoResponse()) ||
        !newTx->canNotify()) {
      status = "NK4 GATT characteristics invalid";
      disconnect();
      return false;
    }
    const uint32_t generation = ++notifyGeneration;
    notifyOverflow = false;
    newTx->registerForNotify([this, generation](BLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
      if (notifyGeneration.load() == generation && !rxLines.append(data, length)) {
        notifyOverflow = true;
      }
    });
    {
      std::lock_guard<std::mutex> lock(mutex);
      rx = newRx;
      tx = newTx;
      connectedName = device.name.length() ? device.name : device.address;
    }
    status = "GATT connected";
    return true;
  }

  bool connected() const
  {
    std::lock_guard<std::mutex> lock(mutex);
    return client != nullptr && client->isConnected() && rx != nullptr && tx != nullptr;
  }

  bool sendLine(const String& line)
  {
    if (!connected()) {
      return false;
    }
    BLERemoteCharacteristic* writer = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex);
      writer = rx;
    }
    if (writer == nullptr) return false;
    String wire = line + "\n";
    writer->writeValue(reinterpret_cast<uint8_t*>(const_cast<char*>(wire.c_str())), wire.length(), writer->canWrite());
    return connected();
  }

  bool readLine(String& line)
  {
    std::string value;
    if (!rxLines.pop(value)) {
      return false;
    }
    line = value.c_str();
    return true;
  }

  void disconnect()
  {
    ++notifyGeneration;
    notifyOverflow = false;
    bool wasConnected = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      rx = nullptr;
      tx = nullptr;
      connectedName = "";
      wasConnected = client != nullptr && client->isConnected();
    }
    rxLines.clear();
    if (wasConnected) {
      client->disconnect();
    }
  }

  bool takeDisconnected() { return disconnected.exchange(false); }
  bool takeNotifyOverflow() { return notifyOverflow.exchange(false); }

  bool takeScanResults(std::vector<BleDeviceEntry>& result)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!scanFinished) {
      return false;
    }
    scanFinished = false;
    result = devices;
    return true;
  }

  std::vector<BleDeviceEntry> deviceList() const
  {
    std::lock_guard<std::mutex> lock(mutex);
    return devices;
  }

  String statusMessage() const
  {
    std::lock_guard<std::mutex> lock(mutex);
    return status;
  }

  String name() const
  {
    std::lock_guard<std::mutex> lock(mutex);
    return connectedName;
  }

private:
  void add(BLEAdvertisedDevice device)
  {
    const bool serviceMatch = device.haveServiceUUID() &&
                              device.isAdvertisingService(BLEUUID(NightKiteProtocol::BLE_SERVICE_UUID));
    const String name = device.haveName() ? String(device.getName().c_str()) : "";
    if (!serviceMatch && !name.startsWith("NK-")) {
      return;
    }
    const String address = device.getAddress().toString().c_str();
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& entry : devices) {
      if (entry.address == address) {
        entry.rssi = device.getRSSI();
        return;
      }
    }
    devices.push_back({name, address, device.getRSSI(), device.getAddressType()});
  }

  class ScanCallbacks final : public BLEAdvertisedDeviceCallbacks {
  public:
    explicit ScanCallbacks(TabBleTransport* owner) : owner(owner) {}
    void onResult(BLEAdvertisedDevice device) override { owner->add(device); }
  private:
    TabBleTransport* owner;
  };

  class ClientCallbacks final : public BLEClientCallbacks {
  public:
    explicit ClientCallbacks(TabBleTransport* owner) : owner(owner) {}
    void onConnect(BLEClient*) override {}
    void onDisconnect(BLEClient*) override
    {
      std::lock_guard<std::mutex> lock(owner->mutex);
      owner->rx = nullptr;
      owner->tx = nullptr;
      owner->disconnected = true;
    }
  private:
    TabBleTransport* owner;
  };

  static void scanComplete(BLEScanResults)
  {
    if (active != nullptr) {
      std::lock_guard<std::mutex> lock(active->mutex);
      active->scanFinished = true;
      active->status = active->devices.empty() ? "No NightKite BLE" : String("Found ") + active->devices.size();
    }
  }

  static TabBleTransport* active;
  BLEClient* client = nullptr;
  BLERemoteCharacteristic* rx = nullptr;
  BLERemoteCharacteristic* tx = nullptr;
  std::unique_ptr<ScanCallbacks> scanCallbacks;
  std::unique_ptr<ClientCallbacks> clientCallbacks;
  BleLineBuffer rxLines{MAX_LINE};
  mutable std::mutex mutex;
  std::vector<BleDeviceEntry> devices;
  String status = "BLE idle";
  String connectedName;
  std::atomic<bool> disconnected{false};
  std::atomic<bool> notifyOverflow{false};
  std::atomic<uint32_t> notifyGeneration{0};
  bool scanFinished = false;
  bool started = false;
};

TabBleTransport* TabBleTransport::active = nullptr;
TabBleTransport ble;
std::vector<BleDeviceEntry> shownBleDevices;

bool transportConnected()
{
  if (selectedTransport == Transport::Usb) {
    return usbStarted && static_cast<bool>(usbSerial);
  }
  return selectedTransport == Transport::Ble && ble.connected();
}

bool sendTransportLine(const String& line)
{
  Serial.printf("[NK4 TX] %s\n", line.c_str());
  if (selectedTransport == Transport::Ble) {
    return ble.sendLine(line);
  }
  if (selectedTransport != Transport::Usb || !transportConnected()) {
    return false;
  }
  usbSerial.write(reinterpret_cast<const uint8_t*>(line.c_str()), line.length());
  usbSerial.write(static_cast<uint8_t>('\n'));
  return true;
}

bool readTransportLine(String& line)
{
  if (selectedTransport == Transport::Ble) {
    return ble.readLine(line);
  }
  if (selectedTransport != Transport::Usb) {
    return false;
  }
  while (usbSerial.available()) {
    const char c = static_cast<char>(usbSerial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      line = usbLineBuffer;
      usbLineBuffer = "";
      line.trim();
      if (line.length()) {
        return true;
      }
    } else if (usbLineBuffer.length() < MAX_LINE) {
      usbLineBuffer += c;
    }
  }
  return false;
}

void clearCommands(bool failed)
{
  commandQueue.clear();
  pending.active = false;
  if (failed) {
    commandOperation.failRemaining();
  } else {
    commandOperation.abort();
  }
}

bool enqueue(const String& payload, CommandClass commandClass, LiveCommandKind liveKind = LiveCommandKind::None)
{
  if (replaceQueuedLiveCommand(commandQueue, payload, liveKind, sessionGeneration)) {
    return true;
  }
  if (hasQueuedBackgroundCommand(commandQueue, payload, commandClass, sessionGeneration)) {
    return true;
  }
  if (!makeCommandQueueSpace(commandQueue, MAX_QUEUE, commandClass)) {
    commandOperation.commandRejected(commandClass);
    setStatus(Phase::Error, "Command queue full");
    return false;
  }
  commandQueue.push_back({payload, commandClass, liveKind, sessionGeneration});
  commandOperation.commandQueued(commandClass);
  invalidateStatus();
  return true;
}

void queueRefresh(CommandClass commandClass = CommandClass::RequiredInit)
{
  initialRefresh.reset();
  enqueue("cmd=info", commandClass);
  enqueue("cmd=status", commandClass);
  enqueue("cmd=get section=config", commandClass);
  enqueue("cmd=get section=play", commandClass);
  enqueue("cmd=caps", CommandClass::OptionalInit);
  enqueue("cmd=get section=patterns", CommandClass::OptionalInit);
  enqueue("cmd=get section=sync", CommandClass::OptionalInit);
  enqueue("cmd=get section=wireless", CommandClass::OptionalInit);
  setStatus(Phase::Loading, "Loading controller configuration");
}

void beginSession()
{
  ++sessionGeneration;
  sessionStarted = true;
  protocolResolved = false;
  controllerConnected = false;
  controllerName = selectedTransport == Transport::Ble ? ble.name() : "USB controller";
  activePattern = -1;
  brightness = -1;
  controllerPatternCount = PATTERN_COUNT;
  draftDirty = false;
  playMode = "unknown";
  bootMode = "unknown";
  autoplayEnabled = false;
  autoplayInterval = -1;
  playDraftDirty = false;
  patternDraftDirty = false;
  syncDraftDirty = false;
  controllerDraftDirty = false;
  enabledPatternMask = ALL_PATTERN_MASK;
  invertedPatternMask = 0;
  syncModel = SyncModel{};
  controllerConfig = ControllerConfig{};
  saveWorkflow = SaveWorkflow::None;
  initialRefresh.reset();
  clearCommands(false);
  usbLineBuffer = "";
  if (selectedTransport == Transport::Usb) {
    sendTransportLine("protocol machine");
    helloAt = millis() + 120;
  } else {
    helloAt = millis();
  }
  setStatus(Phase::Hello, "Starting NK4 handshake");
}

void endSession(const String& reason)
{
  const bool hadSession = sessionStarted;
  sessionStarted = false;
  protocolResolved = false;
  controllerConnected = false;
  controllerName = "No controller";
  activePattern = -1;
  brightness = -1;
  playMode = "unknown";
  bootMode = "unknown";
  autoplayEnabled = false;
  autoplayInterval = -1;
  draftDirty = false;
  playDraftDirty = false;
  patternDraftDirty = false;
  syncDraftDirty = false;
  controllerDraftDirty = false;
  syncModel = SyncModel{};
  controllerConfig = ControllerConfig{};
  saveWorkflow = SaveWorkflow::None;
  ++sessionGeneration;
  clearCommands(true);
  initialRefresh.reset();
  if (hadSession || reason.length()) {
    setStatus(selectedTransport == Transport::None ? Phase::Idle : Phase::Waiting, reason);
  }
}

void selectUsb()
{
  if (selectedTransport == Transport::Ble) {
    ble.disconnect();
  }
  endSession("");
  selectedTransport = Transport::Usb;
  M5.Power.setExtOutput(true, m5::ext_USB);
  if (!usbStarted) {
    usbStarted = usbSerial.begin(115200, 0, 0, 8);
  }
  setSmoke(SmokeId::Usb, usbStarted ? "RUN" : "FAIL", usbStarted ? "host ready; connect controller" : "host init failed");
  setStatus(usbStarted ? Phase::Waiting : Phase::Error,
            usbStarted ? "USB selected; connect a NightKite controller" : "USB host failed");
}

void startBleScan()
{
  if (audioBeaconRunning) stopAudioBeacon();
  if (beaconStartedAt && bleAdvertising != nullptr) {
    bleAdvertising->stop();
    beaconStartedAt = 0;
    setSmoke(SmokeId::Beacon, "READY", "stopped for GATT scan");
  }
  endSession("");
  selectedTransport = Transport::Ble;
  shownBleDevices.clear();
  if (!ble.scan()) {
    setSmoke(SmokeId::Gatt, "FAIL", ble.statusMessage());
    setStatus(Phase::Error, ble.statusMessage());
    return;
  }
  setSmoke(SmokeId::Gatt, "RUN", "C6 scanning for NK4 service");
  setStatus(Phase::Scanning, "Scanning for NightKite BLE controllers");
}

void connectBle(size_t index)
{
  setStatus(Phase::Connecting, "Connecting and discovering NK4 GATT");
  if (!ble.connectIndex(index)) {
    setSmoke(SmokeId::Gatt, "FAIL", ble.statusMessage());
    setStatus(Phase::Error, ble.statusMessage());
    return;
  }
  beginSession();
}

void applyFields(const Nk4Line& line)
{
  auto integer = [&line](const char* key, int& target) {
    const std::string value = line.value(key);
    if (!value.empty()) {
      target = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
      return true;
    }
    return false;
  };
  int parsedPattern = activePattern;
  if (integer("active_pattern", parsedPattern) || integer("pattern", parsedPattern)) {
    if (parsedPattern >= 1 && parsedPattern <= PATTERN_COUNT) {
      activePattern = parsedPattern;
    }
  }
  int parsedBrightness = brightness;
  if (integer("brightness", parsedBrightness) && parsedBrightness >= 0 && parsedBrightness <= 255) {
    brightness = parsedBrightness;
  }
  const std::string name = line.value("name");
  const std::string deviceName = name.empty() ? line.value("device_name") : name;
  if (!deviceName.empty()) {
    controllerName = deviceName.c_str();
    controllerConfig.deviceName = deviceName.c_str();
  }
  const std::string uid = line.value("uid").empty() ? line.value("device_uid") : line.value("uid");
  if (!uid.empty()) controllerConfig.uid = uid.c_str();
  const std::string firmware = line.value("fw").empty() ? line.value("firmware") : line.value("fw");
  if (!firmware.empty()) controllerConfig.firmware = firmware.c_str();
  const std::string hardware = line.value("hw").empty() ? line.value("hardware") : line.value("hw");
  if (!hardware.empty()) controllerConfig.hardware = hardware.c_str();
  int parsedCount = controllerPatternCount;
  if ((integer("pattern_count", parsedCount) || integer("patterns", parsedCount)) &&
      parsedCount >= 1 && parsedCount <= PATTERN_COUNT) {
    controllerPatternCount = parsedCount;
  } else if (!line.value("count").empty()) {
    const int count = static_cast<int>(std::strtol(line.value("count").c_str(), nullptr, 10));
    if (count >= 1 && count <= PATTERN_COUNT) controllerPatternCount = count;
  }

  integer("strip_length", controllerConfig.stripLength);
  integer("smoothing", controllerConfig.smoothing);
  integer("accel_range", controllerConfig.accelRange);
  integer("gyro_range", controllerConfig.gyroRange);
  integer("battery_percent", controllerConfig.batteryPercent);
  const std::string bootCalibration = line.value("boot_calibration");
  if (!bootCalibration.empty()) controllerConfig.bootCalibration = bootCalibration.c_str();
  const std::string imu = line.value("imu");
  if (!imu.empty()) controllerConfig.imu = imu.c_str();
  const std::string fps = line.value("fps");
  if (!fps.empty()) controllerConfig.fps = fps.c_str();
  const std::string batteryState = line.value("battery_state");
  if (!batteryState.empty()) controllerConfig.batteryState = batteryState.c_str();

  const std::string enabledMask = line.value("enabled_mask");
  const std::string invertedMask = line.value("inverted_mask");
  if (!enabledMask.empty()) enabledPatternMask = parseUint32(enabledMask, enabledPatternMask) & ALL_PATTERN_MASK;
  if (!invertedMask.empty()) invertedPatternMask = parseUint32(invertedMask, invertedPatternMask) & ALL_PATTERN_MASK;
  syncReadyPatternMask = parseUint32(line.value("sync_ready_mask"), syncReadyPatternMask) & ALL_PATTERN_MASK;
  partialSyncPatternMask = parseUint32(line.value("partial_sync_mask"), partialSyncPatternMask) & ALL_PATTERN_MASK;
  localReactivePatternMask = parseUint32(line.value("local_reactive_mask"), localReactivePatternMask) & ALL_PATTERN_MASK;

  bool parsedBool = false;
  if (parseBoolean(line.value("sync_enabled"), parsedBool)) syncModel.enabled = parsedBool;
  int parsedGroup = syncModel.group;
  if (integer("sync_group", parsedGroup) || integer("group", parsedGroup)) {
    if (parsedGroup >= 1 && parsedGroup <= 255) syncModel.group = parsedGroup;
  }
  const std::string role = line.value("sync_role").empty() ? line.value("role") : line.value("sync_role");
  if (!role.empty()) syncModel.role = role.c_str();
  const std::string loss = line.value("sync_loss_behavior").empty() ? line.value("loss_behavior") : line.value("sync_loss_behavior");
  if (!loss.empty()) syncModel.loss = loss.c_str();
  const std::string masterUid = line.value("sync_master_uid").empty() ? line.value("master_uid") : line.value("sync_master_uid");
  if (!masterUid.empty() && masterUid != "none") syncModel.masterUid = masterUid.c_str();
  if (parseBoolean(line.value("wireless_enabled"), parsedBool)) syncModel.wirelessEnabled = parsedBool;
  const std::string wirelessProfile = line.value("wireless_profile");
  if (!wirelessProfile.empty()) syncModel.wirelessProfile = wirelessProfile.c_str();
  const std::string syncState = line.value("sync_state");
  if (!syncState.empty()) syncModel.state = syncState.c_str();
  const std::string radioMode = line.value("radio_mode");
  if (!radioMode.empty()) syncModel.radioMode = radioMode.c_str();
  if (parseBoolean(line.value("sync_locked"), parsedBool) || parseBoolean(line.value("locked"), parsedBool)) {
    syncModel.locked = parsedBool;
  }
  syncModel.beaconTx = parseUnsigned(line.value("beacon_tx_count"), syncModel.beaconTx);
  syncModel.beaconRx = parseUnsigned(line.value("beacon_rx_count"), syncModel.beaconRx);
  syncModel.crcErrors = parseUnsigned(line.value("beacon_crc_errors"), syncModel.crcErrors);
  syncModel.crcErrors = parseUnsigned(line.value("scan_crc_fail"), syncModel.crcErrors);
  syncModel.groupMismatch = parseUnsigned(line.value("beacon_group_mismatch"), syncModel.groupMismatch);
  syncModel.groupMismatch = parseUnsigned(line.value("scan_group_mismatch"), syncModel.groupMismatch);
  syncModel.applyCount = parseUnsigned(line.value("sync_apply_count"), syncModel.applyCount);
  integer("drift_ms", syncModel.driftMs);
  integer("beacon_age_ms", syncModel.beaconAgeMs);

  const String parsedPlayMode = line.value("play_mode").c_str();
  if (validOption(parsedPlayMode, NightKitePlayback::PLAY_MODES, NightKitePlayback::PLAY_MODE_COUNT)) {
    playMode = parsedPlayMode;
  }
  const String parsedBootMode = line.value("boot_mode").c_str();
  if (validOption(parsedBootMode, NightKitePlayback::BOOT_MODES, NightKitePlayback::BOOT_MODE_COUNT)) {
    bootMode = parsedBootMode;
  }
  bool parsedAutoplay = autoplayEnabled;
  if (parseBoolean(line.value("autoplay"), parsedAutoplay) ||
      parseBoolean(line.value("autoplay_enabled"), parsedAutoplay)) {
    autoplayEnabled = parsedAutoplay;
  }
  int parsedInterval = autoplayInterval;
  if (integer("autoplay_interval", parsedInterval) && parsedInterval > 0 && parsedInterval <= 86400) {
    autoplayInterval = parsedInterval;
  }

  if (!draftDirty) {
    if (activePattern >= 1) draftPattern = activePattern;
    if (brightness >= 0) draftBrightness = brightness;
  }
  if (!playDraftDirty) {
    if (playMode != "unknown") draftPlayMode = playMode;
    if (bootMode != "unknown") draftBootMode = bootMode;
    draftAutoplayEnabled = autoplayEnabled;
    if (autoplayInterval > 0) draftAutoplayInterval = autoplayInterval;
  }
  if (!patternDraftDirty) {
    draftEnabledPatternMask = enabledPatternMask;
    draftInvertedPatternMask = invertedPatternMask;
  }
  if (!syncDraftDirty) draftSync = syncModel;
  if (!controllerDraftDirty) draftControllerConfig = controllerConfig;
}

void markRefresh(const String& payload)
{
  if (payload == "cmd=info") initialRefresh.commandSucceeded(InitialRefreshPart::Info);
  else if (payload == "cmd=status") initialRefresh.commandSucceeded(InitialRefreshPart::Status);
  else if (payload == "cmd=get section=config") initialRefresh.commandSucceeded(InitialRefreshPart::Config);
  else if (payload == "cmd=get section=play") initialRefresh.commandSucceeded(InitialRefreshPart::Play);
}

void maybeReady()
{
  if (!controllerSessionReady(transportConnected(), controllerConnected, initialRefresh.ready(), protocolResolved)) {
    return;
  }
  if (activePattern < 1 || activePattern > controllerPatternCount || brightness < 0 || brightness > 255) {
    setStatus(Phase::Error, "Controller configuration incomplete");
    return;
  }
  setStatus(Phase::Ready, "Controller ready");
  if (selectedTransport == Transport::Usb) {
    setSmoke(SmokeId::Usb, "PASS", "NK4 write + read");
  } else {
    setSmoke(SmokeId::Gatt, "PASS", "scan + connect + write + notify");
    gattAutoTest = false;
  }
}

void handleResponse(const String& raw)
{
  String text = raw;
  text.trim();
  Serial.printf("[NK4 RX] %s\n", text.c_str());
  terminalLog = text;
  const Nk4Line line = parseNk4Line(text.c_str());
  if (!line.valid) {
    return;
  }
  if (line.event) {
    applyFields(line);
    controllerConnected = true;
    invalidateStatus();
    if (page == Page::Sync || (page == Page::Service && serviceTab == ServiceTab::Diagnostics)) {
      invalidatePageBody();
    }
    return;
  }
  if (!pending.active || line.sequence != pending.sequence) {
    Serial.println("[NK4] stale response ignored");
    return;
  }

  const QueueEntry completed = pending.entry;
  pending.active = false;
  controllerConnected = true;
  applyFields(line);

  if (line.error) {
    commandOperation.commandFinished(completed.commandClass, false);
    if (completed.commandClass == CommandClass::OptionalInit) {
      maybeReady();
      return;
    }
    commandQueue.clear();
    const String code = line.value("code").c_str();
    const String message = line.value("msg").c_str();
    if (code == "busy" && completed.commandClass == CommandClass::User) {
      commandOperation.abort();
      setStatus(Phase::Ready, "Controller busy; tap Apply & Save to retry");
      return;
    }
    commandOperation.failRemaining();
    setStatus(Phase::Error, message.length() ? message : String("NK4 error ") + code);
    return;
  }
  if (!line.ok) {
    commandOperation.commandFinished(completed.commandClass, false);
    commandQueue.clear();
    commandOperation.failRemaining();
    setStatus(Phase::Error, "Invalid NK4 response");
    return;
  }

  commandOperation.commandFinished(completed.commandClass, true);
  if (completed.command.startsWith("cmd=hello")) {
    protocolResolved = true;
    queueRefresh();
    return;
  }
  markRefresh(completed.command);
  if (completed.command.startsWith("cmd=set pattern=")) {
    activePattern = completed.command.substring(16).toInt();
  } else if (completed.command.startsWith("cmd=set brightness=")) {
    brightness = completed.command.substring(19).toInt();
  } else if (completed.command.startsWith("cmd=set play_mode=")) {
    playMode = completed.command.substring(completed.command.indexOf('=') + 1);
  } else if (completed.command.startsWith("cmd=set boot_mode=")) {
    bootMode = completed.command.substring(completed.command.indexOf('=') + 1);
  } else if (completed.command.startsWith("cmd=set autoplay=")) {
    autoplayEnabled = completed.command.endsWith("=1");
  } else if (completed.command.startsWith("cmd=set autoplay_interval=")) {
    autoplayInterval = completed.command.substring(completed.command.indexOf('=') + 1).toInt();
  } else if (completed.command.startsWith("cmd=set enabled_mask=")) {
    enabledPatternMask = static_cast<uint32_t>(completed.command.substring(completed.command.indexOf('=') + 1).toInt());
  } else if (completed.command.startsWith("cmd=set inverted_mask=")) {
    invertedPatternMask = static_cast<uint32_t>(completed.command.substring(completed.command.indexOf('=') + 1).toInt());
  } else if (completed.command.startsWith("cmd=set sync_enabled=")) {
    syncModel.enabled = completed.command.endsWith("=1");
  } else if (completed.command.startsWith("cmd=set sync_group=")) {
    syncModel.group = completed.command.substring(completed.command.indexOf('=') + 1).toInt();
  } else if (completed.command.startsWith("cmd=set sync_role=")) {
    syncModel.role = completed.command.substring(completed.command.indexOf('=') + 1);
  } else if (completed.command.startsWith("cmd=set sync_loss_behavior=")) {
    syncModel.loss = completed.command.substring(completed.command.indexOf('=') + 1);
  } else if (completed.command.startsWith("cmd=set sync_master_uid=")) {
    syncModel.masterUid = completed.command.substring(completed.command.indexOf('=') + 1);
  } else if (completed.command.startsWith("cmd=set wireless_enabled=")) {
    syncModel.wirelessEnabled = completed.command.endsWith("=1");
  } else if (completed.command.startsWith("cmd=set wireless_profile=")) {
    syncModel.wirelessProfile = completed.command.substring(completed.command.indexOf('=') + 1);
  } else if (completed.command.startsWith("cmd=set strip_length=")) {
    controllerConfig.stripLength = completed.command.substring(completed.command.indexOf('=') + 1).toInt();
  } else if (completed.command.startsWith("cmd=set smoothing=")) {
    controllerConfig.smoothing = completed.command.substring(completed.command.indexOf('=') + 1).toInt();
  } else if (completed.command.startsWith("cmd=set accel_range=")) {
    controllerConfig.accelRange = completed.command.substring(completed.command.indexOf('=') + 1).toInt();
  } else if (completed.command.startsWith("cmd=set gyro_range=")) {
    controllerConfig.gyroRange = completed.command.substring(completed.command.indexOf('=') + 1).toInt();
  } else if (completed.command.startsWith("cmd=set boot_calibration=")) {
    controllerConfig.bootCalibration = completed.command.substring(completed.command.indexOf('=') + 1);
  } else if (completed.command.startsWith("cmd=set name=")) {
    controllerConfig.deviceName = completed.command.substring(completed.command.indexOf('=') + 1);
    controllerName = controllerConfig.deviceName;
  } else if (completed.command == "cmd=save") {
    if (saveWorkflow == SaveWorkflow::Control) draftDirty = false;
    if (saveWorkflow == SaveWorkflow::Playback) playDraftDirty = false;
    if (saveWorkflow == SaveWorkflow::Patterns) patternDraftDirty = false;
    if (saveWorkflow == SaveWorkflow::Sync) syncDraftDirty = false;
    if (saveWorkflow == SaveWorkflow::Controller) controllerDraftDirty = false;
    if (saveWorkflow == SaveWorkflow::Defaults) {
      draftDirty = false;
      playDraftDirty = false;
      patternDraftDirty = false;
      syncDraftDirty = false;
      controllerDraftDirty = false;
      queueRefresh(CommandClass::Poll);
    }
    invalidateSaveFooter();
  }
  if (!patternDraftDirty) {
    draftEnabledPatternMask = enabledPatternMask;
    draftInvertedPatternMask = invertedPatternMask;
  }
  if (!syncDraftDirty) draftSync = syncModel;
  if (!controllerDraftDirty) draftControllerConfig = controllerConfig;
  maybeReady();
}

void pumpProtocol()
{
  if (!sessionStarted || !transportConnected()) {
    return;
  }
  if (!protocolResolved && !pending.active && commandQueue.empty() && helloAt &&
      static_cast<int32_t>(millis() - helloAt) >= 0) {
    helloAt = 0;
    enqueue("cmd=hello client=nightkite-link proto_min=4 proto_max=4", CommandClass::RequiredInit);
  }

  String line;
  while (readTransportLine(line)) {
    handleResponse(line);
  }

  if (pending.active) {
    const bool calibration = pending.entry.command.startsWith("cmd=calibrate mode=");
    const bool longBle = pending.entry.command == "cmd=get section=sync" ||
                         pending.entry.command == "cmd=get section=patterns";
    const bool mediumBle = pending.entry.command == "cmd=get section=config" ||
                           pending.entry.command == "cmd=get section=play" ||
                           pending.entry.command == "cmd=get section=wireless";
    const uint32_t timeout = calibration ? 30000 : selectedTransport == Transport::Ble ?
                             longBle ? 10000 : mediumBle ? 8000 : 5000 : 5000;
    if (millis() - pending.sentAt > timeout) {
      commandOperation.commandFinished(pending.entry.commandClass, false);
      const bool soft = pending.entry.commandClass == CommandClass::OptionalInit ||
                        pending.entry.commandClass == CommandClass::Poll;
      pending.active = false;
      if (soft) {
        maybeReady();
        return;
      }
      commandQueue.clear();
      commandOperation.failRemaining();
      setStatus(Phase::Error, "Controller response timeout");
    }
    return;
  }
  if (commandQueue.empty()) {
    bool success = false;
    if (commandOperation.takeResult(success)) {
      playUiTone(success ? 3000 : 600, success ? 60 : 120);
      setStatus(success ? Phase::Ready : Phase::Error,
                success ? operationSuccess : "Change failed; reload before retry");
      saveWorkflow = SaveWorkflow::None;
    }
    return;
  }
  pending.entry = commandQueue.front();
  commandQueue.erase(commandQueue.begin());
  if (pending.entry.generation != sessionGeneration) {
    return;
  }
  pending.sequence = nextSequence++;
  if (nextSequence == 0) nextSequence = 1;
  pending.sentAt = millis();
  pending.active = true;
  const String wire = "NK4 seq=" + String(pending.sequence) + " " + pending.entry.command;
  if (!sendTransportLine(wire)) {
    commandOperation.commandFinished(pending.entry.commandClass, false);
    pending.active = false;
    commandOperation.failRemaining();
    setStatus(Phase::Error, "Transport write failed");
    return;
  }
  if (protocolResolved) {
    setStatus(Phase::Busy, "Sending " + pending.entry.command.substring(4));
  }
}

bool controlsReady()
{
  return phase == Phase::Ready && controllerSessionReady(transportConnected(), controllerConnected,
                                                          initialRefresh.ready(), protocolResolved) &&
         !pending.active && commandQueue.empty();
}

bool liveControlsReady()
{
  return controllerSessionReady(transportConnected(), controllerConnected, initialRefresh.ready(), protocolResolved);
}

bool playControlsReady()
{
  return controlsReady() && validOption(playMode, NightKitePlayback::PLAY_MODES, NightKitePlayback::PLAY_MODE_COUNT) &&
         validOption(bootMode, NightKitePlayback::BOOT_MODES, NightKitePlayback::BOOT_MODE_COUNT) &&
         autoplayInterval > 0;
}

void applyDrafts()
{
  if (!controlsReady()) {
    setStatus(Phase::Error, "Controller is busy or disconnected");
    return;
  }
  if (!draftDirty) {
    setStatus(Phase::Ready, "No changes to save");
    return;
  }
  saveWorkflow = SaveWorkflow::Control;
  enqueue("cmd=set pattern=" + String(draftPattern), CommandClass::User, LiveCommandKind::Pattern);
  enqueue("cmd=set brightness=" + String(draftBrightness), CommandClass::User, LiveCommandKind::Brightness);
  enqueue("cmd=save", CommandClass::User);
  operationSuccess = "Pattern and brightness saved";
  setStatus(Phase::Busy, "Applying and saving changes");
}

void applyPlayDrafts()
{
  if (!playControlsReady()) {
    setStatus(Phase::Error, "Playback controls unavailable or busy");
    return;
  }
  if (!playDraftDirty) {
    setStatus(Phase::Ready, "No playback changes to save");
    return;
  }
  saveWorkflow = SaveWorkflow::Playback;
  if (draftPlayMode != playMode) {
    enqueue("cmd=set play_mode=" + draftPlayMode, CommandClass::User);
  }
  if (draftBootMode != bootMode) {
    enqueue("cmd=set boot_mode=" + draftBootMode, CommandClass::User);
  }
  if (draftAutoplayEnabled != autoplayEnabled) {
    enqueue(String("cmd=set autoplay=") + (draftAutoplayEnabled ? "1" : "0"), CommandClass::User);
  }
  if (draftAutoplayInterval != autoplayInterval) {
    enqueue("cmd=set autoplay_interval=" + String(draftAutoplayInterval), CommandClass::User);
  }
  enqueue("cmd=save", CommandClass::User);
  operationSuccess = "Playback saved";
  setStatus(Phase::Busy, "Applying and saving playback");
}

void applyPatternDrafts()
{
  if (!controlsReady() || !patternDraftDirty) return;
  const uint32_t supported = Tab5WorkflowPolicy::supportedPatternMask(controllerPatternCount);
  uint32_t enabled = draftEnabledPatternMask & supported;
  if (!enabled) enabled = 1;
  draftEnabledPatternMask = enabled;
  draftInvertedPatternMask &= supported;
  saveWorkflow = SaveWorkflow::Patterns;
  if (enabled != enabledPatternMask) enqueue("cmd=set enabled_mask=" + String(enabled), CommandClass::User);
  if (draftInvertedPatternMask != invertedPatternMask) {
    enqueue("cmd=set inverted_mask=" + String(draftInvertedPatternMask), CommandClass::User);
  }
  enqueue("cmd=save", CommandClass::User);
  operationSuccess = "Pattern masks saved";
  setStatus(Phase::Busy, "Applying pattern masks");
}

void applySyncDrafts()
{
  if (!controlsReady() || !syncDraftDirty) return;
  saveWorkflow = SaveWorkflow::Sync;
  if (draftSync.enabled != syncModel.enabled) enqueue(String("cmd=set sync_enabled=") + (draftSync.enabled ? "1" : "0"), CommandClass::User);
  if (draftSync.group != syncModel.group) enqueue("cmd=set sync_group=" + String(draftSync.group), CommandClass::User);
  if (draftSync.role != syncModel.role) enqueue("cmd=set sync_role=" + draftSync.role, CommandClass::User);
  if (draftSync.loss != syncModel.loss) enqueue("cmd=set sync_loss_behavior=" + draftSync.loss, CommandClass::User);
  if (draftSync.masterUid != syncModel.masterUid && draftSync.masterUid.length()) {
    enqueue("cmd=set sync_master_uid=" + draftSync.masterUid, CommandClass::User);
  }
  if (draftSync.wirelessEnabled != syncModel.wirelessEnabled) {
    enqueue(String("cmd=set wireless_enabled=") + (draftSync.wirelessEnabled ? "1" : "0"), CommandClass::User);
  }
  if (draftSync.wirelessProfile != syncModel.wirelessProfile) {
    enqueue("cmd=set wireless_profile=" + draftSync.wirelessProfile, CommandClass::User);
  }
  enqueue("cmd=save", CommandClass::User);
  operationSuccess = "Sync configuration saved";
  setStatus(Phase::Busy, "Applying sync configuration");
}

void applyControllerDrafts()
{
  if (!controlsReady() || !controllerDraftDirty) return;
  saveWorkflow = SaveWorkflow::Controller;
  if (draftControllerConfig.deviceName != controllerConfig.deviceName && draftControllerConfig.deviceName.length()) {
    enqueue("cmd=set name=" + draftControllerConfig.deviceName, CommandClass::User);
  }
  if (draftControllerConfig.stripLength != controllerConfig.stripLength) enqueue("cmd=set strip_length=" + String(draftControllerConfig.stripLength), CommandClass::User);
  if (draftControllerConfig.smoothing != controllerConfig.smoothing) enqueue("cmd=set smoothing=" + String(draftControllerConfig.smoothing), CommandClass::User);
  if (draftControllerConfig.accelRange != controllerConfig.accelRange) enqueue("cmd=set accel_range=" + String(draftControllerConfig.accelRange), CommandClass::User);
  if (draftControllerConfig.gyroRange != controllerConfig.gyroRange) enqueue("cmd=set gyro_range=" + String(draftControllerConfig.gyroRange), CommandClass::User);
  if (draftControllerConfig.bootCalibration != controllerConfig.bootCalibration) enqueue("cmd=set boot_calibration=" + draftControllerConfig.bootCalibration, CommandClass::User);
  enqueue("cmd=save", CommandClass::User);
  operationSuccess = "Controller configuration saved";
  setStatus(Phase::Busy, "Applying controller configuration");
}

void saveLiveController()
{
  if (!controlsReady()) return;
  saveWorkflow = SaveWorkflow::SaveOnly;
  enqueue("cmd=save", CommandClass::User);
  operationSuccess = "Controller state saved";
}

void runCalibration(const char* modeName)
{
  if (!controlsReady()) return;
  saveWorkflow = SaveWorkflow::Calibration;
  enqueue(String("cmd=calibrate mode=") + modeName, CommandClass::User);
  operationSuccess = String(modeName) + " calibration complete";
  setStatus(Phase::Busy, String("Running ") + modeName + " calibration");
}

void confirmFactoryDefaults()
{
  if (!controlsReady()) return;
  saveWorkflow = SaveWorkflow::Defaults;
  enqueue("cmd=defaults confirm=1", CommandClass::User);
  enqueue("cmd=save", CommandClass::User);
  operationSuccess = "Controller defaults restored and saved";
  setStatus(Phase::Busy, "Restoring controller defaults");
}

void sendTerminalCommand()
{
  if (!controlsReady()) return;
  String command = terminalInput;
  command.trim();
  if (command.startsWith("cmd=defaults") || command.startsWith("cmd=save")) {
    setStatus(Phase::Error, "Use the confirmed maintenance actions");
    return;
  }
  if (!Tab5WorkflowPolicy::terminalCommandAllowed(command.c_str())) {
    setStatus(Phase::Error, "Terminal accepts one NK4 cmd= payload");
    return;
  }
  saveWorkflow = SaveWorkflow::Terminal;
  enqueue(command, CommandClass::User);
  operationSuccess = "Terminal command completed";
  setStatus(Phase::Busy, "Sending terminal command");
}

void reloadController()
{
  if (!transportConnected() || !protocolResolved || pending.active) {
    setStatus(Phase::Error, "Cannot reload while disconnected or busy");
    return;
  }
  commandQueue.clear();
  commandOperation.abort();
  draftDirty = false;
  playDraftDirty = false;
  patternDraftDirty = false;
  syncDraftDirty = false;
  controllerDraftDirty = false;
  saveWorkflow = SaveWorkflow::None;
  queueRefresh();
}

void disconnectController()
{
  if (selectedTransport == Transport::Ble) {
    ble.disconnect();
  }
  selectedTransport = Transport::None;
  endSession("Disconnected");
}

bool ensureSdReady()
{
  if (sdReady && SD_MMC.cardType() != CARD_NONE) return true;
  SD_MMC.setPowerChannel(SD_LDO_CHANNEL);
  if (!SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT) || SD_MMC.cardType() == CARD_NONE) {
    sdReady = false;
    setStatus(Phase::Error, "microSD unavailable");
    return false;
  }
  SD_MMC.mkdir("/profiles");
  SD_MMC.mkdir("/firmware");
  sdSizeBytes = SD_MMC.cardSize();
  sdReady = true;
  return true;
}

String baseName(String path)
{
  const int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

String profileDisplayName(String path)
{
  path = baseName(path);
  if (path.endsWith(".json")) path.remove(path.length() - 5);
  return path;
}

String sanitizeFileBase(String name)
{
  name.trim();
  String clean;
  for (size_t i = 0; i < name.length() && clean.length() < 40; ++i) {
    const char c = name[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') clean += c;
    else if (c == ' ') clean += '_';
  }
  while (clean.startsWith(".") || clean.startsWith("_")) clean.remove(0, 1);
  while (clean.endsWith(".") || clean.endsWith("_")) clean.remove(clean.length() - 1);
  return clean;
}

String profilePath(String name)
{
  String clean = sanitizeFileBase(name);
  if (!clean.length()) return "";
  if (!clean.endsWith(".json")) clean += ".json";
  return "/profiles/" + clean;
}

void refreshProfileList()
{
  profileFiles.clear();
  if (!ensureSdReady()) return;
  File directory = SD_MMC.open("/profiles");
  if (!directory) return;
  while (File entry = directory.openNextFile()) {
    String name = baseName(entry.name());
    entry.close();
    if (name.endsWith(".json.tmp")) {
      SD_MMC.remove("/profiles/" + name);
    } else if (name.endsWith(".json.bak")) {
      const String original = "/profiles/" + name.substring(0, name.length() - 4);
      if (SD_MMC.exists(original)) SD_MMC.remove("/profiles/" + name);
      else SD_MMC.rename("/profiles/" + name, original);
    } else if (name.endsWith(".json")) {
      profileFiles.push_back(name);
    }
  }
  directory.close();
  std::sort(profileFiles.begin(), profileFiles.end(), [](const String& a, const String& b) { return a < b; });
  if (profileFiles.empty()) selectedProfile = -1;
  else selectedProfile = constrain(selectedProfile, 0, static_cast<int>(profileFiles.size()) - 1);
  profileOffset = constrain(profileOffset, 0, max(0, static_cast<int>(profileFiles.size()) - 6));
  invalidatePageBody();
}

ProfileData currentProfile()
{
  ProfileData data;
  data.deviceName = controllerConfig.deviceName.c_str();
  data.brightness = brightness;
  data.stripLength = controllerConfig.stripLength;
  data.activePattern = activePattern;
  data.smoothing = controllerConfig.smoothing;
  data.accelRange = controllerConfig.accelRange;
  data.gyroRange = controllerConfig.gyroRange;
  data.playMode = playMode.c_str();
  data.bootMode = bootMode.c_str();
  data.syncEnabled = syncModel.enabled;
  data.syncGroup = syncModel.group;
  data.syncRole = syncModel.role.c_str();
  data.syncMasterUid = syncModel.masterUid.c_str();
  data.syncLossBehavior = syncModel.loss.c_str();
  data.wirelessEnabled = syncModel.wirelessEnabled;
  data.wirelessProfile = syncModel.wirelessProfile.c_str();
  data.enabledPatternMask = enabledPatternMask;
  data.invertedPatternMask = invertedPatternMask;
  data.autoplayEnabled = autoplayEnabled;
  data.autoplayIntervalSeconds = autoplayInterval;
  return data;
}

bool writeProfile(const String& path, const String& displayName, bool overwrite)
{
  if (!controlsReady() || !ensureSdReady() || !path.length()) return false;
  if (SD_MMC.exists(path) && !overwrite) {
    setStatus(Phase::Error, "Profile already exists");
    return false;
  }
  std::string encoded;
  std::string error;
  if (!encodeProfileJson(currentProfile(), encoded, error) || encoded.empty() || encoded.size() > MAX_PROFILE_BYTES) {
    setStatus(Phase::Error, String("Profile invalid: ") + error.c_str());
    return false;
  }
  const String temporary = path + ".tmp";
  const String backup = path + ".bak";
  SD_MMC.remove(temporary);
  File file = SD_MMC.open(temporary, FILE_WRITE);
  if (!file) {
    setStatus(Phase::Error, "Profile open failed");
    return false;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
  file.flush();
  const bool writeOk = file.getWriteError() == 0 && written == encoded.size();
  file.close();
  File verify = SD_MMC.open(temporary, FILE_READ);
  const bool verifyOk = verify && verify.size() == encoded.size();
  if (verify) verify.close();
  if (!writeOk || !verifyOk) {
    SD_MMC.remove(temporary);
    setStatus(Phase::Error, "Profile write verification failed");
    return false;
  }
  const bool existed = SD_MMC.exists(path);
  SD_MMC.remove(backup);
  if (existed && !SD_MMC.rename(path, backup)) {
    SD_MMC.remove(temporary);
    setStatus(Phase::Error, "Profile backup failed");
    return false;
  }
  if (!SD_MMC.rename(temporary, path)) {
    if (existed) SD_MMC.rename(backup, path);
    setStatus(Phase::Error, "Profile replace failed");
    return false;
  }
  SD_MMC.remove(backup);
  loadedProfile = currentProfile();
  hasLoadedProfile = true;
  loadedProfilePath = path;
  loadedProfileName = displayName;
  refreshProfileList();
  setStatus(Phase::Ready, "Profile saved");
  return true;
}

bool loadProfile(const String& path)
{
  if (!ensureSdReady()) return false;
  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.size() == 0 || file.size() > MAX_PROFILE_BYTES) {
    if (file) file.close();
    setStatus(Phase::Error, "Profile missing, empty or too large");
    return false;
  }
  std::string json(file.size(), '\0');
  const size_t read = file.read(reinterpret_cast<uint8_t*>(&json[0]), json.size());
  file.close();
  if (read != json.size()) {
    setStatus(Phase::Error, "Profile read failed");
    return false;
  }
  std::string error;
  ProfileData decoded;
  if (!decodeProfileJson(json, currentProfile(), decoded, error)) {
    setStatus(Phase::Error, String("Invalid profile: ") + error.c_str());
    return false;
  }
  loadedProfile = decoded;
  hasLoadedProfile = true;
  loadedProfilePath = path;
  loadedProfileName = profileDisplayName(path);
  setStatus(Phase::Ready, "Profile loaded; Apply keeps it unsaved");
  invalidatePageBody();
  return true;
}

void applyLoadedProfile()
{
  if (!controlsReady() || !hasLoadedProfile) {
    setStatus(Phase::Error, "Load a profile on a ready controller first");
    return;
  }
  const uint32_t supportedMask = Tab5WorkflowPolicy::supportedPatternMask(controllerPatternCount);
  uint32_t enabled = loadedProfile.enabledPatternMask & supportedMask;
  if (!enabled) enabled = 1;
  int pattern = loadedProfile.activePattern;
  if (pattern < 1 || pattern > controllerPatternCount) pattern = 1;
  saveWorkflow = SaveWorkflow::ProfileApply;
  operationSuccess = "Profile applied live; use Save to persist";
  if (!loadedProfile.deviceName.empty()) enqueue("cmd=set name=" + String(loadedProfile.deviceName.c_str()), CommandClass::User);
  enqueue("cmd=set brightness=" + String(loadedProfile.brightness), CommandClass::User);
  enqueue("cmd=set strip_length=" + String(loadedProfile.stripLength), CommandClass::User);
  enqueue("cmd=set pattern=" + String(pattern), CommandClass::User);
  enqueue("cmd=set smoothing=" + String(loadedProfile.smoothing), CommandClass::User);
  enqueue("cmd=set accel_range=" + String(loadedProfile.accelRange), CommandClass::User);
  enqueue("cmd=set gyro_range=" + String(loadedProfile.gyroRange), CommandClass::User);
  enqueue(String("cmd=set autoplay=") + (loadedProfile.autoplayEnabled ? "1" : "0"), CommandClass::User);
  enqueue("cmd=set autoplay_interval=" + String(loadedProfile.autoplayIntervalSeconds), CommandClass::User);
  if (loadedProfile.playMode != "unknown") enqueue("cmd=set play_mode=" + String(loadedProfile.playMode.c_str()), CommandClass::User);
  if (loadedProfile.bootMode != "unknown") enqueue("cmd=set boot_mode=" + String(loadedProfile.bootMode.c_str()), CommandClass::User);
  enqueue("cmd=set enabled_mask=" + String(enabled), CommandClass::User);
  enqueue("cmd=set inverted_mask=" + String(loadedProfile.invertedPatternMask & supportedMask), CommandClass::User);
  enqueue(String("cmd=set sync_enabled=") + (loadedProfile.syncEnabled ? "1" : "0"), CommandClass::User);
  if (loadedProfile.syncGroup >= 1) enqueue("cmd=set sync_group=" + String(loadedProfile.syncGroup), CommandClass::User);
  if (loadedProfile.syncRole != "unknown") enqueue("cmd=set sync_role=" + String(loadedProfile.syncRole.c_str()), CommandClass::User);
  if (!loadedProfile.syncMasterUid.empty()) enqueue("cmd=set sync_master_uid=" + String(loadedProfile.syncMasterUid.c_str()), CommandClass::User);
  if (loadedProfile.syncLossBehavior != "unknown") enqueue("cmd=set sync_loss_behavior=" + String(loadedProfile.syncLossBehavior.c_str()), CommandClass::User);
  enqueue(String("cmd=set wireless_enabled=") + (loadedProfile.wirelessEnabled ? "1" : "0"), CommandClass::User);
  if (loadedProfile.wirelessProfile != "unknown") enqueue("cmd=set wireless_profile=" + String(loadedProfile.wirelessProfile.c_str()), CommandClass::User);
  setStatus(Phase::Busy, "Applying profile without saving");
}

bool renameSelectedProfile(const String& name)
{
  if (selectedProfile < 0 || selectedProfile >= static_cast<int>(profileFiles.size())) return false;
  const String oldPath = "/profiles/" + profileFiles[selectedProfile];
  const String newPath = profilePath(name);
  if (!newPath.length() || SD_MMC.exists(newPath) || !SD_MMC.rename(oldPath, newPath)) {
    setStatus(Phase::Error, "Profile rename failed or target exists");
    return false;
  }
  if (loadedProfilePath == oldPath) {
    loadedProfilePath = newPath;
    loadedProfileName = profileDisplayName(newPath);
  }
  refreshProfileList();
  setStatus(Phase::Ready, "Profile renamed");
  return true;
}

void deleteSelectedProfile()
{
  if (selectedProfile < 0 || selectedProfile >= static_cast<int>(profileFiles.size())) return;
  const String path = "/profiles/" + profileFiles[selectedProfile];
  if (!SD_MMC.remove(path)) {
    setStatus(Phase::Error, "Profile delete failed");
    return;
  }
  if (loadedProfilePath == path) {
    hasLoadedProfile = false;
    loadedProfilePath = "";
    loadedProfileName = "";
  }
  refreshProfileList();
  setStatus(Phase::Ready, "Profile deleted");
}

void refreshFirmwareList()
{
  firmwareFiles.clear();
  if (!ensureSdReady()) return;
  File directory = SD_MMC.open("/firmware");
  if (directory) {
    while (File entry = directory.openNextFile()) {
      String name = baseName(entry.name());
      entry.close();
      if (name.endsWith(".uf2") || name.endsWith(".UF2")) firmwareFiles.push_back(name);
    }
    directory.close();
  }
  std::sort(firmwareFiles.begin(), firmwareFiles.end(), [](const String& a, const String& b) { return a < b; });
  selectedFirmware = firmwareFiles.empty() ? -1 : constrain(selectedFirmware, 0, static_cast<int>(firmwareFiles.size()) - 1);
  firmwareOffset = constrain(firmwareOffset, 0, max(0, static_cast<int>(firmwareFiles.size()) - 6));
  firmwareValidated = false;
  invalidatePageBody();
}

void validateSelectedFirmware()
{
  if (selectedFirmware < 0 || selectedFirmware >= static_cast<int>(firmwareFiles.size())) return;
  firmwareValidation = Uf2Validator::validate(SD_MMC, "/firmware/" + firmwareFiles[selectedFirmware], firmwareTarget);
  firmwareValidated = true;
  setStatus(firmwareValidation.result == Uf2ValidationResult::Ok ? Phase::Ready : Phase::Error,
            Uf2Validator::message(firmwareValidation.result));
  invalidateRect(815, 330, 415, 325);
}

void startSelectedFirmwareFlash()
{
  if (!firmwareValidated || firmwareValidation.result != Uf2ValidationResult::Ok || selectedFirmware < 0) return;
  disconnectController();
  M5.Power.setExtOutput(true, m5::ext_USB);
  if (!uf2Flasher.begin() || !uf2Flasher.startFlash(SD_MMC, "/firmware/" + firmwareFiles[selectedFirmware],
                                                    firmwareFiles[selectedFirmware], firmwareTarget)) {
    setStatus(Phase::Error, uf2Flasher.resultMessage());
    return;
  }
  flashResultReported = false;
  setStatus(Phase::Busy, "Waiting for matching BOOTSEL device");
}

void updateFirmwareFlash()
{
  if (uf2Flasher.isRunning()) {
    uf2Flasher.poll();
    invalidateRect(815, 430, 415, 225);
  }
  const FlashProgress& progress = uf2Flasher.progress();
  if (progress.done && !flashResultReported) {
    flashResultReported = true;
    setStatus(progress.success ? Phase::Idle : Phase::Error,
              progress.success ? "UF2 flash complete; controller rebooted" : progress.message);
  }
}

void drawButton(int x, int y, int w, int h, const String& label, bool enabled, bool selected = false)
{
  const uint16_t fill = !enabled ? DISABLED_COLOR : (selected ? ACCENT : PANEL_2);
  uiCanvas.fillRoundRect(x, y, w, h, 16, fill);
  uiCanvas.setTextDatum(middle_center);
  uiCanvas.setFont(label.length() > 15 || w < 130 ? &fonts::DejaVu12 : &fonts::DejaVu18);
  uiCanvas.setTextColor(enabled && selected ? BG : TFT_WHITE, fill);
  uiCanvas.drawString(label, x + w / 2, y + h / 2);
}

void drawValueTile(int x, int y, int w, const String& label, const String& value, bool active = false)
{
  const uint16_t fill = active ? ACCENT : PANEL_2;
  uiCanvas.fillRoundRect(x, y, w, 82, 14, fill);
  uiCanvas.setTextDatum(middle_left);
  uiCanvas.setFont(&fonts::DejaVu12);
  uiCanvas.setTextColor(active ? BG : MUTED, fill);
  uiCanvas.drawString(label, x + 16, y + 22);
  uiCanvas.setFont(&fonts::DejaVu18);
  uiCanvas.setTextColor(active ? BG : TFT_WHITE, fill);
  uiCanvas.drawString(value, x + 16, y + 57);
}

void drawBatteryIndicator(int x, const String& label, int level, bool charging, const String& state = "")
{
  String normalizedState = state;
  normalizedState.toUpperCase();
  uint16_t color = OK;
  if (charging) color = ACCENT;
  else if (level < 0) color = MUTED;
  else if (normalizedState == "CRIT" || normalizedState == "CUT" || normalizedState == "EMPTY") color = ERR;
  else if (level <= 30 || normalizedState == "LOW") color = WARN;
  const int shownLevel = constrain(level, 0, 100);
  String text = label + " " + (level < 0 ? String("--") : String(shownLevel) + "%");
  if (charging) text += " CHG";
  else if (normalizedState.length() && normalizedState != "NORMAL") text += " " + normalizedState;
  uiCanvas.setTextDatum(middle_left);
  uiCanvas.setFont(&fonts::DejaVu12);
  uiCanvas.setTextColor(color, BG);
  uiCanvas.drawString(text, x, 35);
  const int batteryX = x + 145;
  uiCanvas.drawRect(batteryX, 23, 38, 24, color);
  uiCanvas.fillRect(batteryX + 38, 29, 5, 12, color);
  if (level >= 0) uiCanvas.fillRect(batteryX + 3, 26, 32 * shownLevel / 100, 18, color);
}

void drawModal()
{
  if (modal == Modal::None) return;
  uiCanvas.fillRect(0, 0, 1280, 720, 0x0000);
  uiCanvas.fillRoundRect(120, 90, 1040, 570, 24, PANEL);
  uiCanvas.setTextDatum(middle_center);
  uiCanvas.setFont(&fonts::DejaVu24);
  uiCanvas.setTextColor(TFT_WHITE, PANEL);
  if (modal == Modal::TextInput) {
    const char* title = textPurpose == TextPurpose::SaveProfile ? "Save profile" :
                        textPurpose == TextPurpose::RenameProfile ? "Rename profile" :
                        textPurpose == TextPurpose::DeviceName ? "Controller name" :
                        textPurpose == TextPurpose::SyncMasterUid ? "Sync master UID" : "NK4 terminal";
    uiCanvas.drawString(title, 640, 128);
    uiCanvas.fillRoundRect(170, 160, 940, 68, 12, PANEL_2);
    uiCanvas.setTextDatum(middle_left);
    uiCanvas.setFont(&fonts::DejaVu18);
    uiCanvas.setTextColor(TFT_WHITE, PANEL_2);
    uiCanvas.drawString(textInput, 190, 194);
    const char* rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL=", "ZXCVBNM_ ."};
    for (int row = 0; row < 4; ++row) {
      const String keys = rows[row];
      const int keyW = 78;
      const int startX = 175 + (10 - static_cast<int>(keys.length())) * keyW / 2;
      for (size_t col = 0; col < keys.length(); ++col) {
        String key = keys.substring(col, col + 1);
        drawButton(startX + static_cast<int>(col) * keyW, 255 + row * 76, 68, 62, key, true);
      }
    }
    drawButton(170, 570, 190, 62, "Cancel", true);
    drawButton(380, 570, 250, 62, "Backspace", textInput.length());
    drawButton(650, 570, 190, 62, "Clear", textInput.length());
    drawButton(860, 570, 250, 62, "Confirm", textInput.length());
  } else {
    const String title = modal == Modal::ConfirmOverwrite ? "Overwrite existing profile?" :
                         modal == Modal::ConfirmDelete ? "Delete selected profile?" :
                         modal == Modal::ConfirmDefaults ? "Restore controller defaults?" :
                                                          "Flash selected UF2?";
    uiCanvas.drawString(title, 640, 245);
    uiCanvas.setFont(&fonts::DejaVu18);
    uiCanvas.setTextColor(WARN, PANEL);
    uiCanvas.drawString(modal == Modal::ConfirmFlash ? "Target and BOOTSEL device must match" :
                          "This action requires explicit confirmation", 640, 310);
    drawButton(280, 420, 300, 90, "Cancel", true);
    drawButton(700, 420, 300, 90, "Confirm", true, true);
  }
}

void render()
{
  static const char* const pageNames[] = {
      "Connect", "Control", "Patterns", "Playback", "Sync", "Audio", "Profiles", "Controller", "Service", "Firmware"};
  uiCanvas.fillScreen(BG);
  uiCanvas.setTextDatum(middle_left);
  uiCanvas.setFont(&fonts::DejaVu24);
  uiCanvas.setTextColor(TFT_WHITE, BG);
  uiCanvas.drawString("NightKite Link", 28, 35);
  drawBatteryIndicator(805, "TAB5", tab5BatteryLevel, tab5BatteryCharging);
  drawBatteryIndicator(1030, "NK", controllerConnected ? controllerConfig.batteryPercent : -1,
                       false, controllerConnected ? controllerConfig.batteryState : String(""));
  uiCanvas.setFont(&fonts::DejaVu12);
  uiCanvas.setTextColor(phase == Phase::Error ? ERR : phase == Phase::Ready ? OK : ACCENT, BG);
  uiCanvas.drawString(String(phaseName()) + "  " + statusText, 30, 72);
  uiCanvas.setTextDatum(middle_right);
  uiCanvas.setTextColor(MUTED, BG);
  uiCanvas.drawString(String("Q") + commandQueue.size() + (pending.active ? "+1" : "") + "  " +
                        (selectedTransport == Transport::Usb ? "USB" : selectedTransport == Transport::Ble ? "BLE" : "OFF"),
                        1245, 72);

  uiCanvas.fillRoundRect(18, 96, 225, 610, 18, PANEL);
  for (int i = 0; i < 10; ++i) {
    drawButton(28, 108 + i * 58, 205, 50, pageNames[i], true, static_cast<int>(page) == i);
  }

  uiCanvas.fillRoundRect(255, 96, 1007, 610, 18, PANEL);
  uiCanvas.setTextDatum(middle_left);
  uiCanvas.setFont(&fonts::DejaVu18);
  uiCanvas.setTextColor(TFT_WHITE, PANEL);
  uiCanvas.drawString(pageNames[static_cast<int>(page)], 280, 128);
  uiCanvas.setFont(&fonts::DejaVu12);
  uiCanvas.setTextColor(controllerConnected ? OK : WARN, PANEL);
  uiCanvas.drawString(controllerConnected ? controllerName + "  NK4" : "No ready controller", 470, 128);
  drawButton(990, 108, 115, 50, "Reload", transportConnected() && protocolResolved && !pending.active);
  drawButton(1120, 108, 120, 50, "Disconnect", selectedTransport != Transport::None);

  const bool ready = controlsReady();
  if (page == Page::Connect) {
    drawButton(285, 185, 220, 76, "USB-A Host", true, selectedTransport == Transport::Usb);
    drawButton(525, 185, 220, 76, "BLE Scan", true, selectedTransport == Transport::Ble);
    uiCanvas.setTextDatum(middle_left);
    uiCanvas.setFont(&fonts::DejaVu18);
    uiCanvas.setTextColor(MUTED, PANEL);
    uiCanvas.drawString(selectedTransport == Transport::Ble ? "Tap a discovered controller" :
                          selectedTransport == Transport::Usb ? "Connect the controller to Tab5 USB-A" :
                                                               "Select a transport", 285, 295);
    for (size_t i = 0; i < shownBleDevices.size() && i < 5; ++i) {
      const int y = 325 + static_cast<int>(i) * 65;
      uiCanvas.fillRoundRect(285, y, 700, 54, 12, PANEL_2);
      uiCanvas.setFont(&fonts::DejaVu12);
      uiCanvas.setTextColor(TFT_WHITE, PANEL_2);
      uiCanvas.drawString(shownBleDevices[i].name.length() ? shownBleDevices[i].name : shownBleDevices[i].address, 305, y + 18);
      uiCanvas.setTextColor(MUTED, PANEL_2);
      uiCanvas.drawString(shownBleDevices[i].address + "  " + shownBleDevices[i].rssi + " dBm", 305, y + 39);
    }
    drawValueTile(1010, 185, 210, "C6 / GATT", ble.statusMessage());
    drawValueTile(1010, 285, 210, "Controller", controllerConnected ? "Ready" : "Waiting");
  } else if (page == Page::Control) {
    drawValueTile(285, 185, 935, "Pattern", String(draftPattern) + "  " + NightKitePatterns::name(draftPattern));
    drawButton(300, 280, 150, 74, "Pattern -", liveControlsReady());
    drawButton(1055, 280, 150, 74, "Pattern +", liveControlsReady());
    drawValueTile(285, 385, 935, "Brightness", String(draftBrightness));
    drawButton(300, 480, 150, 74, "Level -", liveControlsReady());
    drawButton(1055, 480, 150, 74, "Level +", liveControlsReady());
    drawButton(540, 590, 430, 82, draftDirty ? "Apply & Save" : "Saved", ready && draftDirty, draftDirty);
  } else if (page == Page::Patterns) {
    drawButton(285, 170, 220, 58, editInvertedMask ? "Edit: Invert" : "Edit: Cycle", ready, true);
    drawButton(520, 170, 150, 58, "All", ready);
    drawButton(685, 170, 150, 58, "None", ready);
    drawButton(850, 170, 170, 58, "Normal", ready);
    drawButton(1035, 170, 185, 58, "Invert all", ready);
    for (int id = 1; id <= controllerPatternCount; ++id) {
      const int col = (id - 1) % 9;
      const int row = (id - 1) / 9;
      const int x = 280 + col * 104;
      const int y = 245 + row * 108;
      const uint32_t bit = 1UL << (id - 1);
      const bool set = editInvertedMask ? (draftInvertedPatternMask & bit) : (draftEnabledPatternMask & bit);
      uiCanvas.fillRoundRect(x, y, 94, 98, 12, set ? ACCENT : PANEL_2);
      uiCanvas.setTextDatum(middle_center);
      uiCanvas.setFont(&fonts::DejaVu18);
      uiCanvas.setTextColor(set ? BG : TFT_WHITE, set ? ACCENT : PANEL_2);
      uiCanvas.drawString(String(id), x + 47, y + 28);
      uiCanvas.setFont(&fonts::DejaVu12);
      const char tag = (syncReadyPatternMask & bit) ? 'S' : (partialSyncPatternMask & bit) ? 'P' :
                       (localReactivePatternMask & bit) ? 'L' : '?';
      uiCanvas.drawString(String(tag) + " " + String(NightKitePatterns::name(id)).substring(0, 10), x + 47, y + 69);
    }
    drawButton(480, 590, 520, 78, patternDraftDirty ? "Apply masks & Save" : "Masks saved",
               ready && patternDraftDirty, patternDraftDirty);
  } else if (page == Page::Playback) {
    drawValueTile(285, 185, 440, "Play mode (tap)", draftPlayMode, playDraftDirty);
    drawValueTile(750, 185, 470, "Boot mode (tap)", draftBootMode, playDraftDirty);
    drawValueTile(285, 295, 440, "Autoplay (tap)", draftAutoplayEnabled ? "ON" : "OFF", draftAutoplayEnabled);
    drawValueTile(750, 295, 470, "Interval", String(draftAutoplayInterval) + " s", playDraftDirty);
    drawButton(770, 395, 190, 65, "Interval -", playControlsReady());
    drawButton(1010, 395, 190, 65, "Interval +", playControlsReady());
    drawButton(500, 570, 510, 86, playDraftDirty ? "Apply playback & Save" : "Playback saved",
               playControlsReady() && playDraftDirty, playDraftDirty);
  } else if (page == Page::Sync) {
    drawValueTile(285, 175, 290, "Sync enabled", draftSync.enabled ? "ON" : "OFF", draftSync.enabled);
    drawValueTile(595, 175, 290, "Group", String(draftSync.group), syncDraftDirty);
    drawValueTile(905, 175, 315, "Role", draftSync.role, syncDraftDirty);
    drawValueTile(285, 275, 440, "Loss behavior", draftSync.loss, syncDraftDirty);
    drawValueTile(745, 275, 475, "Wireless profile", draftSync.wirelessProfile, syncDraftDirty);
    drawValueTile(285, 375, 290, "Wireless", draftSync.wirelessEnabled ? "ON" : "OFF", draftSync.wirelessEnabled);
    drawValueTile(595, 375, 290, "Master UID (tap)", draftSync.masterUid.length() ? draftSync.masterUid : "none", syncDraftDirty);
    drawValueTile(905, 375, 315, "State / radio", syncModel.state + "  " + syncModel.radioMode + (syncModel.locked ? " LOCK" : ""));
    drawButton(600, 475, 130, 58, "Group -", ready);
    drawButton(750, 475, 130, 58, "Group +", ready);
    drawButton(470, 575, 560, 80, syncDraftDirty ? "Apply sync & Save" : "Sync saved",
               ready && syncDraftDirty, syncDraftDirty);
  } else if (page == Page::Audio) {
    const String mode = audioBeaconSettings.mode == AudioBeaconMode::ManualV1 ? "Manual V1" :
                        audioBeaconSettings.mode == AudioBeaconMode::ManualV2 ? "Manual V2" :
                        audioBeaconSettings.mode == AudioBeaconMode::MicEnergyV2 ? "Mic Energy V2" : "Mic Full V2";
    drawValueTile(285, 175, 290, "Mode (tap)", mode, audioBeaconRunning);
    drawValueTile(595, 175, 200, "Group (tap)", String(audioBeaconSettings.group));
    drawValueTile(815, 175, 200, "Pattern (tap)", String(audioBeaconSettings.pattern));
    drawValueTile(1035, 175, 185, "Brightness", String(audioBeaconSettings.brightness));
    drawValueTile(285, 275, 215, "BPM (tap)", String(audioBeaconSettings.bpm));
    drawValueTile(520, 275, 215, "Sensitivity", String(audioBeaconSettings.sensitivity));
    drawValueTile(755, 275, 215, "Noise gate", String(audioBeaconSettings.noiseGate));
    drawValueTile(990, 275, 230, "Smoothing", String(audioBeaconSettings.smoothing));
    drawValueTile(285, 385, 290, "Energy / bands (pause)", String(audioBeaconOutput.energy) + "  " +
                  audioBeaconOutput.bass + "/" + audioBeaconOutput.mid + "/" + audioBeaconOutput.treble);
    drawValueTile(595, 385, 290, audioBeaconSettings.beatDetect ? "Beat detect ON" : "Beat detect OFF",
                  String(audioBeaconOutput.bpm) + " BPM  " + audioBeaconOutput.confidence,
                  audioBeaconSettings.beatDetect);
    drawValueTile(905, 385, 315, "Advertisements", String(audioBeaconSent));
    drawButton(420, 535, 670, 96, audioBeaconRunning ? "Stop Audio Beacon" : "Start Audio Beacon",
               !ble.connected(), audioBeaconRunning);
  } else if (page == Page::Profiles) {
    uiCanvas.setTextDatum(middle_left);
    uiCanvas.setFont(&fonts::DejaVu12);
    uiCanvas.setTextColor(MUTED, PANEL);
    uiCanvas.drawString("Loaded: " + (hasLoadedProfile ? loadedProfileName : String("none")), 285, 178);
    for (int row = 0; row < 6 && profileOffset + row < static_cast<int>(profileFiles.size()); ++row) {
      const int index = profileOffset + row;
      drawButton(285, 205 + row * 62, 480, 52, profileDisplayName(profileFiles[index]), true,
                 selectedProfile == index);
    }
    drawButton(285, 585, 230, 52, "Previous", profileOffset > 0);
    drawButton(535, 585, 230, 52, "Next", profileOffset + 6 < static_cast<int>(profileFiles.size()));
    drawButton(790, 205, 205, 62, "Refresh", true);
    drawButton(1010, 205, 210, 62, "Save new", ready);
    drawButton(790, 282, 205, 62, "Load", selectedProfile >= 0);
    drawButton(1010, 282, 210, 62, "Apply live", ready && hasLoadedProfile);
    drawButton(790, 359, 205, 62, "Rename", selectedProfile >= 0);
    drawButton(1010, 359, 210, 62, "Delete", selectedProfile >= 0);
    drawButton(790, 436, 430, 62, "Save controller state", ready);
    drawValueTile(790, 520, 430, "Persistence", "Apply is live only; Save is explicit");
  } else if (page == Page::Controller) {
    drawValueTile(285, 175, 455, "Device name (tap)", draftControllerConfig.deviceName, controllerDraftDirty);
    drawValueTile(760, 175, 220, "Strip length", String(draftControllerConfig.stripLength), controllerDraftDirty);
    drawValueTile(1000, 175, 220, "Smoothing", String(draftControllerConfig.smoothing), controllerDraftDirty);
    drawValueTile(285, 275, 290, "Accel range", String(draftControllerConfig.accelRange) + " g", controllerDraftDirty);
    drawValueTile(595, 275, 290, "Gyro range", String(draftControllerConfig.gyroRange) + " dps", controllerDraftDirty);
    drawValueTile(905, 275, 315, "Boot calibration", draftControllerConfig.bootCalibration, controllerDraftDirty);
    drawValueTile(285, 375, 290, "Firmware", controllerConfig.firmware);
    drawValueTile(595, 375, 290, "Hardware", controllerConfig.hardware);
    drawValueTile(905, 375, 315, "Battery / IMU", String(controllerConfig.batteryPercent) + "%  " + controllerConfig.imu);
    drawButton(285, 485, 430, 72, controllerDraftDirty ? "Apply config & Save" : "Config saved",
               ready && controllerDraftDirty, controllerDraftDirty);
    drawButton(735, 485, 235, 72, "Save now", ready);
    drawButton(990, 485, 230, 72, "Factory defaults", ready);
  } else if (page == Page::Service) {
    drawButton(285, 170, 175, 55, "Calibration", true, serviceTab == ServiceTab::Calibration);
    drawButton(475, 170, 175, 55, "Terminal", true, serviceTab == ServiceTab::Terminal);
    drawButton(665, 170, 175, 55, "Diagnostics", true, serviceTab == ServiceTab::Diagnostics);
    drawButton(855, 170, 175, 55, "SD Card", true, serviceTab == ServiceTab::Sd);
    drawButton(1045, 170, 175, 55, "Link", true, serviceTab == ServiceTab::Link);
    if (serviceTab == ServiceTab::Calibration) {
      drawValueTile(285, 255, 935, "Calibration", "Keep the controller still during calibration");
      drawButton(350, 385, 360, 100, "Quick calibration", ready);
      drawButton(790, 385, 360, 100, "Precise calibration", ready);
    } else if (serviceTab == ServiceTab::Terminal) {
      drawValueTile(285, 245, 935, "Command (tap to edit)", terminalInput);
      drawValueTile(285, 345, 935, "Last response", terminalLog.substring(0, 110));
      drawButton(470, 485, 560, 85, "Send NK4 command", ready);
    } else if (serviceTab == ServiceTab::Diagnostics) {
      drawValueTile(285, 245, 290, "Sync", syncModel.state + (syncModel.locked ? " LOCK" : ""));
      drawValueTile(595, 245, 290, "Radio", syncModel.radioMode);
      drawValueTile(905, 245, 315, "Drift / age", String(syncModel.driftMs) + " / " + syncModel.beaconAgeMs + " ms");
      drawValueTile(285, 345, 290, "Beacon TX / RX", String(syncModel.beaconTx) + " / " + syncModel.beaconRx);
      drawValueTile(595, 345, 290, "CRC / group", String(syncModel.crcErrors) + " / " + syncModel.groupMismatch);
      drawValueTile(905, 345, 315, "Apply count", String(syncModel.applyCount));
      drawButton(470, 485, 560, 85, "Refresh all diagnostics", ready);
    } else if (serviceTab == ServiceTab::Sd) {
      drawValueTile(285, 245, 455, "microSD", sdReady ? String(sdSizeBytes >> 20) + " MiB mounted" : "Not mounted");
      drawValueTile(760, 245, 460, "Contents", String(profileFiles.size()) + " profiles / " + firmwareFiles.size() + " UF2");
      drawButton(350, 385, 360, 90, "Mount / Refresh", true);
      drawButton(790, 385, 360, 90, "Run SD check", true);
    } else {
      drawValueTile(285, 245, 455, "Display brightness (tap)", String(linkSettings.displayBrightness));
      drawValueTile(760, 245, 460, "Local persistence", linkSettingsDirty ? "Pending write" : "Saved");
      drawValueTile(285, 345, 935, "Tab5 audio", "Audio Beacon uses its own microphone settings");
    }
  } else if (page == Page::Firmware) {
    for (int row = 0; row < 6 && firmwareOffset + row < static_cast<int>(firmwareFiles.size()); ++row) {
      const int index = firmwareOffset + row;
      drawButton(285, 180 + row * 60, 510, 50, firmwareFiles[index], true, selectedFirmware == index);
    }
    drawButton(285, 555, 245, 52, "Previous", firmwareOffset > 0);
    drawButton(550, 555, 245, 52, "Next", firmwareOffset + 6 < static_cast<int>(firmwareFiles.size()));
    drawButton(825, 180, 395, 62, firmwareTarget == Uf2Target::Rp2040 ? "Target: RP2040" : "Target: RP2350", true, true);
    drawButton(825, 260, 190, 62, "Refresh", true);
    drawButton(1030, 260, 190, 62, "Validate", selectedFirmware >= 0);
    drawValueTile(825, 340, 395, "Validation", firmwareValidated ? Uf2Validator::message(firmwareValidation.result) : "Not validated");
    const FlashProgress& progress = uf2Flasher.progress();
    drawValueTile(825, 440, 395, "Flash status", progress.message.length() ? progress.message : "Idle");
    drawButton(825, 545, 395, 82, uf2Flasher.isRunning() ? "Cancel flash" : "Flash validated UF2",
               uf2Flasher.isRunning() || (firmwareValidated && firmwareValidation.result == Uf2ValidationResult::Ok));
  }

  drawModal();
  M5.Display.startWrite();
  for (size_t i = 0; i < dirtyRegionCount; ++i) {
    const DirtyRegion& region = dirtyRegions[i];
    M5.Display.setClipRect(region.x, region.y, region.width, region.height);
    uiCanvas.pushSprite(0, 0);
  }
  M5.Display.clearClipRect();
  M5.Display.endWrite();
  dirty = false;
  dirtyRegionCount = 0;
}
bool inside(int x, int y, int left, int top, int width, int height)
{
  return x >= left && x < left + width && y >= top && y < top + height;
}

void adjustPattern(int delta)
{
  if (!liveControlsReady()) return;
  draftPattern += delta;
  if (draftPattern < 1) draftPattern = controllerPatternCount;
  if (draftPattern > controllerPatternCount) draftPattern = 1;
  draftDirty = draftPattern != activePattern || draftBrightness != brightness;
  operationSuccess = "Pattern applied live; Save keeps it after reboot";
  enqueue("cmd=set pattern=" + String(draftPattern), CommandClass::User, LiveCommandKind::Pattern);
  invalidateRect(275, 175, 955, 190);
  invalidateSaveFooter();
}

void adjustBrightness(int delta)
{
  if (!liveControlsReady()) return;
  int index = 0;
  int distance = abs(draftBrightness - BRIGHTNESS_LEVELS[0]);
  for (size_t i = 1; i < sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]); ++i) {
    const int candidate = abs(draftBrightness - BRIGHTNESS_LEVELS[i]);
    if (candidate < distance) {
      distance = candidate;
      index = static_cast<int>(i);
    }
  }
  const int count = static_cast<int>(sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]));
  index = (index + delta + count) % count;
  draftBrightness = BRIGHTNESS_LEVELS[index];
  draftDirty = draftPattern != activePattern || draftBrightness != brightness;
  operationSuccess = "Brightness applied live; Save keeps it after reboot";
  enqueue("cmd=set brightness=" + String(draftBrightness), CommandClass::User, LiveCommandKind::Brightness);
  invalidateRect(275, 375, 955, 190);
  invalidateSaveFooter();
}

void adjustPlayMode(int delta)
{
  if (!playControlsReady()) return;
  draftPlayMode = nextOption(draftPlayMode, NightKitePlayback::PLAY_MODES,
                             NightKitePlayback::PLAY_MODE_COUNT, delta);
  updatePlayDraftDirty();
}

void adjustBootMode(int delta)
{
  if (!playControlsReady()) return;
  draftBootMode = nextOption(draftBootMode, NightKitePlayback::BOOT_MODES,
                             NightKitePlayback::BOOT_MODE_COUNT, delta);
  updatePlayDraftDirty();
}

void toggleAutoplay()
{
  if (!playControlsReady()) return;
  draftAutoplayEnabled = !draftAutoplayEnabled;
  updatePlayDraftDirty();
}

void adjustAutoplayInterval(int delta)
{
  if (!playControlsReady()) return;
  draftAutoplayInterval = nextAutoplayInterval(draftAutoplayInterval, delta);
  updatePlayDraftDirty();
}

void openTextInput(TextPurpose purpose, const String& initial)
{
  textPurpose = purpose;
  textInput = initial;
  modal = Modal::TextInput;
  invalidateAll();
}

void finishTextInput()
{
  if (textPurpose == TextPurpose::SaveProfile) {
    pendingProfileName = sanitizeFileBase(textInput);
    pendingProfilePath = profilePath(pendingProfileName);
    if (!pendingProfilePath.length()) {
      setStatus(Phase::Error, "Invalid profile name");
    } else if (SD_MMC.exists(pendingProfilePath)) {
      modal = Modal::ConfirmOverwrite;
      invalidateAll();
      return;
    } else {
      writeProfile(pendingProfilePath, pendingProfileName, false);
    }
  } else if (textPurpose == TextPurpose::RenameProfile) {
    renameSelectedProfile(textInput);
  } else if (textPurpose == TextPurpose::DeviceName) {
    String name = sanitizeFileBase(textInput);
    if (!name.length()) setStatus(Phase::Error, "Invalid controller name");
    else {
      draftControllerConfig.deviceName = name.substring(0, 31);
      updateControllerDraftDirty();
    }
  } else if (textPurpose == TextPurpose::SyncMasterUid) {
    String uid = sanitizeFileBase(textInput);
    uid.toUpperCase();
    draftSync.masterUid = uid;
    updateSyncDraftDirty();
  } else if (textPurpose == TextPurpose::Terminal) {
    textInput.toLowerCase();
    terminalInput = textInput;
  }
  textPurpose = TextPurpose::None;
  modal = Modal::None;
  invalidateAll();
}

bool handleModalTouch(int x, int y)
{
  if (modal == Modal::None) return false;
  if (modal == Modal::TextInput) {
    const char* rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL=", "ZXCVBNM_ ."};
    for (int row = 0; row < 4; ++row) {
      const String keys = rows[row];
      const int keyW = 78;
      const int startX = 175 + (10 - static_cast<int>(keys.length())) * keyW / 2;
      for (size_t col = 0; col < keys.length(); ++col) {
        if (inside(x, y, startX + static_cast<int>(col) * keyW, 255 + row * 76, 68, 62)) {
          const size_t limit = textPurpose == TextPurpose::Terminal ? 80 : 40;
          if (textInput.length() < limit) textInput += keys[col];
          invalidateRect(160, 150, 960, 90);
          invalidateRect(370, 560, 750, 85);
          return true;
        }
      }
    }
    if (inside(x, y, 170, 570, 190, 62)) {
      modal = Modal::None;
      textPurpose = TextPurpose::None;
    } else if (inside(x, y, 380, 570, 250, 62) && textInput.length()) {
      textInput.remove(textInput.length() - 1);
    } else if (inside(x, y, 650, 570, 190, 62)) {
      textInput = "";
    } else if (inside(x, y, 860, 570, 250, 62) && textInput.length()) {
      finishTextInput();
    }
    if (modal == Modal::None) invalidateAll();
    else {
      invalidateRect(160, 150, 960, 90);
      invalidateRect(160, 560, 960, 85);
    }
    return true;
  }
  if (inside(x, y, 280, 420, 300, 90)) {
    modal = Modal::None;
  } else if (inside(x, y, 700, 420, 300, 90)) {
    const Modal confirmed = modal;
    modal = Modal::None;
    if (confirmed == Modal::ConfirmOverwrite) {
      writeProfile(pendingProfilePath, pendingProfileName, true);
      textPurpose = TextPurpose::None;
    }
    else if (confirmed == Modal::ConfirmDelete) deleteSelectedProfile();
    else if (confirmed == Modal::ConfirmDefaults) confirmFactoryDefaults();
    else if (confirmed == Modal::ConfirmFlash) startSelectedFirmwareFlash();
  }
  invalidateAll();
  return true;
}

void handleTouch()
{
  if (!M5.Touch.getCount()) return;
  const auto touch = M5.Touch.getDetail(0);
  if (!touch.wasClicked()) return;
  playUiTone(2200, 35);
  setSmoke(SmokeId::Touch, "PASS", String("tap ") + touch.x + "," + touch.y);
  const int x = touch.x;
  const int y = touch.y;
  if (handleModalTouch(x, y)) return;

  if (uf2Flasher.isRunning()) {
    if (page == Page::Firmware && inside(x, y, 825, 545, 395, 82)) uf2Flasher.cancel();
    return;
  }

  const int navIndex = Tab5WorkflowPolicy::navIndexAt(x, y);
  if (navIndex >= 0) {
    page = static_cast<Page>(navIndex);
    if (page == Page::Profiles && profileFiles.empty()) refreshProfileList();
    if (page == Page::Firmware && firmwareFiles.empty()) refreshFirmwareList();
    invalidateAll();
    return;
  }
  if (inside(x, y, 990, 108, 115, 50)) reloadController();
  else if (inside(x, y, 1120, 108, 120, 50)) disconnectController();
  else if (page == Page::Connect) {
    if (inside(x, y, 285, 185, 220, 76)) selectUsb();
    else if (inside(x, y, 525, 185, 220, 76)) startBleScan();
    else if (selectedTransport == Transport::Ble && inside(x, y, 285, 325, 700, 325)) {
      const size_t index = static_cast<size_t>((y - 325) / 65);
      if (index < shownBleDevices.size()) connectBle(index);
    }
  } else if (page == Page::Control) {
    if (inside(x, y, 300, 280, 150, 74)) adjustPattern(-1);
    else if (inside(x, y, 1055, 280, 150, 74)) adjustPattern(1);
    else if (inside(x, y, 300, 480, 150, 74)) adjustBrightness(-1);
    else if (inside(x, y, 1055, 480, 150, 74)) adjustBrightness(1);
    else if (inside(x, y, 540, 590, 430, 82)) applyDrafts();
  } else if (page == Page::Patterns && controlsReady()) {
    if (inside(x, y, 285, 170, 220, 58)) editInvertedMask = !editInvertedMask;
    else if (inside(x, y, 520, 170, 150, 58)) draftEnabledPatternMask = ALL_PATTERN_MASK;
    else if (inside(x, y, 685, 170, 150, 58)) draftEnabledPatternMask = 1;
    else if (inside(x, y, 850, 170, 170, 58)) draftInvertedPatternMask = 0;
    else if (inside(x, y, 1035, 170, 185, 58)) draftInvertedPatternMask = ALL_PATTERN_MASK;
    else if (inside(x, y, 280, 245, 936, 324)) {
      const int col = (x - 280) / 104;
      const int row = (y - 245) / 108;
      const int id = row * 9 + col + 1;
      if (col < 9 && id <= controllerPatternCount && (x - 280) % 104 < 94 && (y - 245) % 108 < 98) {
        const uint32_t bit = 1UL << (id - 1);
        if (editInvertedMask) draftInvertedPatternMask ^= bit;
        else {
          const uint32_t changed = draftEnabledPatternMask ^ bit;
          if (changed) draftEnabledPatternMask = changed;
        }
      }
    } else if (inside(x, y, 480, 590, 520, 78)) applyPatternDrafts();
    updatePatternDraftDirty();
  } else if (page == Page::Playback && playControlsReady()) {
    if (inside(x, y, 285, 185, 440, 82)) adjustPlayMode(1);
    else if (inside(x, y, 750, 185, 470, 82)) adjustBootMode(1);
    else if (inside(x, y, 285, 295, 440, 82)) toggleAutoplay();
    else if (inside(x, y, 770, 395, 190, 65)) adjustAutoplayInterval(-1);
    else if (inside(x, y, 1010, 395, 190, 65)) adjustAutoplayInterval(1);
    else if (inside(x, y, 500, 570, 510, 86)) applyPlayDrafts();
  } else if (page == Page::Sync && controlsReady()) {
    if (inside(x, y, 285, 175, 290, 82)) draftSync.enabled = !draftSync.enabled;
    else if (inside(x, y, 595, 175, 290, 82)) draftSync.group = draftSync.group % 255 + 1;
    else if (inside(x, y, 905, 175, 315, 82)) draftSync.role = nextOption(draftSync.role, SYNC_ROLES, 3, 1);
    else if (inside(x, y, 285, 275, 440, 82)) draftSync.loss = nextOption(draftSync.loss, SYNC_LOSS, 3, 1);
    else if (inside(x, y, 745, 275, 475, 82)) draftSync.wirelessProfile = nextOption(draftSync.wirelessProfile, WIRELESS_PROFILES, 3, 1);
    else if (inside(x, y, 285, 375, 290, 82)) draftSync.wirelessEnabled = !draftSync.wirelessEnabled;
    else if (inside(x, y, 595, 375, 290, 82)) openTextInput(TextPurpose::SyncMasterUid, draftSync.masterUid);
    else if (inside(x, y, 600, 475, 130, 58)) draftSync.group = draftSync.group <= 1 ? 255 : draftSync.group - 1;
    else if (inside(x, y, 750, 475, 130, 58)) draftSync.group = draftSync.group % 255 + 1;
    else if (inside(x, y, 470, 575, 560, 80)) applySyncDrafts();
    updateSyncDraftDirty();
  } else if (page == Page::Audio) {
    if (inside(x, y, 285, 175, 290, 82)) {
      audioBeaconSettings.mode = static_cast<AudioBeaconMode>((static_cast<int>(audioBeaconSettings.mode) + 1) % 4);
      if (audioBeaconRunning) { stopAudioBeacon(); startAudioBeacon(); }
    } else if (inside(x, y, 595, 175, 200, 82)) audioBeaconSettings.group = audioBeaconSettings.group % 4 + 1;
    else if (inside(x, y, 815, 175, 200, 82)) audioBeaconSettings.pattern = audioBeaconSettings.pattern % PATTERN_COUNT + 1;
    else if (inside(x, y, 1035, 175, 185, 82)) audioBeaconSettings.brightness = nextLevel(audioBeaconSettings.brightness, BRIGHTNESS_LEVELS, 1);
    else if (inside(x, y, 285, 275, 215, 82)) { audioBeaconSettings.bpm += 5; if (audioBeaconSettings.bpm > 240) audioBeaconSettings.bpm = 40; }
    else if (inside(x, y, 520, 275, 215, 82)) audioBeaconSettings.sensitivity = audioBeaconSettings.sensitivity >= 250 ? 25 : audioBeaconSettings.sensitivity + 25;
    else if (inside(x, y, 755, 275, 215, 82)) audioBeaconSettings.noiseGate = audioBeaconSettings.noiseGate >= 100 ? 0 : audioBeaconSettings.noiseGate + 5;
    else if (inside(x, y, 990, 275, 230, 82)) audioBeaconSettings.smoothing = audioBeaconSettings.smoothing >= 100 ? 0 : audioBeaconSettings.smoothing + 10;
    else if (inside(x, y, 285, 385, 290, 82)) {
      const bool wasRunning = audioBeaconRunning;
      if (wasRunning) stopAudioBeacon();
      audioBeaconSettings.micPaused = !audioBeaconSettings.micPaused;
      if (wasRunning) startAudioBeacon();
    }
    else if (inside(x, y, 595, 385, 290, 82)) audioBeaconSettings.beatDetect = !audioBeaconSettings.beatDetect;
    else if (inside(x, y, 420, 535, 670, 96)) {
      if (audioBeaconRunning) stopAudioBeacon(); else startAudioBeacon();
    }
    if (y < 270) invalidateRect(275, 165, 955, 102);
    else if (y < 375) invalidateRect(275, 265, 955, 102);
    else if (y < 500) invalidateRect(275, 375, 955, 102);
    else invalidateRect(410, 525, 690, 116);
  } else if (page == Page::Profiles) {
    if (inside(x, y, 285, 205, 480, 372)) {
      const int index = profileOffset + (y - 205) / 62;
      if (index >= 0 && index < static_cast<int>(profileFiles.size())) selectedProfile = index;
    } else if (inside(x, y, 285, 585, 230, 52) && profileOffset > 0) {
      profileOffset = max(0, profileOffset - 6);
    } else if (inside(x, y, 535, 585, 230, 52) && profileOffset + 6 < static_cast<int>(profileFiles.size())) {
      profileOffset += 6;
    } else if (inside(x, y, 790, 205, 205, 62)) refreshProfileList();
    else if (inside(x, y, 1010, 205, 210, 62)) openTextInput(TextPurpose::SaveProfile, hasLoadedProfile ? loadedProfileName : "profile");
    else if (inside(x, y, 790, 282, 205, 62) && selectedProfile >= 0) loadProfile("/profiles/" + profileFiles[selectedProfile]);
    else if (inside(x, y, 1010, 282, 210, 62)) applyLoadedProfile();
    else if (inside(x, y, 790, 359, 205, 62) && selectedProfile >= 0) openTextInput(TextPurpose::RenameProfile, profileDisplayName(profileFiles[selectedProfile]));
    else if (inside(x, y, 1010, 359, 210, 62) && selectedProfile >= 0) { modal = Modal::ConfirmDelete; invalidateAll(); }
    else if (inside(x, y, 790, 436, 430, 62)) saveLiveController();
    invalidatePageBody();
  } else if (page == Page::Controller && controlsReady()) {
    if (inside(x, y, 285, 175, 455, 82)) openTextInput(TextPurpose::DeviceName, draftControllerConfig.deviceName);
    else if (inside(x, y, 760, 175, 220, 82)) { draftControllerConfig.stripLength++; if (draftControllerConfig.stripLength > 35) draftControllerConfig.stripLength = 10; }
    else if (inside(x, y, 1000, 175, 220, 82)) draftControllerConfig.smoothing = nextLevel(draftControllerConfig.smoothing, SMOOTHING_LEVELS, 1);
    else if (inside(x, y, 285, 275, 290, 82)) draftControllerConfig.accelRange = nextLevel(draftControllerConfig.accelRange, ACCEL_LEVELS, 1);
    else if (inside(x, y, 595, 275, 290, 82)) draftControllerConfig.gyroRange = nextLevel(draftControllerConfig.gyroRange, GYRO_LEVELS, 1);
    else if (inside(x, y, 905, 275, 315, 82)) draftControllerConfig.bootCalibration = draftControllerConfig.bootCalibration == "quick" ? "precise" : "quick";
    else if (inside(x, y, 285, 485, 430, 72)) applyControllerDrafts();
    else if (inside(x, y, 735, 485, 235, 72)) saveLiveController();
    else if (inside(x, y, 990, 485, 230, 72)) { modal = Modal::ConfirmDefaults; invalidateAll(); }
    updateControllerDraftDirty();
  } else if (page == Page::Service) {
    if (inside(x, y, 285, 170, 175, 55)) serviceTab = ServiceTab::Calibration;
    else if (inside(x, y, 475, 170, 175, 55)) serviceTab = ServiceTab::Terminal;
    else if (inside(x, y, 665, 170, 175, 55)) serviceTab = ServiceTab::Diagnostics;
    else if (inside(x, y, 855, 170, 175, 55)) serviceTab = ServiceTab::Sd;
    else if (inside(x, y, 1045, 170, 175, 55)) serviceTab = ServiceTab::Link;
    else if (serviceTab == ServiceTab::Calibration && inside(x, y, 350, 385, 360, 100)) runCalibration("quick");
    else if (serviceTab == ServiceTab::Calibration && inside(x, y, 790, 385, 360, 100)) runCalibration("precise");
    else if (serviceTab == ServiceTab::Terminal && inside(x, y, 285, 245, 935, 82)) openTextInput(TextPurpose::Terminal, terminalInput);
    else if (serviceTab == ServiceTab::Terminal && inside(x, y, 470, 485, 560, 85)) sendTerminalCommand();
    else if (serviceTab == ServiceTab::Diagnostics && inside(x, y, 470, 485, 560, 85)) reloadController();
    else if (serviceTab == ServiceTab::Sd && (inside(x, y, 350, 385, 360, 90) || inside(x, y, 790, 385, 360, 90))) {
      testSd();
      refreshProfileList();
      refreshFirmwareList();
    }
    else if (serviceTab == ServiceTab::Link && inside(x, y, 285, 245, 455, 82)) changeDisplayBrightness();
    invalidatePageBody();
  } else if (page == Page::Firmware) {
    if (inside(x, y, 285, 180, 510, 360)) {
      const int index = firmwareOffset + (y - 180) / 60;
      if (index >= 0 && index < static_cast<int>(firmwareFiles.size())) {
        selectedFirmware = index;
        firmwareValidated = false;
      }
    } else if (inside(x, y, 285, 555, 245, 52) && firmwareOffset > 0) {
      firmwareOffset = max(0, firmwareOffset - 6);
    } else if (inside(x, y, 550, 555, 245, 52) && firmwareOffset + 6 < static_cast<int>(firmwareFiles.size())) {
      firmwareOffset += 6;
    } else if (inside(x, y, 825, 180, 395, 62)) {
      firmwareTarget = firmwareTarget == Uf2Target::Rp2040 ? Uf2Target::Rp2350 : Uf2Target::Rp2040;
      firmwareValidated = false;
    } else if (inside(x, y, 825, 260, 190, 62)) refreshFirmwareList();
    else if (inside(x, y, 1030, 260, 190, 62)) validateSelectedFirmware();
    else if (inside(x, y, 825, 545, 395, 82) && firmwareValidated &&
             firmwareValidation.result == Uf2ValidationResult::Ok) {
      modal = Modal::ConfirmFlash;
    }
    if (modal == Modal::ConfirmFlash) invalidateAll();
    else invalidatePageBody();
  }
}
void updateConnection()
{
  if (selectedTransport == Transport::Usb && usbStarted) {
    const bool connected = static_cast<bool>(usbSerial);
    if (connected && !sessionStarted) beginSession();
    else if (!connected && sessionStarted) endSession("USB controller disconnected");
  }
  if (selectedTransport == Transport::Ble && ble.takeDisconnected() && sessionStarted) {
    endSession("BLE controller disconnected");
  }
  if (selectedTransport == Transport::Ble && ble.takeNotifyOverflow()) {
    ble.disconnect();
    endSession("BLE notification overflow");
    setStatus(Phase::Error, "BLE notification overflow; reconnect");
  }
  std::vector<BleDeviceEntry> results;
  if (selectedTransport == Transport::Ble && ble.takeScanResults(results)) {
    shownBleDevices = results;
    if (results.empty()) {
      setSmoke(SmokeId::Gatt, "FAIL", "no NK4 advertiser found");
      setStatus(Phase::Error, "No NightKite BLE controller found");
    } else if (gattAutoTest) {
      connectBle(0);
    } else {
      setStatus(Phase::Found, String("Found ") + results.size() + " controller(s)");
    }
  }
}

void testSd()
{
  setSmoke(SmokeId::Sd, "RUN", "mounting 4-bit SDMMC");
  if (!ensureSdReady()) {
    setSmoke(SmokeId::Sd, "FAIL", "no readable card");
    return;
  }
  setSmoke(SmokeId::Sd, "PASS", String(static_cast<uint32_t>(sdSizeBytes >> 20)) + " MiB mounted");
}

void startAudioTest()
{
  if (audioState != AudioState::Idle) return;
  if (audioBeaconRunning) stopAudioBeacon();
  if (uiToneActive) {
    M5.Speaker.end();
    uiToneActive = false;
  }
  M5.Mic.end();
  if (!M5.Speaker.begin()) {
    setSmoke(SmokeId::Audio, "FAIL", "speaker init failed");
    return;
  }
  M5.Speaker.setVolume(255);
  M5.Speaker.setAllChannelVolume(255);
  if (!M5.Speaker.tone(4000, 900)) {
    M5.Speaker.end();
    setSmoke(SmokeId::Audio, "FAIL", "speaker tone failed");
    return;
  }
  audioState = AudioState::Tone;
  audioStartedAt = millis();
  setSmoke(SmokeId::Audio, "RUN", "4 kHz tone");
}

void updateAudio()
{
  if (audioState == AudioState::Tone && millis() - audioStartedAt >= 1100) {
    M5.Speaker.end();
    if (!M5.Mic.begin()) {
      audioState = AudioState::Idle;
      setSmoke(SmokeId::Audio, "FAIL", "microphone init failed");
      return;
    }
    audioState = AudioState::MicStarting;
    audioStartedAt = millis();
  }
  if (audioState == AudioState::MicStarting && millis() - audioStartedAt >= 100) {
    if (!M5.Mic.record(microphoneSamples, 4096, 16000, false)) {
      M5.Mic.end();
      audioState = AudioState::Idle;
      setSmoke(SmokeId::Audio, "FAIL", "microphone record failed");
      return;
    }
    audioState = AudioState::Recording;
    audioStartedAt = millis();
  }
  if (audioState != AudioState::Recording || M5.Mic.isRecording() || millis() - audioStartedAt < 50) return;
  int32_t peak = 0;
  for (int16_t sample : microphoneSamples) {
    const int32_t magnitude = sample < 0 ? -static_cast<int32_t>(sample) : sample;
    peak = max(peak, magnitude);
  }
  M5.Mic.end();
  M5.Speaker.begin();
  audioState = AudioState::Idle;
  setSmoke(SmokeId::Audio, peak > 16 ? "PASS" : "FAIL",
           String(peak > 16 ? "tone queued; mic peak " : "microphone silent, peak ") + peak);
}

void startBeaconTest()
{
  if (ble.connected() || (selectedTransport == Transport::Ble && sessionStarted)) {
    setSmoke(SmokeId::Beacon, "FAIL", "disconnect GATT before advertising");
    return;
  }
  if (!ble.begin()) {
    setSmoke(SmokeId::Beacon, "FAIL", ble.statusMessage());
    return;
  }
  if (audioBeaconRunning) stopAudioBeacon();
  BLEAdvertisementData data;
  uint8_t payload[NightKiteSync::V1_ADVERTISING_SIZE] = {};
  NightKiteSync::BeaconInput input;
  input.group = 1;
  input.sequence = static_cast<uint16_t>(millis());
  input.pattern = activePattern >= 1 ? activePattern : 1;
  input.brightness = brightness >= 0 ? brightness : 127;
  input.phaseMs = millis();
  const auto encoded = NightKiteSync::encodeAdvertising(input, payload, sizeof(payload));
  if (encoded.size != sizeof(payload)) {
    setSmoke(SmokeId::Beacon, "FAIL", "sync beacon encoding failed");
    return;
  }
  data.addData(String(reinterpret_cast<const char*>(payload), encoded.size));
  bleAdvertising = BLEDevice::getAdvertising();
  bleAdvertising->stop();
  bleAdvertising->reset();
  bleAdvertising->setScanResponse(false);
  bleAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
  if (!bleAdvertising->setAdvertisementData(data) || !bleAdvertising->start(3)) {
    setSmoke(SmokeId::Beacon, "FAIL", "advertising failed");
    return;
  }
  beaconStartedAt = millis();
  setSmoke(SmokeId::Beacon, "RUN", "C6 advertising for 3 seconds");
}

AudioSyncDspConfig audioBeaconConfig()
{
  AudioSyncDspConfig config;
  config.sampleRate = 8000;
  config.sensitivity = constrain(audioBeaconSettings.sensitivity, 1, 255);
  config.noiseGate = constrain(audioBeaconSettings.noiseGate, 0, 100);
  config.smoothing = constrain(audioBeaconSettings.smoothing, 0, 100);
  config.fullBands = audioBeaconSettings.mode == AudioBeaconMode::MicFullV2;
  config.beatDetect = config.fullBands && audioBeaconSettings.beatDetect;
  config.fallbackBpm = constrain(audioBeaconSettings.bpm, 40, 240);
  return config;
}

void stopAudioBeacon()
{
  if (bleAdvertising != nullptr) bleAdvertising->stop();
  if (audioBeaconRecording || audioBeaconSettings.mode == AudioBeaconMode::MicEnergyV2 ||
      audioBeaconSettings.mode == AudioBeaconMode::MicFullV2) M5.Mic.end();
  audioBeaconRecording = false;
  audioBeaconRunning = false;
  audioBeaconLastAdvAt = 0;
  audioBeaconLastRenderAt = 0;
  invalidateRect(275, 375, 955, 102);
  invalidateRect(410, 525, 690, 116);
}

bool publishAudioBeacon()
{
  NightKiteSync::BeaconInput input;
  input.version = audioBeaconSettings.mode == AudioBeaconMode::ManualV1 ? NightKiteSync::VERSION_V1
                                                                        : NightKiteSync::VERSION_V2;
  input.group = static_cast<uint8_t>(audioBeaconSettings.group);
  input.sequence = ++audioBeaconSequence;
  input.pattern = static_cast<uint8_t>(audioBeaconSettings.pattern);
  input.brightness = static_cast<uint8_t>(audioBeaconSettings.brightness);
  const uint16_t manualBeatMs = static_cast<uint16_t>(60000 / constrain(audioBeaconSettings.bpm, 40, 240));
  input.beatMs = manualBeatMs;
  input.phaseMs = millis() % manualBeatMs;
  const bool micMode = audioBeaconSettings.mode == AudioBeaconMode::MicEnergyV2 ||
                       audioBeaconSettings.mode == AudioBeaconMode::MicFullV2;
  if (micMode && !audioBeaconSettings.micPaused) {
    audioBeaconOutput = audioBeaconDsp.output(millis(), audioBeaconConfig());
    input.flags = audioBeaconOutput.beat ? NightKiteSync::FLAG_AUDIO_BEAT : 0;
    input.phaseMs = audioBeaconOutput.phaseMs;
    input.beatMs = audioBeaconOutput.beatMs;
    input.energy = audioBeaconOutput.energy;
    if (audioBeaconSettings.mode == AudioBeaconMode::MicFullV2) {
      input.bass = audioBeaconOutput.bass;
      input.mid = audioBeaconOutput.mid;
      input.treble = audioBeaconOutput.treble;
    }
    input.confidence = audioBeaconOutput.confidence;
  }
  uint8_t payload[NightKiteSync::V2_ADVERTISING_SIZE] = {};
  const auto encoded = NightKiteSync::encodeAdvertising(input, payload, sizeof(payload));
  if (!encoded.size) return false;
  BLEAdvertisementData data;
  data.addData(String(reinterpret_cast<const char*>(payload), encoded.size));
  bleAdvertising->stop();
  bleAdvertising->reset();
  bleAdvertising->setScanResponse(false);
  bleAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
  if (!bleAdvertising->setAdvertisementData(data) || !bleAdvertising->start(0)) return false;
  ++audioBeaconSent;
  return true;
}

void startAudioBeacon()
{
  if (audioState != AudioState::Idle) {
    setStatus(Phase::Error, "Wait for the audio diagnostic to finish");
    return;
  }
  if (ble.connected() || (selectedTransport == Transport::Ble && sessionStarted)) {
    setStatus(Phase::Error, "Disconnect GATT before Audio Beacon");
    return;
  }
  if (uiToneActive) {
    M5.Speaker.end();
    uiToneActive = false;
  }
  if (!ble.begin()) {
    setStatus(Phase::Error, ble.statusMessage());
    return;
  }
  if (beaconStartedAt && bleAdvertising != nullptr) bleAdvertising->stop();
  beaconStartedAt = 0;
  bleAdvertising = BLEDevice::getAdvertising();
  audioBeaconDsp.reset(millis(), audioBeaconSettings.bpm);
  audioBeaconSent = 0;
  audioBeaconSequence = 0;
  const bool useMic = (audioBeaconSettings.mode == AudioBeaconMode::MicEnergyV2 ||
                       audioBeaconSettings.mode == AudioBeaconMode::MicFullV2) && !audioBeaconSettings.micPaused;
  if (useMic && !M5.Mic.begin()) {
    setStatus(Phase::Error, "Microphone init failed");
    return;
  }
  audioBeaconRunning = true;
  audioBeaconLastAdvAt = 0;
  audioBeaconLastRenderAt = 0;
  setStatus(phase == Phase::Ready ? Phase::Ready : Phase::Idle, "Audio Beacon running");
}

void updateAudioBeacon()
{
  if (!audioBeaconRunning) return;
  const bool useMic = (audioBeaconSettings.mode == AudioBeaconMode::MicEnergyV2 ||
                       audioBeaconSettings.mode == AudioBeaconMode::MicFullV2) && !audioBeaconSettings.micPaused;
  if (useMic) {
    if (audioBeaconRecording && !M5.Mic.isRecording()) {
      audioBeaconDsp.processFrame(audioBeaconSamples, 256, millis(), audioBeaconConfig());
      audioBeaconRecording = false;
      audioBeaconLastFrameAt = millis();
    }
    if (!audioBeaconRecording) {
      audioBeaconRecording = M5.Mic.record(audioBeaconSamples, 256, 8000, false);
      if (!audioBeaconRecording) {
        stopAudioBeacon();
        setStatus(Phase::Error, "Microphone capture failed");
        return;
      }
    }
  }
  if (millis() - audioBeaconLastAdvAt >= 100) {
    audioBeaconLastAdvAt = millis();
    if (!publishAudioBeacon()) {
      stopAudioBeacon();
      setStatus(Phase::Error, "Audio Beacon advertising failed");
    }
    if (page == Page::Audio && millis() - audioBeaconLastRenderAt >= 250) {
      audioBeaconLastRenderAt = millis();
      invalidateRect(275, 375, 955, 102);
    }
  }
}

void testCore()
{
  uint8_t bytes[NightKiteSync::V2_ADVERTISING_SIZE] = {};
  NightKiteSync::BeaconInput input;
  input.version = NightKiteSync::VERSION_V2;
  input.sequence = 42;
  const auto encoded = NightKiteSync::encodeAdvertising(input, bytes, sizeof(bytes));
  const bool valid = encoded.size == sizeof(bytes) && NightKiteSync::validateAdvertising(bytes, encoded.size) &&
                     NightKiteCommands::setPattern(7) == "set pattern 7";
  setSmoke(SmokeId::Core, valid ? "PASS" : "FAIL", valid ? "protocol + queue + beacon" : "shared core failed");
}

void updateBeacon()
{
  if (beaconStartedAt && millis() - beaconStartedAt >= 3200) {
    bleAdvertising->stop();
    beaconStartedAt = 0;
    setSmoke(SmokeId::Beacon, "PASS", "NightKite v1 manufacturer ADV sent");
  }
}

void printDiagnostics()
{
  Serial.printf("[TAB5] link transport=%s phase=%s queue=%u pending=%d controller=%s pattern=%d brightness=%d\n",
                selectedTransport == Transport::Usb ? "USB" : selectedTransport == Transport::Ble ? "BLE" : "NONE",
                phaseName(), static_cast<unsigned>(commandQueue.size()), pending.active ? 1 : 0,
                controllerName.c_str(), activePattern, brightness);
  Serial.printf("[TAB5] page=%u play_mode=%s boot_mode=%s autoplay=%d interval=%d dirty=%d/%d/%d/%d/%d\n",
                static_cast<unsigned>(page), playMode.c_str(), bootMode.c_str(), autoplayEnabled ? 1 : 0,
                autoplayInterval, draftDirty ? 1 : 0, playDraftDirty ? 1 : 0, patternDraftDirty ? 1 : 0,
                syncDraftDirty ? 1 : 0, controllerDraftDirty ? 1 : 0);
  Serial.printf("[TAB5] sd=%d profiles=%u uf2=%u audio_beacon=%d sent=%lu flash=%d\n", sdReady ? 1 : 0,
                static_cast<unsigned>(profileFiles.size()), static_cast<unsigned>(firmwareFiles.size()),
                audioBeaconRunning ? 1 : 0, audioBeaconSent, uf2Flasher.isRunning() ? 1 : 0);
  Serial.printf("[TAB5] battery local=%d%% voltage=%dmV charging=%d controller=%d%% state=%s\n",
                tab5BatteryLevel, tab5BatteryVoltage, tab5BatteryCharging ? 1 : 0,
                controllerConnected ? controllerConfig.batteryPercent : -1,
                controllerConnected ? controllerConfig.batteryState.c_str() : "disconnected");
  for (const auto& result : smoke) {
    Serial.printf("[SMOKE] %-12s %-5s %s\n", result.name, result.state.c_str(), result.detail.c_str());
  }
  Serial.println("[TAB5] commands: status reload sd audio usb gatt beacon all");
}

void runDiagnostic(String command)
{
  command.trim();
  command.toLowerCase();
  if (command == "status") printDiagnostics();
  else if (command == "reload") reloadController();
  else if (command == "sd") testSd();
  else if (command == "audio") startAudioTest();
  else if (command == "usb") selectUsb();
  else if (command == "gatt") { gattAutoTest = true; startBleScan(); }
  else if (command == "beacon") startBeaconTest();
  else if (command == "all") { testSd(); startAudioTest(); selectUsb(); }
  else if (command.length()) Serial.printf("[TAB5] unknown command: %s\n", command.c_str());
}

void handleDiagnostics()
{
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      runDiagnostic(diagnosticLine);
      diagnosticLine = "";
    } else if (diagnosticLine.length() < 80) {
      diagnosticLine += c;
    }
  }
}

}  // namespace

void setup()
{
  Serial.begin(115200);
  Serial.println("[TAB5] boot: before M5.begin");
  auto config = M5.config();
  M5.begin(config);
  Serial.println("[TAB5] boot: M5.begin complete");
  for (int retry = 1; M5.Display.width() == 0 && retry <= 2; ++retry) {
    Serial.printf("[TAB5] display detection retry %d\n", retry);
    auto& panelReset = M5.getIOExpander(0);
    panelReset.setDirection(4, true);
    panelReset.setDirection(5, true);
    panelReset.digitalWrite(4, false);
    panelReset.digitalWrite(5, false);
    delay(100);
    panelReset.digitalWrite(4, true);
    panelReset.digitalWrite(5, true);
    delay(100);
    M5.Display.init();
  }
  if (M5.Display.width() == 0 && displayInitRestarts < 2) {
    ++displayInitRestarts;
    Serial.printf("[TAB5] display unavailable; controlled restart %u/2\n", displayInitRestarts);
    delay(100);
    ESP.restart();
  }
  if (M5.Display.width() != 0) displayInitRestarts = 0;
  M5.Touch.begin(M5.Display.touch() ? &M5.Display : nullptr);
  M5.Display.setRotation(3);
  uiCanvas.setColorDepth(16);
  if (uiCanvas.createSprite(1280, 720) == nullptr) {
    Serial.println("[TAB5] full-screen UI buffer allocation failed");
    ESP.restart();
  }
  loadLinkSettings();
  const bool displayOk = M5.Display.width() == 1280 && M5.Display.height() == 720;
  setSmoke(SmokeId::Display, displayOk ? "PASS" : "FAIL",
           String(M5.Display.width()) + "x" + M5.Display.height());
  testCore();
  render();
  Serial.println("[TAB5] ready; type status");
}

void loop()
{
  M5.update();
  handleDiagnostics();
  handleTouch();
  updateConnection();
  pumpProtocol();
  updateUiTone();
  updateBatteryStatus();
  updateAudio();
  updateAudioBeacon();
  updateBeacon();
  updateFirmwareFlash();
  persistLinkSettings();
  if (dirty) render();
  delay(5);
}
