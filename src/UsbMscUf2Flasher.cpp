#include "UsbMscUf2Flasher.h"
#include "Uf2Validator.h"

#include <SD.h>
#include <cstdio>
#include <cstring>
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "esp_private/msc_scsi_bot.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"

namespace {

constexpr const char* USB_MOUNT_POINT = "/usb";
constexpr const char* USB_UF2_TARGET = "/usb/FIRMWARE.UF2";
constexpr unsigned long MASS_STORAGE_TIMEOUT_MS = 30000;
constexpr unsigned long REBOOT_WAIT_MS = 12000;
constexpr size_t UF2_BLOCK_SIZE = 512;
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
    case FlashResult::WrongTargetDevice:
      return "Wrong BOOTSEL device";
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

bool UsbMscUf2Flasher::startFlash(const String& sdUf2Path, const String& name, Uf2Target target)
{
  if (isRunning()) {
    return false;
  }

  cleanup();
  current = FlashProgress{};
  sourcePath = sdUf2Path;
  displayName = name;
  expectedTarget = target;
  directSectorWrite = target == Uf2Target::Rp2350;
  current.message = "USB host init";
  current.result = FlashResult::UnknownError;

  Serial.print("[UF2] selected: ");
  Serial.println(sourcePath);
  Serial.print("[UF2] write mode: ");
  Serial.println(directSectorWrite ? "direct-sector" : "vfs-file");

  if (!SD.exists(sourcePath)) {
    setError(FlashResult::FileMissing, "No UF2 selected");
    return false;
  }

  const Uf2ValidationInfo validation = Uf2Validator::validate(sourcePath, expectedTarget);
  if (validation.result != Uf2ValidationResult::Ok) {
    setError(FlashResult::InvalidUf2, Uf2Validator::message(validation.result));
    return false;
  }

  sourceFile = SD.open(sourcePath, FILE_READ);
  if (!sourceFile) {
    setError(FlashResult::OpenSourceFailed, "Open source failed");
    return false;
  }
  current.totalBytes = sourceFile.size();
  if (current.totalBytes != validation.fileSize) {
    setError(FlashResult::InvalidUf2, "UF2 changed during validation");
    return false;
  }
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
      if (prepareDevice()) {
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
      if (disconnectedEvent) {
        current.done = true;
        current.success = true;
        current.result = FlashResult::Ok;
        current.message = "Flash complete";
        state = State::Done;
        cleanup();
        Serial.println("[UF2] flash complete");
      } else if (millis() - stateStartedMs > REBOOT_WAIT_MS) {
        setError(FlashResult::Timeout, "Reboot not detected");
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
  } else if (event->event == 1 && self->deviceHandle == event->device.handle) {
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
  directSectorWrite = false;
  targetSectorSize = 0;
  targetSectorCount = 0;
  nextWriteSector = 0;
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

bool UsbMscUf2Flasher::prepareDevice()
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

  msc_host_device_info_t info = {};
  err = msc_host_get_device_info(device, &info);
  if (err != ESP_OK) {
    Serial.printf("[UF2] msc_host_get_device_info failed: 0x%x\n", err);
    setError(FlashResult::MountFailed, "Device info failed");
    return false;
  }
  Serial.printf("[UF2] MSC VID=%04x PID=%04x sectors=%u sector_size=%u\n", info.idVendor, info.idProduct,
                static_cast<unsigned>(info.sector_count), static_cast<unsigned>(info.sector_size));
  if (!uf2BootselDeviceMatchesTarget(info.idVendor, info.idProduct, expectedTarget)) {
    setError(FlashResult::WrongTargetDevice, "Wrong BOOTSEL device");
    return false;
  }

  if (!directSectorWrite) {
    return mountVfsDevice(device);
  }
  return prepareDirectDevice(device, info);
}

bool UsbMscUf2Flasher::prepareDirectDevice(msc_host_device_handle_t device, const msc_host_device_info_t& info)
{
  targetSectorSize = info.sector_size;
  targetSectorCount = info.sector_count;
  nextWriteSector = 0;
  Serial.printf("[UF2] MSC ready sectors=%u sector_size=%u\n", static_cast<unsigned>(targetSectorCount),
                static_cast<unsigned>(targetSectorSize));
  if (targetSectorSize != UF2_BLOCK_SIZE || targetSectorCount == 0) {
    setError(FlashResult::MountFailed, "Unsupported MSC sector size");
    return false;
  }

  sourceFile = SD.open(sourcePath, FILE_READ);
  if (!sourceFile) {
    setError(FlashResult::OpenSourceFailed, "Open source failed");
    return false;
  }
  return true;
}

bool UsbMscUf2Flasher::mountVfsDevice(msc_host_device_handle_t device)
{
  esp_vfs_fat_mount_config_t mountConfig = {};
  mountConfig.format_if_mount_failed = false;
  mountConfig.max_files = 2;
  mountConfig.allocation_unit_size = 4096;

  msc_host_vfs_handle_t vfs = nullptr;
  esp_err_t err = msc_host_vfs_register(device, USB_MOUNT_POINT, &mountConfig, &vfs);
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
  if (!sourceFile || (directSectorWrite && deviceHandle == nullptr) || (!directSectorWrite && targetFile == nullptr)) {
    setError(FlashResult::UnknownError, "Copy not ready");
    return false;
  }

  const size_t readSize = directSectorWrite ? UF2_BLOCK_SIZE : COPY_BUFFER_SIZE;
  int bytesRead = sourceFile.read(buffer, readSize);
  if (bytesRead < 0) {
    setError(FlashResult::OpenSourceFailed, "Read source failed");
    return false;
  }
  if (bytesRead == 0) {
    if (current.copiedBytes != current.totalBytes) {
      setError(FlashResult::OpenSourceFailed, "Read source failed");
      return false;
    }
    return finishCopy();
  }
  if (directSectorWrite && bytesRead != static_cast<int>(UF2_BLOCK_SIZE)) {
    setError(FlashResult::InvalidUf2, "Invalid UF2 block");
    return false;
  }

  if (directSectorWrite) {
    esp_err_t err = scsi_cmd_write10(static_cast<msc_host_device_handle_t>(deviceHandle), buffer, nextWriteSector, 1,
                                     targetSectorSize);
    if (err != ESP_OK) {
      Serial.printf("[UF2] sector write failed sector=%u err=0x%x\n", static_cast<unsigned>(nextWriteSector), err);
      setError(FlashResult::WriteFailed, "Write failed");
      return false;
    }
    nextWriteSector = (nextWriteSector + 1) % targetSectorCount;
  } else {
    size_t written = fwrite(buffer, 1, static_cast<size_t>(bytesRead), targetFile);
    if (written != static_cast<size_t>(bytesRead)) {
      setError(FlashResult::WriteFailed, "Write failed");
      return false;
    }
  }
  current.copiedBytes += static_cast<size_t>(bytesRead);
  updatePercent();
  if (current.totalBytes > 0 && current.copiedBytes >= current.totalBytes) {
    return finishCopy();
  }
  if (millis() - lastProgressLogMs > 500) {
    lastProgressLogMs = millis();
    Serial.printf("[UF2] copy %u/%u (%d%%)\n", static_cast<unsigned>(current.copiedBytes),
                  static_cast<unsigned>(current.totalBytes), current.percent);
  }
  return true;
}

bool UsbMscUf2Flasher::finishCopy()
{
  if (targetFile != nullptr) {
    const int flushResult = fflush(targetFile);
    const int closeResult = fclose(targetFile);
    targetFile = nullptr;
    if (flushResult != 0 || closeResult != 0) {
      setError(FlashResult::WriteFailed, "Flush failed");
      return false;
    }
  }
  if (sourceFile) {
    sourceFile.close();
  }
  if (vfsMounted && vfsHandle != nullptr) {
    const esp_err_t err = msc_host_vfs_unregister(static_cast<msc_host_vfs_handle_t>(vfsHandle));
    if (err != ESP_OK) {
      Serial.printf("[UF2] VFS unmount failed: 0x%x\n", err);
      setError(FlashResult::WriteFailed, "Unmount failed");
      return false;
    }
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
