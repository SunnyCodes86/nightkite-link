#include "UsbMscUf2Flasher.h"

#include <SD.h>
#include <cstdio>
#include <cstring>
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"

namespace {

constexpr const char* USB_MOUNT_POINT = "/usb";
constexpr const char* USB_UF2_TARGET = "/usb/FIRMWARE.UF2";
constexpr unsigned long MASS_STORAGE_TIMEOUT_MS = 30000;
constexpr unsigned long REBOOT_WAIT_MS = 12000;
constexpr size_t COPY_BUFFER_SIZE = 4096;

const char* flashResultText(FlashResult result)
{
  switch (result) {
    case FlashResult::Ok:
      return "Flash complete";
    case FlashResult::SdNotReady:
      return "No SD card";
    case FlashResult::FileMissing:
      return "No UF2 selected";
    case FlashResult::InvalidUf2:
      return "Invalid UF2";
    case FlashResult::UsbHostInitFailed:
      return "USB host init failed";
    case FlashResult::NoMassStorageDevice:
      return "Mass Storage timeout";
    case FlashResult::MountFailed:
      return "Mount failed";
    case FlashResult::OpenSourceFailed:
      return "Open source failed";
    case FlashResult::OpenTargetFailed:
      return "Open target failed";
    case FlashResult::WriteFailed:
      return "Write failed";
    case FlashResult::DeviceDisconnectedTooEarly:
      return "Device disconnected";
    case FlashResult::Timeout:
      return "Timeout";
    case FlashResult::Cancelled:
      return "Flash cancelled";
    case FlashResult::UnknownError:
      return "Unknown error";
  }
  return "Unknown error";
}

}  // namespace

bool UsbMscUf2Flasher::begin()
{
  return true;
}

void UsbMscUf2Flasher::end()
{
  cleanup();
}

bool UsbMscUf2Flasher::isRunning() const
{
  return state != State::Idle && state != State::Done && state != State::Error;
}

bool UsbMscUf2Flasher::isMassStorageConnected() const
{
  return current.massStorageConnected;
}

bool UsbMscUf2Flasher::startFlash(const String& sdUf2Path, const String& name)
{
  if (isRunning()) {
    return false;
  }

  cleanup();
  current = FlashProgress{};
  sourcePath = sdUf2Path;
  displayName = name;
  current.message = "USB host init";
  current.result = FlashResult::UnknownError;

  Serial.print("[UF2] selected: ");
  Serial.println(sourcePath);

  if (!SD.exists(sourcePath)) {
    setError(FlashResult::FileMissing, "No UF2 selected");
    return false;
  }

  sourceFile = SD.open(sourcePath, FILE_READ);
  if (!sourceFile) {
    setError(FlashResult::OpenSourceFailed, "Open source failed");
    return false;
  }
  current.totalBytes = sourceFile.size();
  sourceFile.close();
  sourceFile = File();

  if (!installMscHost()) {
    return false;
  }

  state = State::WaitingForDevice;
  stateStartedMs = millis();
  current.message = "Waiting for drive";
  Serial.println("[UF2] waiting for MSC device");
  return true;
}

void UsbMscUf2Flasher::poll()
{
  switch (state) {
    case State::Idle:
    case State::Done:
    case State::Error:
      return;
    case State::Installing:
      return;
    case State::WaitingForDevice:
      if (connectedEvent) {
        connectedEvent = false;
        state = State::Mounting;
        stateStartedMs = millis();
        current.massStorageConnected = true;
        current.message = "Mass Storage ready";
        Serial.println("[UF2] MSC detected");
      } else if (millis() - stateStartedMs > MASS_STORAGE_TIMEOUT_MS) {
        setError(FlashResult::NoMassStorageDevice, "Mass Storage timeout");
      }
      break;
    case State::Mounting:
      if (mountDevice()) {
        state = State::Copying;
        stateStartedMs = millis();
        current.message = "Copying firmware";
        Serial.print("[UF2] copy start: ");
        Serial.println(displayName);
      }
      break;
    case State::Copying:
      if (disconnectedEvent) {
        setError(FlashResult::DeviceDisconnectedTooEarly, "Device disconnected");
        return;
      }
      for (int i = 0; i < 2 && state == State::Copying; ++i) {
        copyChunk();
      }
      break;
    case State::WaitingForReboot:
      if (disconnectedEvent || millis() - stateStartedMs > REBOOT_WAIT_MS) {
        current.done = true;
        current.success = true;
        current.result = FlashResult::Ok;
        current.message = "Flash complete";
        state = State::Done;
        cleanup();
        Serial.println("[UF2] flash complete");
      }
      break;
  }
}

void UsbMscUf2Flasher::cancel()
{
  setError(FlashResult::Cancelled, "Flash cancelled");
}

const FlashProgress& UsbMscUf2Flasher::progress() const
{
  return current;
}

FlashResult UsbMscUf2Flasher::result() const
{
  return current.result;
}

const char* UsbMscUf2Flasher::resultMessage() const
{
  return flashResultText(current.result);
}

void UsbMscUf2Flasher::mscEventCallback(const void* rawEvent, void* arg)
{
  auto* self = static_cast<UsbMscUf2Flasher*>(arg);
  auto* event = static_cast<const msc_host_event_t*>(rawEvent);
  if (self == nullptr || event == nullptr) {
    return;
  }
  if (event->event == 0) {
    self->connectedEvent = true;
    self->deviceAddress = event->device.address;
  } else if (event->event == 1) {
    self->disconnectedEvent = true;
    self->current.massStorageConnected = false;
  }
}

void UsbMscUf2Flasher::setError(FlashResult flashResult, const String& message)
{
  current.result = flashResult;
  current.message = message.length() > 0 ? message : flashResultText(flashResult);
  current.done = true;
  current.success = false;
  Serial.print("[UF2] error: ");
  Serial.println(current.message);
  state = State::Error;
  cleanup();
}

void UsbMscUf2Flasher::cleanup()
{
  if (targetFile != nullptr) {
    fflush(targetFile);
    fclose(targetFile);
    targetFile = nullptr;
  }
  if (sourceFile) {
    sourceFile.close();
  }
  if (vfsMounted && vfsHandle != nullptr) {
    msc_host_vfs_unregister(static_cast<msc_host_vfs_handle_t>(vfsHandle));
  }
  vfsMounted = false;
  vfsHandle = nullptr;

  if (deviceInstalled && deviceHandle != nullptr) {
    msc_host_uninstall_device(static_cast<msc_host_device_handle_t>(deviceHandle));
  }
  deviceInstalled = false;
  deviceHandle = nullptr;

  if (mscInstalled) {
    msc_host_uninstall();
  }
  mscInstalled = false;
  connectedEvent = false;
  disconnectedEvent = false;
  deviceAddress = 0;
}

bool UsbMscUf2Flasher::installMscHost()
{
  msc_host_driver_config_t config = {};
  config.create_backround_task = true;
  config.task_priority = 5;
  config.stack_size = 4096;
  config.core_id = tskNO_AFFINITY;
  config.callback = [](const msc_host_event_t* event, void* arg) {
    UsbMscUf2Flasher::mscEventCallback(event, arg);
  };
  config.callback_arg = this;

  Serial.println("[UF2] installing MSC host");
  esp_err_t err = msc_host_install(&config);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("[UF2] msc_host_install failed: 0x%x\n", err);
    setError(FlashResult::UsbHostInitFailed, "USB host init failed");
    return false;
  }
  mscInstalled = true;
  return true;
}

bool UsbMscUf2Flasher::mountDevice()
{
  msc_host_device_handle_t device = nullptr;
  esp_err_t err = msc_host_install_device(deviceAddress, &device);
  if (err != ESP_OK) {
    Serial.printf("[UF2] msc_host_install_device failed: 0x%x\n", err);
    setError(FlashResult::MountFailed, "Mount failed");
    return false;
  }
  deviceHandle = device;
  deviceInstalled = true;

  esp_vfs_fat_mount_config_t mountConfig = {};
  mountConfig.format_if_mount_failed = false;
  mountConfig.max_files = 2;
  mountConfig.allocation_unit_size = 4096;

  msc_host_vfs_handle_t vfs = nullptr;
  err = msc_host_vfs_register(device, USB_MOUNT_POINT, &mountConfig, &vfs);
  if (err != ESP_OK) {
    Serial.printf("[UF2] msc_host_vfs_register failed: 0x%x\n", err);
    setError(FlashResult::MountFailed, "Mount failed");
    return false;
  }
  vfsHandle = vfs;
  vfsMounted = true;
  Serial.print("[UF2] mounted at ");
  Serial.println(USB_MOUNT_POINT);

  sourceFile = SD.open(sourcePath, FILE_READ);
  if (!sourceFile) {
    setError(FlashResult::OpenSourceFailed, "Open source failed");
    return false;
  }
  targetFile = fopen(USB_UF2_TARGET, "wb");
  if (targetFile == nullptr) {
    setError(FlashResult::OpenTargetFailed, "Open target failed");
    return false;
  }
  return true;
}

bool UsbMscUf2Flasher::copyChunk()
{
  static uint8_t buffer[COPY_BUFFER_SIZE];
  if (!sourceFile || targetFile == nullptr) {
    setError(FlashResult::UnknownError, "Copy not ready");
    return false;
  }

  int bytesRead = sourceFile.read(buffer, sizeof(buffer));
  if (bytesRead < 0) {
    setError(FlashResult::OpenSourceFailed, "Read source failed");
    return false;
  }
  if (bytesRead == 0) {
    fflush(targetFile);
    fclose(targetFile);
    targetFile = nullptr;
    sourceFile.close();
    if (vfsMounted && vfsHandle != nullptr) {
      msc_host_vfs_unregister(static_cast<msc_host_vfs_handle_t>(vfsHandle));
      vfsMounted = false;
      vfsHandle = nullptr;
      Serial.println("[UF2] VFS unmounted");
    }
    current.copiedBytes = current.totalBytes;
    current.percent = 100;
    current.message = "Firmware copied";
    state = State::WaitingForReboot;
    stateStartedMs = millis();
    Serial.println("[UF2] copy done, waiting for reboot/disconnect");
    return true;
  }

  size_t written = fwrite(buffer, 1, static_cast<size_t>(bytesRead), targetFile);
  if (written != static_cast<size_t>(bytesRead)) {
    setError(FlashResult::WriteFailed, "Write failed");
    return false;
  }
  current.copiedBytes += written;
  updatePercent();
  if (millis() - lastProgressLogMs > 500) {
    lastProgressLogMs = millis();
    Serial.printf("[UF2] copy %u/%u (%d%%)\n", static_cast<unsigned>(current.copiedBytes),
                  static_cast<unsigned>(current.totalBytes), current.percent);
  }
  return true;
}

void UsbMscUf2Flasher::updatePercent()
{
  if (current.totalBytes == 0) {
    current.percent = 0;
    return;
  }
  current.percent = static_cast<int>((current.copiedBytes * 100) / current.totalBytes);
  if (current.percent > 100) {
    current.percent = 100;
  }
}
