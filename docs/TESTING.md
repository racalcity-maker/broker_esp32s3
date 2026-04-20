# Testing

## Scope

Current automated coverage in `v.1.02` is centered on the `device_manager` test app:

- config parse/model behavior
- config export behavior
- pure runtime logic for:
  - `dm_runtime_sequence`
  - `dm_runtime_signal`

This is the fast, high-value layer: no web UI, no MQTT broker, no SD card, no hardware peripherals required.

A second integration app now exists for `template_runtime` wiring tests:

- `v.1.02/tests/template_runtime_integration`

There is also a broker-focused MQTT test app and an external protocol semantics script:

- `v.1.02/tests/mqtt_core`
- `v.1.02/tests/stress_chaos_tests/mqtt_protocol_semantics_test.py`

## Test App

Path:

- `v.1.02/tests/device_manager`
- `v.1.02/tests/template_runtime_integration`
- `v.1.02/tests/mqtt_core`
- `v.1.02/tests/stress_chaos_tests`

Main files:

- `v.1.02/tests/device_manager/main/test_runner.c`
- `v.1.02/tests/device_manager/main/test_device_manager_parse.c`
- `v.1.02/tests/device_manager/main/test_runtime_pure.c`
- `v.1.02/tests/template_runtime_integration/main/test_runner.c`
- `v.1.02/tests/template_runtime_integration/main/test_template_runtime_integration.c`
- `v.1.02/tests/template_runtime_integration/main/test_template_helpers.h`
- `v.1.02/tests/mqtt_core/main/test_runner.c`
- `v.1.02/components/mqtt_core/test/test_mqtt_core.c`
- `v.1.02/tests/stress_chaos_tests/mqtt_protocol_semantics_test.py`

## Run

From PowerShell:

```powershell
cd C:\Users\test\Documents\Arduino\zal\brocker\v.1.02\tests\device_manager
idf.py build
idf.py flash monitor
```

For the `template_runtime` integration scaffold:

```powershell
cd C:\Users\test\Documents\Arduino\zal\brocker\v.1.02\tests\template_runtime_integration
idf.py build
idf.py flash monitor
```

For local `mqtt_core` tests:

```powershell
cd C:\Users\test\Documents\Arduino\zal\brocker\v.1.02\tests\mqtt_core
idf.py build
idf.py flash monitor
```

For the external MQTT semantics script:

```powershell
cd C:\Users\test\Documents\Arduino\zal\brocker\v.1.02\tests\stress_chaos_tests
python mqtt_protocol_semantics_test.py --host 192.168.43.203 --port 1883
```

If the managed components cache gets dirty:

```powershell
Remove-Item "C:\Users\test\Documents\Arduino\zal\brocker\v.1.02\tests\device_manager\managed_components\espressif__mdns\.component_hash"
idf.py reconfigure
idf.py build
```

If the build cache becomes unstable:

```powershell
idf.py fullclean
idf.py build
```

## Current Coverage

### Parse / Model

- `test_uid_validator_parses_actions_and_background_audio`
- `test_sequence_lock_parses_steps_and_outcomes`
- `test_device_name_falls_back_to_display_name`
- `test_long_audio_track_path_survives_sequence_parse`
- `test_signal_hold_without_signal_topic_is_ignored`
- `test_sequence_lock_without_steps_is_ignored`
- `test_export_omits_legacy_name_and_topics`

What this covers:

- legacy `name -> display_name` fallback
- long audio track path parsing
- invalid template rejection
- export hygiene:
  - no legacy `name`
  - no removed `topics`

### Pure Runtime: Sequence

- `test_sequence_runtime_ignores_unrelated_topics`
- `test_sequence_runtime_fails_on_wrong_step_when_strict`
- `test_sequence_runtime_timeout_is_reported_explicitly`
- `test_sequence_runtime_completes_on_happy_path`
- `test_sequence_runtime_wrong_step_is_ignored_when_not_strict`
- `test_sequence_runtime_wrong_payload_is_ignored`
- `test_sequence_runtime_timeout_before_first_step_is_noop`
- `test_sequence_runtime_reset_clears_progress`
- `test_sequence_runtime_non_required_payload_matches_topic_only`

What this covers:

- strict vs non-strict sequence behavior
- timeout behavior
- reset behavior
- happy path completion
- topic-only matching when payload is optional

### Pure Runtime: Signal Hold

- `test_signal_runtime_starts_and_accumulates_progress`
- `test_signal_runtime_completes_and_emits_finish_actions`
- `test_signal_runtime_timeout_before_start_is_noop`
- `test_signal_runtime_timeout_stops_active_hold`
- `test_signal_runtime_tick_with_delta_above_timeout_stops`
- `test_signal_runtime_timeout_after_completion_is_noop`
- `test_signal_runtime_completes_exactly_on_boundary`
- `test_signal_runtime_tick_after_completion_is_noop`
- `test_signal_runtime_set_template_resets_state`

What this covers:

- start/progress accumulation
- completion path
- timeout path
- boundary behavior
- template reset semantics

### Template Runtime Integration: Sequence Lock

- `test_template_runtime_init_reset_smoke`
- `test_template_runtime_missing_snapshots_return_not_found`
- `test_template_runtime_missing_manual_resets_return_not_found`
- `test_sequence_runtime_snapshot_and_manual_reset`
- `test_sequence_runtime_completion_snapshot`
- `test_sequence_runtime_timeout_snapshot`
- `test_sequence_runtime_event_bus_mqtt_routing_updates_snapshot`

What this covers:

- parse + register real `sequence_lock` template into `dm_template_runtime`
- live snapshot state after first step
- manual reset API
- completion snapshot state
- timer-driven timeout snapshot state
- `event_bus` routing for `EVENT_MQTT_MESSAGE`

### Template Runtime Integration: UID Validator

- `test_uid_runtime_snapshot_tracks_last_values`
- `test_uid_start_topic_resets_snapshot_values`
- `test_uid_runtime_event_bus_routing_updates_snapshot`

What this covers:

- parse + register real `uid_validator` template into `dm_template_runtime`
- live snapshot state after reader MQTT values
- start-topic reset behavior
- `event_bus` routing for reader/start MQTT messages

### Template Runtime Integration: Flag Trigger

- `test_flag_trigger_direct_handle_posts_scenario_events`
- `test_flag_trigger_event_bus_routing_posts_scenario_event`

What this covers:

- parse + register real `on_flag` template into `dm_template_runtime`
- direct `dm_template_runtime_handle_flag(...)` integration path
- `EVENT_FLAG_CHANGED` routing through `event_bus`
- scenario trigger emission on state transitions
- no duplicate trigger when the flag state does not change

### Template Runtime Integration: If Condition

- `test_condition_direct_handle_posts_false_then_true_scenarios`
- `test_condition_event_bus_routing_posts_true_scenario`

What this covers:

- parse + register real `if_condition` template into `dm_template_runtime`
- direct `dm_template_runtime_handle_flag(...)` integration path
- `EVENT_FLAG_CHANGED` routing through `event_bus`
- false -> true -> false scenario transitions for `mode=all`
- no duplicate trigger when the condition result does not change

### Template Runtime Integration: Interval Task

- `test_interval_runtime_posts_periodic_scenario_events`

What this covers:

- parse + register real `interval_task` template into `dm_template_runtime`
- periodic `esp_timer` callback path
- scenario trigger emission through `EVENT_SCENARIO_TRIGGER`
- repeated trigger behavior over multiple intervals

### Template Runtime Integration: Signal Hold

- `test_signal_runtime_snapshot_and_manual_reset`
- `test_signal_runtime_completion_snapshot`
- `test_signal_runtime_timeout_snapshot`
- `test_signal_runtime_event_bus_routing_updates_snapshot`

What this covers:

- parse + register real `signal_hold` template into `dm_template_runtime`
- live snapshot state during heartbeat accumulation
- manual reset API
- completion snapshot state
- timer-driven timeout -> paused snapshot state
- `event_bus` routing for heartbeat/reset MQTT messages

### MQTT Core Tests

Local `mqtt_core` unit tests cover:

- event-topic mapping
- generic and typed injected MQTT dispatch
- stress injection path
- parallel burst handling
- wildcard matcher regression checks
- retained-clear regression checks

External MQTT protocol semantics script covers broker behavior from a real client point of view:

- retained overwrite and retained clear semantics
- wildcard routing for `#`, `+`, and exact subscriptions
- max subscriptions per client
- random protocol soak with post-cleanup slot verification

## Latest Known Result

Latest confirmed green run:

- Date: `2026-04-20`
- Result: `25 Tests 0 Failures 0 Ignored`

Latest explicitly confirmed green run for `template_runtime_integration`:

- Date: `2026-04-20`
- Result: `14 Tests 0 Failures 0 Ignored`

Latest confirmed external MQTT semantics result:

- Date: `2026-04-20`
- Result: full pass after fixes for:
  - retained clear semantics
  - wildcard `+`
  - wildcard `#`

## Important Notes

- The test runner is executed in a dedicated FreeRTOS task, not directly in `app_main()`.
- `tests/device_manager/sdkconfig` increases main task stack and disables task watchdog for this test app.
- `device_manager_export.c` now falls back to internal RAM if SPIRAM allocation for exported JSON is unavailable. This was found by the export test.
- `mqtt_core` fixes validated by tests:
  - retained publish with empty payload now deletes retained entry
  - `topic_matches_filter()` now handles MQTT wildcard semantics correctly for `#` and `+`

## Not Covered Yet

Still missing or intentionally separate:

- deeper `template_runtime` integration tests beyond `uid_validator`, `on_flag`, `if_condition`, `interval_task`, `sequence_lock`, and `signal_hold`
- web API handler tests
- OTA flow tests
- hardware-dependent tests:
  - SD card
  - audio playback on real board
  - network/Wi-Fi behavior
  - full UI/operator flow

## Recommended Next Step

Keep both layers in sync:

- internal component tests in `tests/device_manager`, `tests/template_runtime_integration`, and `tests/mqtt_core`
- external broker behavior checks in `tests/stress_chaos_tests`
