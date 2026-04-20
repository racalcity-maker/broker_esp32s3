## MQTT Stress / Chaos Tests

Установка зависимости:

```powershell
pip install paho-mqtt
```

Полный прогон всех 10 тестов:

```powershell
python .\mqtt_stress_test.py --host 192.168.1.XX --max-clients 16 --rounds 5
```

Выборочный прогон по номерам тестов:

```powershell
python .\mqtt_stress_test.py --host 192.168.1.XX --max-clients 16 --tests 7-10
python .\mqtt_stress_test.py --host 192.168.1.XX --max-clients 16 --tests 8,9 --verbose
```

Текущий набор покрывает:

- `1` Slot exhaustion
- `2` Rapid churn
- `3` Same client_id reconnect
- `4` LWT on ungraceful disconnect
- `5` Concurrent mixed churn
- `6` Publish flood
- `7` Subscribe fanout
- `8` Subscribe/unsubscribe churn
- `9` Duplicate client_id storm
- `10` Malformed raw packet smoke

## MQTT Protocol / Semantics Tests

Отдельный набор для retained, wildcard, subscription limits и random soak:

```powershell
python .\mqtt_protocol_semantics_test.py --host 192.168.1.XX --max-clients 16 --max-subs 8 --duration 60
```

Выборочный запуск:

```powershell
python .\mqtt_protocol_semantics_test.py --host 192.168.1.XX --tests 1-3
python .\mqtt_protocol_semantics_test.py --host 192.168.1.XX --tests 4 --duration 300 --verbose
```
