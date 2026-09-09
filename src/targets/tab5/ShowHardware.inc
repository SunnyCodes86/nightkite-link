// Hosted/NimBLE adapter. Timeline, USB API, SD player and radio arbitration are shared.
const char* showAudioMode(const char* mode) {
  AudioBeaconMode value;
  if (!strcmp(mode, "OFF")) value = AudioBeaconMode::ManualV1;
  else if (!strcmp(mode, "MANUAL")) value = AudioBeaconMode::ManualV2;
  else if (!strcmp(mode, "MIC_ENERGY")) value = AudioBeaconMode::MicEnergyV2;
  else if (!strcmp(mode, "MIC_FULL")) value = AudioBeaconMode::MicFullV2;
  else return "audio_mode";
  if (audioState != AudioState::Idle) return "audio_busy";
  M5.Mic.end(); audioBeaconRecording = false;
  audioBeaconSettings.mode = value; audioBeaconSettings.micPaused = false;
  audioBeaconDsp.reset(millis());
  if (value == AudioBeaconMode::MicEnergyV2 || value == AudioBeaconMode::MicFullV2) {
    M5.Speaker.end(); uiToneActive = false;
    if (!M5.Mic.begin()) return "mic";
  }
  return nullptr;
}
bool showArmHardware() {
  if (uf2Flasher.isRunning() || audioState != AudioState::Idle || phase == Phase::Scanning ||
      phase == Phase::Connecting || ble.connected()) return false;
  stopAudioBeacon(); beaconStartedAt = 0;
  if (!ble.begin()) return false;
  bleAdvertising = BLEDevice::getAdvertising();
  const char* mode = audioBeaconSettings.mode == AudioBeaconMode::MicFullV2 ? "MIC_FULL" :
      audioBeaconSettings.mode == AudioBeaconMode::MicEnergyV2 ? "MIC_ENERGY" :
      audioBeaconSettings.mode == AudioBeaconMode::ManualV2 ? "MANUAL" : "OFF";
  return bleAdvertising && !showAudioMode(mode);
}
void showOffHardware() { stopAudioBeacon(); }
bool showAdvertise(const uint8_t* bytes, size_t size) {
  BLEAdvertisementData data; data.addData(String((const char*)bytes, size));
  bleAdvertising->stop(); bleAdvertising->reset();
  bleAdvertising->setScanResponse(false); bleAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
  bleAdvertising->setMinInterval(32); bleAdvertising->setMaxInterval(40);
  return bleAdvertising->setAdvertisementData(data) && bleAdvertising->start(0);
}
bool showAudio(NightKiteSync::BeaconInput& input) {
  if (audioBeaconSettings.mode == AudioBeaconMode::ManualV1) return false;
  input = currentAudioBeacon(); return true;
}
void setupShow() {
  ShowRuntime::Hardware hardware = {showArmHardware, showOffHardware, showAdvertise, showAudio, showAudioMode, ensureSdReady};
  showRuntime.begin(SD_MMC, hardware);
}
