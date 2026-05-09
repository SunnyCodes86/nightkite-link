#pragma once

#include <Arduino.h>
#include "SoundAssets.h"

class SoundManager {
public:
  void begin();

  void setEnabled(bool enabled);
  bool isEnabled() const;

  void setVolume(uint8_t volume);
  uint8_t volume() const;

  void playStartup();
  void playKey();
  void playTextKey();
  void playNavigate();
  void playPageChange();
  void playTransferComplete();
  void playConfirm();
  void playCancel();
  void playSuccess();
  void playError();

  void update();
  bool isPlaying() const;

private:
  uint8_t chooseNextKeyVariant();
  void refillKeyVariantBag();
  void playClip(const NightKiteSoundAssets::SoundClip& clip, bool interrupt = true);
  bool shouldPlayUiClick();

  bool enabled = true;
  uint8_t currentVolume = 210;
  unsigned long lastClickMs = 0;
  uint8_t bufferIndex = 0;
  uint8_t lastKeyVariant = 0xFF;
  uint8_t keyVariantBag[NightKiteSoundAssets::KEY_VARIANT_COUNT] = {};
  uint8_t keyVariantBagIndex = NightKiteSoundAssets::KEY_VARIANT_COUNT;
  uint8_t navigateVariantIndex = 0;
};
