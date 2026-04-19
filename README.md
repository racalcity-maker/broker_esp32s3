# Broker Firmware

Firmware for the `Broker` controller: an ESP32-S3 based automation hub with:

- built-in MQTT broker
- Web UI with authentication
- profile-based device configuration
- template runtime
- scenario automation engine
- audio playback
- OTA firmware update

The firmware is designed for stand-alone escape room and interactive exhibit setups where the ESP32 acts as both the control plane and the local integration point for field devices.

## Main Capabilities

- Embedded MQTT broker for local devices over Wi-Fi
- Web UI for status, settings, devices, audio and firmware update
- Device profiles stored on SD card
- Template-driven runtime logic for common puzzle patterns
- Scenario engine with MQTT, audio, flags, waits, delays and loops
- OTA update flow with rollback-aware status
- Local status LED and fault monitoring

## Current Architecture

The codebase is now split into clear modules:

- `device_model` - shared device and template types
- `device_manager` - config, profiles, storage, import/export
- `device_runtime` - runtime state and template execution
- `automation_engine` - scenario orchestration
- `audio_player` - playback service
- `sd_storage` - SD card ownership
- `web_ui` - HTTP/API layer
- `mqtt_core` - MQTT broker
- `event_bus` - internal module-to-module signaling

Detailed architecture notes are in:

- `docs/ARCHITECTURE.md`

## Requirements

- ESP32-S3
- PSRAM enabled
- SD card connected to configured SPI pins
- I2S audio output connected if audio playback is used
- ESP-IDF 5.3.x

## Build

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

## Important Runtime Storage

### NVS

Stored in `config_store`:

- Wi-Fi config
- MQTT config and credentials
- Web credentials
- time/NTP config
- logging flags

### SD Card

Stored through `sd_storage` and used by higher-level services:

- active config backup
- profile binaries in `/.dm_profiles`
- audio files

## Web UI

The Web UI is served directly by the firmware.

Main functional areas:

- Status
- Audio
- Devices
- Settings
- Update

Authentication:

- cookie-based session auth
- admin account
- optional operator/user account
- credential reset supported by hardware reset flow

## OTA Update

OTA is supported through the Web UI.

Flow:

1. open the firmware update page
2. upload the built firmware binary
3. device writes image into OTA partition
4. device reports `phase=reboot_required`
5. operator explicitly requests reboot
6. device boots into new image
7. healthy boot confirms the update

The OTA status view shows:

- running partition
- boot partition
- current app version
- transfer progress
- lifecycle phase
- rollback-related state

Main OTA phases exposed by the API:

- `idle`
- `uploading`
- `reboot_required`
- `rebooting`
- `verify_wait_ready`
- `verify_pending`

## Device Configuration Model

The system keeps one active profile in RAM and persists profiles on SD.

Typical flow:

1. load active config from SD
2. rebuild template runtime
3. serve/edit config via Web UI
4. save updated config back to SD
5. rebuild runtime again after apply

This keeps inactive profiles off RAM while allowing live switching.

## Template and Scenario System

### Templates

The current template set includes:

- `uid_validator`
- `signal_hold`
- `on_mqtt_event`
- `on_flag`
- `if_condition`
- `interval_task`
- `sequence_lock`

Templates are configuration-driven. Their runtime state lives in `device_runtime`.

### Scenarios

Scenarios are executed by `automation_engine`.

Supported step types include:

- `mqtt_publish`
- `audio_play`
- `audio_stop`
- `set_flag`
- `wait_flags`
- `delay`
- `loop`
- `event_bus`
- `nop`

## Audio

Audio is handled by `audio_player`.

Responsibilities:

- playback control
- decode/read pipeline
- I2S output
- playback status
- volume persistence

The SD card is not owned by audio anymore. Audio uses `sd_storage` like any other consumer.

## MQTT

`mqtt_core` implements the local broker.

Responsibilities:

- client sessions
- publish path
- injected local MQTT messages
- event mapping to internal bus
- stats for UI/status

The broker is intended for local embedded devices and puzzle hardware, not as a general-purpose internet-facing broker.

## Status and Fault Monitoring

`error_monitor` drives the status LED and aggregates health signals.

Examples:

- Wi-Fi disconnected
- SD missing or mount failure
- audio fault indication

SD card state now reaches `error_monitor` through `event_bus`, not through direct calls from storage.

## Project Layout

```text
components/
  audio_player/
  automation_engine/
  config_store/
  device_manager/
  device_model/
  device_runtime/
  error_monitor/
  event_bus/
  mqtt_core/
  network/
  ota_manager/
  sd_storage/
  status_led/
  web_ui/
docs/
  ARCHITECTURE.md
main/
  main.c
```

## Documentation

- `docs/ARCHITECTURE.md` - module boundaries and layered design
- `docs/SCENARIO_SETUP.md` - scenario setup guide
- `docs/SCENARIO_SETUP_RUS.md` - same in Russian
- `docs/TEMPLATE_GUIDE.md` - template behavior guide
- `docs/TEMPLATE_GUIDE_RUS.md` - same in Russian

## Notes for Further Work

Likely next evolutions:

- additional runtime snapshot/read-model cleanup
- finer split inside `device_manager`
- more OTA diagnostics
- more tests around config/profile/runtime transitions
