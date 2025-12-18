# Testing Overview

The project currently ships two ESP-IDF Unity suites. Run them from the repository root:

```bash
cd tests/device_manager
idf.py test -T device_manager
```

## Device Manager Suite (`tests/device_manager`)

Target: `esp32s3`. Covers both JSON parsing and runtime integration.

- **Parsing** (Unity tests in `test_device_manager_parse.c`)
  - `test_sensor_monitor_parses_channels`
  - `test_sensor_monitor_invalid_channel_falls_back_to_default`
  - `test_sensor_monitor_requires_base_json_key_in_json_mode`
  - `test_all_template_types_parse` (UID, signal hold, MQTT trigger, flag trigger, if_condition, interval, sequence, sensor monitor)
- **Integration** (Unity tests in `test_device_manager_integration.c`)
  - `test_sensor_runtime_handles_multi_channel_messages`
  - `test_sensor_runtime_supports_default_channel`
  - `test_sensor_runtime_history_handles_burst_updates`

All tests rely on the ESP-IDF toolchain (`idf.py`) being available in `PATH`.
