// Cardputer hardware and existing carousel bindings; shared runtime owns events.
int showField = 0, showTargetKind = 0, showPattern = 6, showBrightness = 159, showColor = 0, showFileIndex = 0;
uint32_t showGroup = 1, showSingle = 0;
bool showSingleSelected = false;
String showSingleName;
String showFiles[16]; unsigned showFileCount = 0;
const uint8_t showColors[][3] = {{255,255,255},{255,0,0},{0,255,0},{0,0,255},{255,100,0}};
const char* showColorNames[] = {"White","Red","Green","Blue","Amber"};
const char* showFields[] = {"ARM / DISARM","Target kind","Group / Controller","Pattern","Brightness","Solid color","Show file","PLAY","STOP / RELEASE","USB role (reboot)"};

bool showArmHardware() {
  if (flashWorkflowActive()) return false;
  if (app.transportMode == TransportMode::Ble || bleTransport.connected()) disconnectBleDevice();
  beaconBroadcaster.stop();
  ensureBleStackStarted(); BLEDevice::getScan()->stop();
  app.bleScanning = false;
  return BLEDevice::getAdvertising() != nullptr;
}
void showOffHardware() { BLEDevice::getAdvertising()->stop(); cardputerAudioSync.end(); }
bool showAdvertise(const uint8_t* bytes, size_t size) {
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  BLEAdvertisementData data; data.addData(String((const char*)bytes, size));
  adv->stop(); adv->setScanResponse(false);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
  adv->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
#else
  adv->setAdvertisementType(ADV_TYPE_NONCONN_IND);
#endif
  adv->setMinInterval(32); adv->setMaxInterval(40);
  return adv->setAdvertisementData(data) && adv->start();
}
bool showAudio(NightKiteSync::BeaconInput& input) {
  if (!beaconModeUsesV2(beaconMasterSettings)) return false;
  const auto audio = effectiveBeaconAudioValues(beaconMasterSettings);
  input.version = NightKiteSync::VERSION_V2; input.group = beaconMasterSettings.group;
  input.pattern = beaconMasterSettings.pattern; input.brightness = beaconMasterSettings.brightness;
  input.phaseMs = beaconBroadcaster.phaseMs(beaconMasterSettings); input.beatMs = beaconBroadcaster.beatMs(beaconMasterSettings);
  input.flags = audio.beat ? NightKiteSync::FLAG_AUDIO_BEAT : 0;
  input.energy = audio.energy; input.bass = audio.bass; input.mid = audio.mid; input.treble = audio.treble; input.confidence = audio.confidence;
  return true;
}
const char* showAudioMode(const char* modeName) {
  if (!strcmp(modeName, "OFF")) beaconMasterSettings.mode = BeaconMasterMode::V1Manual;
  else if (!strcmp(modeName, "MANUAL")) beaconMasterSettings.mode = BeaconMasterMode::V2Manual;
  else if (!strcmp(modeName, "MIC_ENERGY")) beaconMasterSettings.mode = BeaconMasterMode::V2MicEnergy;
  else if (!strcmp(modeName, "MIC_FULL")) beaconMasterSettings.mode = BeaconMasterMode::V2MicFull;
  else return "audio_mode";
  return startBeaconMicIfNeeded() ? nullptr : "mic";
}
void setupShow() {
  ShowRuntime::Hardware hardware = {showArmHardware, showOffHardware, showAdvertise, showAudio, showAudioMode, ensureSdReady};
  showRuntime.begin(SD, hardware);
}
void scanShowFiles() {
  showFileCount = 0; showFileIndex = 0;
  char names[384]; showRuntime.list(names, sizeof(names));
  char* save = nullptr;
  for (char* name = strtok_r(names, ",", &save); name && showFileCount < 16; name = strtok_r(nullptr, ",", &save)) {
    if (String(name).endsWith(".nks")) showFiles[showFileCount++] = name;
  }
}
NightKiteShow::Target selectedShowTarget() {
  NightKiteShow::Target target; target.kind = NightKiteShow::TargetKind(showTargetKind);
  target.value = showTargetKind == 1 ? showGroup : showTargetKind == 2 ? showSingle : 0; return target;
}
void showResult(const char* error) { setStatus(error ? String(error) : "Show accepted", error ? COLOR_ERR : COLOR_OK); app.dirty = true; }
void showTrigger(NightKiteShow::Command command) {
  if (showTargetKind == 2 && !showSingleSelected) { showResult("Select controller first"); return; }
  NightKiteShow::Event event; event.target = selectedShowTarget(); event.command = command;
  if (command == NightKiteShow::Command::Pattern) event.params[0] = showPattern;
  if (command == NightKiteShow::Command::Brightness) event.params[0] = showBrightness;
  if (command == NightKiteShow::Command::Solid) {
    memcpy(event.params, showColors[showColor], 3); event.params[3] = showBrightness;
  }
  uint16_t id; showResult(showRuntime.live(event, id));
}
void changeShowValue(int amount) {
  switch (showField) {
    case 1: showTargetKind = (showTargetKind + amount + 3) % 3; break;
    case 2:
      if (showTargetKind == 1) showGroup = wrapRange(showGroup, 1, 255, 1, amount);
      else if (showTargetKind == 2) {
        if (!app.bleDevices.empty()) {
          app.selectedBleIndex = wrapRange(app.selectedBleIndex, 0, app.bleDevices.size() - 1, 1, amount);
          String name = app.bleDevices[app.selectedBleIndex].name;
          showSingleSelected = name.startsWith("NK-") && name.length() == 9 && NightKiteShow::shortId(name.substring(3).c_str(), showSingle);
          showSingleName = name;
        } else {
          showSingleSelected = NightKiteShow::shortId(app.identity.shortId.c_str(), showSingle);
          showSingleName = app.identity.name;
        }
      }
      break;
    case 3: showPattern = wrapRange(showPattern, 1, 27, 1, amount); break;
    case 4: showBrightness = constrain(showBrightness + amount * 16, 1, 255); break;
    case 5: showColor = (showColor + amount + 5) % 5; break;
    case 6: if (showFileCount) showFileIndex = wrapRange(showFileIndex, 0, showFileCount - 1, 1, amount); break;
  }
  app.dirty = true;
}
void applyShowAction() {
  switch (showField) {
    case 0: if (showRuntime.engine.active()) showRuntime.stop(true); else showResult(showRuntime.arm()); break;
    case 3: showTrigger(NightKiteShow::Command::Pattern); break;
    case 4: showTrigger(NightKiteShow::Command::Brightness); break;
    case 5: showTrigger(NightKiteShow::Command::Solid); break;
    case 6: if (showFileCount) showResult(showRuntime.load(showFiles[showFileIndex].c_str())); else scanShowFiles(); break;
    case 7: showResult(showRuntime.play()); break;
    case 8: showRuntime.stop(); break;
    case 9:
      if (showRuntime.engine.active()) { showResult("Disarm first"); break; }
      linkSettings.usbBridge = !linkSettings.usbBridge;
      linkSettingsChanged();
      persistLinkSettings(true);
      if (linkSettingsDirty) showResult("USB mode save failed");
      else setStatus(linkSettings.usbBridge ? "Saved: reboot for Bridge" : "Saved: unplug PC, reboot", COLOR_WARN);
      break;
    default: break;
  }
  app.dirty = true;
}
bool showKey(char c) {
  if (c == 'c' || c == 'C') { showField = (showField + 1) % 10; app.dirty = true; return true; }
  if (c == 'w' || c == 'W') { showColor = 0; showTrigger(NightKiteShow::Command::Solid); return true; }
  if (c == 'b' || c == 'B') { showTrigger(NightKiteShow::Command::Blackout); return true; }
  if (c == 'x' || c == 'X') { showRuntime.stop(); return true; }
  if (c == 'r' || c == 'R') { showTrigger(NightKiteShow::Command::Release); return true; }
  if (c == 'f' || c == 'F') { scanShowFiles(); app.dirty = true; return true; }
  if (c == 'a' || c == 'A') { if (showRuntime.engine.active()) showRuntime.stop(true); else showResult(showRuntime.arm()); return true; }
  return false;
}
void drawShowCard() {
  auto& d = uiCanvas; d.setTextColor(COLOR_ACCENT, COLOR_BG);
  d.drawString(String("SHOW ") + showRuntime.engine.stateName() + (bridgeMode ? " USB" : " Live"), 6, 17);
  char target[40];
  if (!showTargetKind) snprintf(target, sizeof(target), "ALL");
  else if (showTargetKind == 1) snprintf(target, sizeof(target), "GROUP %lu", (unsigned long)showGroup);
  else snprintf(target, sizeof(target), "SINGLE %06lX", (unsigned long)showSingle);
  d.setTextColor(COLOR_TEXT, COLOR_BG); d.drawString(target, 6, 32);
  d.drawString(String(showFields[showField]), 6, 47);
  String value;
  if (showField == 2 && showTargetKind == 2) value = showSingleSelected ? showSingleName : "Scan BLE / read USB first";
  else if (showField == 3) value = String(showPattern);
  else if (showField == 4) value = String(showBrightness);
  else if (showField == 5) value = showColorNames[showColor];
  else if (showField == 6) value = showFileCount ? showFiles[showFileIndex] : "F: scan /shows";
  else if (showField == 9) value = String(bridgeMode ? "Bridge" : "Host") + " -> " + (linkSettings.usbBridge ? "Bridge" : "Host") + " (next boot)";
  else value = showRuntime.player.stateName();
  drawTextFit(value, 6, 62, 228, COLOR_ACCENT);
  d.drawString(String("t ") + showRuntime.player.time(millis()) + " id " + showRuntime.engine.counters.lastId + " q " + showRuntime.engine.depth(), 6, 78);
  d.drawString(String("next ") + showRuntime.engine.nextTime() + " bridge " + (showRuntime.bridgeSeen ? "seen" : "idle"), 6, 91);
  drawTextFit("A arm W white B black R release X stop", 6, 105, 228, COLOR_MUTED);
  drawFooterHints("C field +/- value", "Enter / F files");
}
void tickShow() {
  showRuntime.tick(bridgeMode ? &bridgeSerial : nullptr);
  static uint32_t lastUi = 0;
  if (static_cast<Card>(app.selectedCard) == Card::Show && millis() - lastUi >= 200) { lastUi = millis(); app.dirty = true; }
}
