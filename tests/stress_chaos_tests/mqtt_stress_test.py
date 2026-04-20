"""
mqtt_stress_test.py  —  stress / chaos тест для embedded MQTT брокера на ESP32

Запуск:
    pip install paho-mqtt
    python mqtt_stress_test.py --host 192.168.1.XX --max-clients 16

Параметры:
    --host          IP адрес ESP32 (обязательно)
    --port          MQTT порт (default 1883)
    --max-clients   значение BROKER_MQTT_MAX_CLIENTS из sdkconfig (default 16)
    --rounds        сколько раз гонять churn-тест (default 5)
    --keepalive     keepalive в секундах (default 10)
    --verbose       подробный лог каждого события
"""

import argparse
import socket
import struct
import threading
import time
import random
import sys
from dataclasses import dataclass, field
from typing import Optional

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("ERROR: нужен paho-mqtt:  pip install paho-mqtt")
    sys.exit(1)

# ─── цвета для терминала ────────────────────────────────────────────────────
RED    = "\033[91m"
GREEN  = "\033[92m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
RESET  = "\033[0m"

def ok(msg):    print(f"  {GREEN}✓{RESET} {msg}")
def fail(msg):  print(f"  {RED}✗ FAIL: {msg}{RESET}")
def info(msg):  print(f"  {CYAN}→{RESET} {msg}")
def warn(msg):  print(f"  {YELLOW}!{RESET} {msg}")

# ─── счётчик результатов ────────────────────────────────────────────────────
@dataclass
class Results:
    passed: int = 0
    failed: int = 0
    errors: list = field(default_factory=list)

    def check(self, cond: bool, description: str):
        if cond:
            self.passed += 1
            ok(description)
        else:
            self.failed += 1
            self.errors.append(description)
            fail(description)

    def summary(self):
        total = self.passed + self.failed
        status = GREEN + "ALL PASSED" + RESET if self.failed == 0 else RED + f"{self.failed} FAILED" + RESET
        print(f"\n{'─'*60}")
        print(f"  Результат: {self.passed}/{total}  {status}")
        if self.errors:
            print(f"  Упавшие:")
            for e in self.errors:
                print(f"    {RED}• {e}{RESET}")
        print(f"{'─'*60}")

R = Results()

# ─── вспомогательные функции ────────────────────────────────────────────────

def make_client(host, port, client_id, keepalive=10, will_topic=None, will_payload=None,
                connect_timeout=3.0, verbose=False):
    """Создать paho клиент и подключить. Вернуть (client, connected_flag)."""
    connected = threading.Event()
    disconnected = threading.Event()
    refused   = threading.Event()

    def on_connect(c, userdata, flags, reason_code, properties=None):
        rc = getattr(reason_code, "value", reason_code)
        if rc == 0:
            connected.set()
        else:
            refused.set()
        if verbose:
            info(f"[{client_id}] on_connect rc={rc}")

    def on_disconnect(c, userdata, disconnect_flags, reason_code, properties=None):
        rc = getattr(reason_code, "value", reason_code)
        disconnected.set()
        if verbose:
            info(f"[{client_id}] disconnected rc={rc}")

    c = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=client_id,
        clean_session=True,
        reconnect_on_failure=False,
    )
    c.on_connect    = on_connect
    c.on_disconnect = on_disconnect
    c._stress_disconnected = disconnected
    if will_topic:
        c.will_set(will_topic, will_payload or "", qos=1, retain=False)

    try:
        c.connect(host, port, keepalive=keepalive)
        c.loop_start()
        if connected.wait(timeout=connect_timeout):
            return c, True
        if refused.wait(timeout=0.1):
            c.loop_stop()
            return c, False
        c.loop_stop()
        return c, False
    except Exception as e:
        if verbose:
            warn(f"[{client_id}] connect exception: {e}")
        return None, False


def raw_connect_tcp(host, port, timeout=3.0):
    """Просто открыть TCP соединение без MQTT handshake."""
    try:
        s = socket.create_connection((host, port), timeout=timeout)
        return s
    except Exception:
        return None


def raw_send_packet(host, port, payload, timeout=3.0):
    s = raw_connect_tcp(host, port, timeout=timeout)
    if not s:
        return False
    try:
        s.sendall(payload)
        return True
    except Exception:
        return False
    finally:
        try:
            s.close()
        except Exception:
            pass


def disconnect_clean(client):
    """Корректный MQTT DISCONNECT."""
    if client:
        disconnected = getattr(client, "_stress_disconnected", None)
        try:
            client.disconnect()
        except Exception:
            pass
        if disconnected is not None:
            disconnected.wait(timeout=1.5)
        try:
            client.loop_stop()
        except Exception:
            pass


def connect_n_clients(host, port, prefix, count, keepalive, verbose, connect_timeout=3.0):
    clients = []
    connected = 0
    for i in range(count):
        cid = f"{prefix}_{i:03d}"
        c, ok_flag = make_client(host, port, cid, keepalive=keepalive,
                                 connect_timeout=connect_timeout, verbose=verbose)
        if ok_flag:
            clients.append(c)
            connected += 1
    return clients, connected


def wait_until_all_slots_available(host, port, max_clients, prefix, keepalive, verbose,
                                   timeout=4.0, connect_timeout=3.0):
    deadline = time.time() + timeout
    last_probe_connected = 0
    while time.time() < deadline:
        probe_clients, probe_connected = connect_n_clients(
            host, port, prefix, max_clients, keepalive, verbose, connect_timeout=connect_timeout
        )
        last_probe_connected = probe_connected
        for c in probe_clients:
            disconnect_clean(c)
        if probe_connected == max_clients:
            return True, probe_connected
        time.sleep(0.25)
    return False, last_probe_connected


def disconnect_raw(client):
    """Грязный разрыв — TCP RST, без MQTT DISCONNECT."""
    if client:
        try:
            sock = getattr(client, "_sock", None)
            if sock:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("HH", 1, 0))
                client._sock_close()
        except Exception:
            pass
        try:
            client.loop_stop()
        except Exception:
            pass


def safe_subscribe(client, topic, qos=0, timeout=2.0):
    if not client:
        return False
    subacked = threading.Event()

    def on_subscribe(c, userdata, mid, reason_codes, properties=None):
        subacked.set()

    prev = getattr(client, "on_subscribe", None)
    client.on_subscribe = on_subscribe
    try:
        res = client.subscribe(topic, qos=qos)
        if isinstance(res, tuple):
            rc = res[0]
        else:
            rc = getattr(res, "rc", mqtt.MQTT_ERR_UNKNOWN)
        if rc != mqtt.MQTT_ERR_SUCCESS:
            return False
        return subacked.wait(timeout=timeout)
    finally:
        client.on_subscribe = prev


def safe_unsubscribe(client, topic, timeout=2.0):
    if not client:
        return False
    unsubacked = threading.Event()

    def on_unsubscribe(c, userdata, mid, reason_codes=None, properties=None):
        unsubacked.set()

    prev = getattr(client, "on_unsubscribe", None)
    client.on_unsubscribe = on_unsubscribe
    try:
        res = client.unsubscribe(topic)
        if isinstance(res, tuple):
            rc = res[0]
        else:
            rc = getattr(res, "rc", mqtt.MQTT_ERR_UNKNOWN)
        if rc != mqtt.MQTT_ERR_SUCCESS:
            return False
        return unsubacked.wait(timeout=timeout)
    finally:
        client.on_unsubscribe = prev


# ════════════════════════════════════════════════════════════════════════════
# ТЕСТ 1 — Slot exhaustion
# Подключить ровно MAX клиентов, убедиться что (MAX+1)-й получает отказ
# ════════════════════════════════════════════════════════════════════════════
def test_slot_exhaustion(host, port, max_clients, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 1 — Slot exhaustion (max={max_clients})")
    print(f"{'─'*60}")

    clients = []
    connected_count = 0

    info(f"Подключаем {max_clients} клиентов...")
    for i in range(max_clients):
        cid = f"stress_slot_{i:03d}"
        c, ok_flag = make_client(host, port, cid, keepalive=60, verbose=verbose)
        if ok_flag:
            clients.append(c)
            connected_count += 1
        else:
            warn(f"Клиент {cid} не подключился (слот {i})")

    R.check(connected_count == max_clients,
            f"Slot exhaustion: все {max_clients} клиентов подключились ({connected_count}/{max_clients})")

    # Попытка подключить лишний
    info("Пробуем подключить лишний клиент (должен быть отклонён)...")
    extra, extra_ok = make_client(host, port, "stress_slot_EXTRA", keepalive=5,
                                  connect_timeout=4.0, verbose=verbose)
    R.check(not extra_ok,
            "Slot exhaustion: (MAX+1)-й клиент отклонён брокером")

    if extra:
        disconnect_clean(extra)

    info("Отключаем всех...")
    for c in clients:
        disconnect_clean(c)
    time.sleep(1.0)


# ════════════════════════════════════════════════════════════════════════════
# ТЕСТ 2 — Rapid churn: подключить/отключить в цикле, следить за утечкой счётчика
# ════════════════════════════════════════════════════════════════════════════
def test_rapid_churn(host, port, max_clients, rounds, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 2 — Rapid churn ({rounds} раундов × {max_clients} клиентов)")
    print(f"{'─'*60}")

    for r in range(rounds):
        info(f"Раунд {r+1}/{rounds}: подключаем {max_clients} клиентов...")
        clients = []
        connected = 0

        for i in range(max_clients):
            cid = f"stress_churn_r{r}_{i:03d}"
            c, ok_flag = make_client(host, port, cid, keepalive=30, verbose=verbose)
            if ok_flag:
                clients.append(c)
                connected += 1

        info(f"  Подключено: {connected}/{max_clients}")

        # Небольшая пауза чтобы все сессии устоялись
        time.sleep(0.5)

        # Отключаем все чисто
        for c in clients:
            disconnect_clean(c)

        # Дать брокеру время освободить слоты
        time.sleep(1.5)

        # После каждого раунда должны снова подключиться все MAX
        all_free, probe_connected = wait_until_all_slots_available(
            host, port, max_clients, f"stress_probe_r{r}", keepalive=30,
            verbose=verbose, timeout=4.0, connect_timeout=4.0
        )

        R.check(all_free,
                f"Churn раунд {r+1}: после отключения все {max_clients} слотов снова доступны "
                f"({probe_connected}/{max_clients})")
        time.sleep(1.5)


# ════════════════════════════════════════════════════════════════════════════
# ТЕСТ 3 — Same client_id reconnect (должен выбить старую сессию)
# ════════════════════════════════════════════════════════════════════════════
def test_same_client_id_reconnect(host, port, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 3 — Same client_id reconnect")
    print(f"{'─'*60}")

    CLIENT_ID = "stress_dup_client"

    info("Подключаем первый экземпляр...")
    c1, ok1 = make_client(host, port, CLIENT_ID, keepalive=60, verbose=verbose)
    if not ok1:
        R.check(False, "Same client_id: первый клиент не подключился (пропуск)")
        return

    time.sleep(0.5)
    info("Подключаем второй экземпляр с тем же client_id...")
    c2, ok2 = make_client(host, port, CLIENT_ID, keepalive=60, verbose=verbose)

    R.check(ok2, "Same client_id: второй клиент подключился успешно")

    # Первый должен отвалиться. Ждём явный disconnect и проверяем, что publish больше не имеет соединения.
    first_disconnected = getattr(c1, "_stress_disconnected", threading.Event()).wait(timeout=3.0)
    time.sleep(0.2)
    first_still_alive = c1.is_connected()
    publish_rc = None
    try:
        publish_rc = c1.publish("stress/test", "ping", qos=0).rc
    except Exception:
        publish_rc = None

    R.check(first_disconnected and (not first_still_alive) and publish_rc == mqtt.MQTT_ERR_NO_CONN,
            "Same client_id: старая сессия выбита после переподключения с тем же ID")

    disconnect_clean(c1)
    disconnect_clean(c2)
    time.sleep(1.0)


# ════════════════════════════════════════════════════════════════════════════
# ТЕСТ 4 — LWT (Last Will) доставляется при ungraceful disconnect
# ════════════════════════════════════════════════════════════════════════════
def test_lwt_on_ungraceful_disconnect(host, port, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 4 — LWT при ungraceful disconnect (TCP RST)")
    print(f"{'─'*60}")

    WILL_TOPIC   = "stress/will/test"
    WILL_PAYLOAD = "gone"
    WILL_CLIENT  = "stress_will_sender"
    OBS_CLIENT   = "stress_will_observer"

    lwt_received = threading.Event()

    # Подписчик на LWT
    obs, ok_obs = make_client(host, port, OBS_CLIENT, keepalive=60, verbose=verbose)
    if not ok_obs:
        R.check(False, "LWT: наблюдатель не подключился (пропуск)")
        return

    def on_message(c, userdata, msg):
        if msg.topic == WILL_TOPIC and msg.payload.decode() == WILL_PAYLOAD:
            lwt_received.set()
            if verbose:
                info(f"[observer] LWT получен: {msg.topic} = {msg.payload.decode()}")

    obs.on_message = on_message
    obs.subscribe(WILL_TOPIC, qos=1)
    time.sleep(0.5)

    # Клиент с LWT
    info("Подключаем клиент с LWT...")
    sender, ok_sender = make_client(
        host, port, WILL_CLIENT, keepalive=10,
        will_topic=WILL_TOPIC, will_payload=WILL_PAYLOAD,
        verbose=verbose
    )
    if not ok_sender:
        R.check(False, "LWT: sender не подключился (пропуск)")
        disconnect_clean(obs)
        return

    time.sleep(0.5)
    info("Делаем грязный разрыв (TCP RST)...")
    disconnect_raw(sender)

    # Ждём LWT — брокер должен его разослать после keepalive * 1.5 = 15 сек
    info("Ждём LWT (до 20 сек, keepalive=10)...")
    got_lwt = lwt_received.wait(timeout=20.0)
    R.check(got_lwt, "LWT: Last Will доставлен после ungraceful disconnect")

    disconnect_clean(obs)
    time.sleep(1.0)


# ════════════════════════════════════════════════════════════════════════════
# ТЕСТ 5 — Concurrent mixed churn: часть отключается чисто, часть грязно
# ════════════════════════════════════════════════════════════════════════════
def test_concurrent_mixed_churn(host, port, max_clients, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 5 — Concurrent mixed churn (clean + raw disconnect)")
    print(f"{'─'*60}")

    half = max_clients // 2
    clients = []
    connected = 0

    info(f"Подключаем {max_clients} клиентов...")
    for i in range(max_clients):
        cid = f"stress_mixed_{i:03d}"
        c, ok_flag = make_client(host, port, cid, keepalive=5, verbose=verbose)
        if ok_flag:
            clients.append(c)
            connected += 1

    info(f"Подключилось: {connected}/{max_clients}")
    time.sleep(0.5)

    # Половина — чистый disconnect, половина — грязный
    info(f"Первые {half} — clean disconnect, остальные — TCP RST...")
    threads = []

    def do_clean(c):
        disconnect_clean(c)

    def do_raw(c):
        disconnect_raw(c)

    for i, c in enumerate(clients):
        fn = do_clean if i < half else do_raw
        t = threading.Thread(target=fn, args=(c,), daemon=True)
        threads.append(t)
        t.start()

    for t in threads:
        t.join(timeout=5.0)

    # Дать брокеру время почистить грязные сессии (keepalive * 1.5 = 45с — долго)
    # Поэтому используем keepalive=5 в этом тесте — ждём 10 сек
    info("Ждём 12 сек чтобы брокер почистил грязные сессии (keepalive=5)...")
    time.sleep(12.0)

    # Теперь все слоты должны быть свободны
    probe_clients = []
    probe_connected = 0
    for i in range(max_clients):
        cid = f"stress_mixed_probe_{i:03d}"
        c, ok_flag = make_client(host, port, cid, keepalive=30,
                                 connect_timeout=4.0, verbose=verbose)
        if ok_flag:
            probe_clients.append(c)
            probe_connected += 1

    R.check(probe_connected == max_clients,
            f"Mixed churn: после mixed disconnect все {max_clients} слотов освободились "
            f"({probe_connected}/{max_clients})")

    for c in probe_clients:
        disconnect_clean(c)
    time.sleep(1.0)


# ════════════════════════════════════════════════════════════════════════════
# ТЕСТ 6 — Publish flood: много публикаций от всех клиентов одновременно
# ════════════════════════════════════════════════════════════════════════════
def test_publish_flood(host, port, max_clients, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 6 — Publish flood ({max_clients} клиентов × 50 сообщений)")
    print(f"{'─'*60}")

    clients = []
    connected = 0

    info(f"Подключаем {max_clients} клиентов...")
    for i in range(max_clients):
        cid = f"stress_flood_{i:03d}"
        c, ok_flag = make_client(host, port, cid, keepalive=60, verbose=verbose)
        if ok_flag:
            clients.append((i, c))
            connected += 1

    if connected < max_clients // 2:
        R.check(False, f"Publish flood: слишком мало клиентов подключилось ({connected})")
        for _, c in clients:
            disconnect_clean(c)
        return

    info(f"Подключилось {connected} клиентов. Флудим...")
    errors = []
    lock = threading.Lock()

    def flood(idx, c):
        for j in range(50):
            try:
                topic = f"stress/flood/{idx}"
                payload = f"msg_{j}_" + "x" * random.randint(10, 200)
                c.publish(topic, payload, qos=0)
            except Exception as e:
                with lock:
                    errors.append(f"client {idx} msg {j}: {e}")

    threads = [threading.Thread(target=flood, args=(i, c), daemon=True)
               for i, c in clients]
    for t in threads:
        t.start()
    for t in threads:
        t.join(timeout=15.0)

    R.check(len(errors) == 0,
            f"Publish flood: {connected * 50} сообщений без ошибок "
            f"({'OK' if not errors else ', '.join(errors[:3])})")

    info("Отключаем всех...")
    for _, c in clients:
        disconnect_clean(c)
    time.sleep(1.5)


# ════════════════════════════════════════════════════════════════════════════
# main
# ════════════════════════════════════════════════════════════════════════════
def pre_test_health_probe(host, port, max_clients, verbose, tag, required_clients=None, keepalive=30):
    if required_clients is None:
        required_clients = max_clients
    required_clients = max(0, min(required_clients, max_clients))
    info(f"[{tag}] pre-check: TCP + room for {required_clients} clients...")
    s = raw_connect_tcp(host, port, timeout=3.0)
    tcp_ok = s is not None
    if s:
        try:
            s.close()
        except Exception:
            pass
    if not tcp_ok:
        R.check(False, f"{tag}: pre-check TCP unavailable")
        return False

    if required_clients == 0:
        R.check(True, f"{tag}: pre-check TCP reachable")
        return True

    safe_tag = "".join(ch if ch.isalnum() else "_" for ch in tag.lower())
    probe_clients, probe_connected = connect_n_clients(
        host, port, f"stress_precheck_{safe_tag}", required_clients, keepalive,
        verbose, connect_timeout=1.0
    )
    for c in probe_clients:
        disconnect_clean(c)

    ok_probe = probe_connected == required_clients
    if ok_probe:
        ok(f"{tag}: pre-check room for {required_clients} clients "
           f"({probe_connected}/{required_clients})")
    else:
        warn(f"{tag}: pre-check room only {probe_connected}/{required_clients}, continuing to actual test")
    return True


def test_subscribe_fanout(host, port, max_clients, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 7 — Subscribe fanout (1 publisher -> N subscribers)")
    print(f"{'─'*60}")

    sub_count = max(2, min(max_clients - 1, 8))
    if not pre_test_health_probe(host, port, max_clients, verbose, "Subscribe fanout",
                                 required_clients=sub_count + 1):
        return

    topic = "stress/fanout/all"
    expected_payloads = [f"fanout_{i}" for i in range(20)]
    expected_set = set(expected_payloads)
    received = {}
    lock = threading.Lock()
    subscribers = []

    info(f"Подключаем {sub_count} подписчиков и 1 publisher...")
    for i in range(sub_count):
        cid = f"stress_fanout_sub_{i:03d}"
        c, ok_flag = make_client(host, port, cid, keepalive=30, verbose=verbose)
        if not ok_flag:
            continue

        def on_message(cli, userdata, msg, idx=i):
            if msg.topic != topic:
                return
            payload = msg.payload.decode(errors="ignore")
            with lock:
                received.setdefault(idx, set()).add(payload)

        c.on_message = on_message
        if safe_subscribe(c, topic, qos=0, timeout=2.0):
            subscribers.append(c)
        else:
            disconnect_clean(c)

    pub, ok_pub = make_client(host, port, "stress_fanout_pub", keepalive=30, verbose=verbose)
    if not ok_pub or len(subscribers) < 2:
        R.check(False, f"Subscribe fanout: недостаточно клиентов (subs={len(subscribers)}, pub={ok_pub})")
        for c in subscribers:
            disconnect_clean(c)
        if pub:
            disconnect_clean(pub)
        return

    time.sleep(0.5)
    info(f"Публикуем {len(expected_payloads)} сообщений...")
    for payload in expected_payloads:
        try:
            pub.publish(topic, payload, qos=0)
        except Exception as e:
            warn(f"fanout publish exception: {e}")
        time.sleep(0.03)

    deadline = time.time() + 4.0
    while time.time() < deadline:
        with lock:
            done = all(received.get(i, set()) == expected_set for i in range(len(subscribers)))
        if done:
            break
        time.sleep(0.05)

    with lock:
        ok_all = all(received.get(i, set()) == expected_set for i in range(len(subscribers)))
    R.check(ok_all,
            f"Subscribe fanout: все {len(subscribers)} подписчиков получили "
            f"{len(expected_payloads)} уникальных сообщений")

    disconnect_clean(pub)
    for c in subscribers:
        disconnect_clean(c)
    time.sleep(1.0)


def test_subscribe_unsubscribe_churn(host, port, max_clients, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 8 — Subscribe/unsubscribe churn")
    print(f"{'─'*60}")

    sub_count = max(2, min(max_clients - 1, 6))
    if not pre_test_health_probe(host, port, max_clients, verbose, "Sub/unsub churn",
                                 required_clients=sub_count + 1):
        return

    rounds = 3
    topic = "stress/subchurn/topic"
    subscribers = []
    lock = threading.Lock()
    stats = {}
    fail_reason = None

    info(f"Подключаем {sub_count} подписчиков и 1 publisher...")
    for i in range(sub_count):
        cid = f"stress_subchurn_sub_{i:03d}"
        c, ok_flag = make_client(host, port, cid, keepalive=30, verbose=verbose)
        if not ok_flag:
            continue

        def on_message(cli, userdata, msg, idx=i):
            payload = msg.payload.decode(errors="ignore")
            with lock:
                stats.setdefault(idx, []).append(payload)

        c.on_message = on_message
        subscribers.append(c)

    pub, ok_pub = make_client(host, port, "stress_subchurn_pub", keepalive=30, verbose=verbose)
    if not ok_pub or len(subscribers) < 2:
        R.check(False, f"Sub/unsub churn: недостаточно клиентов (subs={len(subscribers)}, pub={ok_pub})")
        for c in subscribers:
            disconnect_clean(c)
        if pub:
            disconnect_clean(pub)
        return

    rounds_ok = True
    for r in range(rounds):
        info(f"Раунд {r+1}/{rounds}: subscribe -> publish -> unsubscribe...")
        for idx, c in enumerate(subscribers):
            if not safe_subscribe(c, topic, qos=0, timeout=2.0):
                fail_reason = f"round {r+1}: subscribe failed on sub#{idx}"
                rounds_ok = False
                break
        if not rounds_ok:
            break

        marker = f"sub_on_{r}"
        pub.publish(topic, marker, qos=0)
        deadline = time.time() + 2.0
        while time.time() < deadline:
            with lock:
                got_all = all(marker in stats.get(i, []) for i in range(len(subscribers)))
            if got_all:
                break
            time.sleep(0.05)
        with lock:
            got_all = all(marker in stats.get(i, []) for i in range(len(subscribers)))
        if not got_all:
            with lock:
                missing = [idx for idx in range(len(subscribers)) if marker not in stats.get(idx, [])]
            fail_reason = f"round {r+1}: marker {marker} not delivered to subs {missing}"
            rounds_ok = False
            break

        for idx, c in enumerate(subscribers):
            if not safe_unsubscribe(c, topic, timeout=2.0):
                fail_reason = f"round {r+1}: unsubscribe failed on sub#{idx}"
                rounds_ok = False
                break
        if not rounds_ok:
            break

        silence_marker = f"sub_off_{r}"
        pub.publish(topic, silence_marker, qos=0)
        time.sleep(0.5)
        with lock:
            leaked_subs = [idx for idx in range(len(subscribers)) if silence_marker in stats.get(idx, [])]
            leaked = bool(leaked_subs)
        if leaked:
            fail_reason = f"round {r+1}: post-unsubscribe leak for marker {silence_marker} to subs {leaked_subs}"
            rounds_ok = False
            break

    if fail_reason:
        warn(f"Sub/unsub churn detail: {fail_reason}")
    R.check(rounds_ok,
            f"Sub/unsub churn: {rounds} раунда подписки/отписки без утечек delivery")

    disconnect_clean(pub)
    for c in subscribers:
        disconnect_clean(c)
    time.sleep(1.0)


def test_duplicate_client_id_storm(host, port, max_clients, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 9 — Duplicate client_id storm")
    print(f"{'─'*60}")

    if not pre_test_health_probe(host, port, max_clients, verbose, "Duplicate client_id storm",
                                 required_clients=1):
        return

    client_id = "stress_dup_storm"
    clients = []
    storm_rounds = 8
    all_connected = True
    old_sessions_killed = True
    prev = None
    storm_details = []

    info(f"Делаем {storm_rounds} быстрых подключений с одним и тем же client_id...")
    for i in range(storm_rounds):
        c, ok_flag = make_client(host, port, client_id, keepalive=30, verbose=verbose)
        clients.append(c)
        all_connected &= ok_flag
        storm_details.append(f"gen#{i}: new_ok={ok_flag}")
        if not ok_flag:
            continue
        if prev is not None:
            disconnected = getattr(prev, "_stress_disconnected", threading.Event()).wait(timeout=2.0)
            time.sleep(0.1)
            alive = prev.is_connected()
            try:
                rc = prev.publish("stress/storm", f"old_{i}", qos=0).rc
            except Exception:
                rc = None
            killed = disconnected and (not alive) and rc == mqtt.MQTT_ERR_NO_CONN
            storm_details.append(
                f"gen#{i}: prev_disconnected={disconnected} prev_alive={alive} prev_publish_rc={rc} killed={killed}"
            )
            old_sessions_killed &= killed
        prev = c
        time.sleep(0.15)

    last_alive = bool(prev and prev.is_connected())
    try:
        last_publish_rc = prev.publish("stress/storm", "final", qos=0).rc if prev else None
    except Exception:
        last_publish_rc = None
    last_ok = last_alive and last_publish_rc == mqtt.MQTT_ERR_SUCCESS

    storm_details.append(f"final: last_alive={last_alive} last_publish_rc={last_publish_rc} last_ok={last_ok}")
    if verbose or not (all_connected and old_sessions_killed and last_ok):
        for line in storm_details:
            info(f"[dup-storm] {line}")
    R.check(all_connected and old_sessions_killed and last_ok,
            "Duplicate client_id storm: после серии reconnect жива только последняя сессия")

    for c in clients:
        disconnect_clean(c)

    all_free, probe_connected = wait_until_all_slots_available(
        host, port, max_clients, "stress_dup_storm_probe", keepalive=30,
        verbose=verbose, timeout=4.0, connect_timeout=4.0
    )
    R.check(all_free,
            f"Duplicate client_id storm: после cleanup все {max_clients} слотов доступны "
            f"({probe_connected}/{max_clients})")
    time.sleep(1.0)


def test_malformed_raw_packet_smoke(host, port, max_clients, verbose):
    print(f"\n{'─'*60}")
    print(f"  ТЕСТ 10 — Malformed raw packet smoke")
    print(f"{'─'*60}")

    if not pre_test_health_probe(host, port, max_clients, verbose, "Malformed raw packet",
                                 required_clients=0):
        return

    packets = [
        b"\x10",
        b"\xff\x00",
        b"\x10\x7f" + b"\x00" * 8,
        b"\x30\xff\xff\xff\x7f",
    ]

    info(f"Шлём {len(packets)} битых TCP payload'ов...")
    sent = 0
    for payload in packets:
        if raw_send_packet(host, port, payload, timeout=3.0):
            sent += 1
        time.sleep(0.1)

    tcp_alive = raw_connect_tcp(host, port, timeout=3.0)
    board_alive = tcp_alive is not None
    if tcp_alive:
        tcp_alive.close()

    all_free, probe_connected = wait_until_all_slots_available(
        host, port, max_clients, "stress_malformed_probe", keepalive=30,
        verbose=verbose, timeout=4.0, connect_timeout=4.0
    )
    R.check(sent == len(packets) and board_alive and all_free,
            f"Malformed raw packet: брокер жив и все {max_clients} слотов доступны "
            f"после {sent}/{len(packets)} битых пакетов ({probe_connected}/{max_clients})")
    time.sleep(1.0)


def parse_test_selection(spec: Optional[str]):
    if not spec:
        return None
    selected = set()
    for part in spec.split(","):
        item = part.strip()
        if not item:
            continue
        if "-" in item:
            left, right = item.split("-", 1)
            start = int(left.strip())
            end = int(right.strip())
            if start > end:
                start, end = end, start
            selected.update(range(start, end + 1))
        else:
            selected.add(int(item))
    return selected


def main():
    parser = argparse.ArgumentParser(description="ESP32 MQTT broker stress test")
    parser.add_argument("--host",        required=True,       help="IP адрес ESP32")
    parser.add_argument("--port",        type=int, default=1883)
    parser.add_argument("--max-clients", type=int, default=16, dest="max_clients",
                        help="BROKER_MQTT_MAX_CLIENTS из sdkconfig")
    parser.add_argument("--rounds",      type=int, default=5,
                        help="Кол-во раундов churn-теста")
    parser.add_argument("--skip-lwt",   action="store_true", dest="skip_lwt",
                        help="Пропустить тест LWT (он долгий — 20 сек)")
    parser.add_argument("--tests",      default=None,
                        help="Run only selected tests, e.g. 7-10 or 1,3,5")
    parser.add_argument("--verbose",     action="store_true")
    args = parser.parse_args()

    print(f"\n{'═'*60}")
    print(f"  ESP32 MQTT Stress Test")
    print(f"  host={args.host}:{args.port}  max_clients={args.max_clients}")
    print(f"{'═'*60}")

    # Проверить что борда вообще отвечает
    info("Проверяем TCP доступность борды...")
    s = raw_connect_tcp(args.host, args.port)
    if not s:
        print(f"\n{RED}FATAL: не удалось подключиться к {args.host}:{args.port}{RESET}")
        print("Проверь IP адрес и что MQTT брокер запущен на борде.")
        sys.exit(1)
    s.close()
    ok(f"Борда отвечает на {args.host}:{args.port}")

    selected_tests = parse_test_selection(args.tests)
    if selected_tests is not None:
        tests = {
            1: lambda: test_slot_exhaustion(args.host, args.port, args.max_clients, args.verbose),
            2: lambda: test_rapid_churn(args.host, args.port, args.max_clients, args.rounds, args.verbose),
            3: lambda: test_same_client_id_reconnect(args.host, args.port, args.verbose),
            4: lambda: test_lwt_on_ungraceful_disconnect(args.host, args.port, args.verbose),
            5: lambda: test_concurrent_mixed_churn(args.host, args.port, args.max_clients, args.verbose),
            6: lambda: test_publish_flood(args.host, args.port, args.max_clients, args.verbose),
            7: lambda: test_subscribe_fanout(args.host, args.port, args.max_clients, args.verbose),
            8: lambda: test_subscribe_unsubscribe_churn(args.host, args.port, args.max_clients, args.verbose),
            9: lambda: test_duplicate_client_id_storm(args.host, args.port, args.max_clients, args.verbose),
            10: lambda: test_malformed_raw_packet_smoke(args.host, args.port, args.max_clients, args.verbose),
        }
        unknown = sorted(n for n in selected_tests if n not in tests)
        if unknown:
            print(f"{RED}FATAL: unknown test numbers: {unknown}{RESET}")
            sys.exit(2)
        info(f"Запускаем только тесты: {', '.join(str(n) for n in sorted(selected_tests))}")
        for test_num in sorted(selected_tests):
            if test_num == 4 and args.skip_lwt:
                warn("LWT С‚РµСЃС‚ РїСЂРѕРїСѓС‰РµРЅ (--skip-lwt)")
                continue
            tests[test_num]()
        R.summary()
        sys.exit(0 if R.failed == 0 else 1)

    test_slot_exhaustion(args.host, args.port, args.max_clients, args.verbose)
    test_rapid_churn(args.host, args.port, args.max_clients, args.rounds, args.verbose)
    test_same_client_id_reconnect(args.host, args.port, args.verbose)

    if not args.skip_lwt:
        test_lwt_on_ungraceful_disconnect(args.host, args.port, args.verbose)
    else:
        warn("LWT тест пропущен (--skip-lwt)")

    # Тест 5 использует keepalive=5, поэтому пересоздаём клиентов с коротким keepalive
    test_concurrent_mixed_churn(args.host, args.port, args.max_clients, args.verbose)
    test_publish_flood(args.host, args.port, args.max_clients, args.verbose)
    test_subscribe_fanout(args.host, args.port, args.max_clients, args.verbose)
    test_subscribe_unsubscribe_churn(args.host, args.port, args.max_clients, args.verbose)
    test_duplicate_client_id_storm(args.host, args.port, args.max_clients, args.verbose)
    test_malformed_raw_packet_smoke(args.host, args.port, args.max_clients, args.verbose)

    R.summary()
    sys.exit(0 if R.failed == 0 else 1)


if __name__ == "__main__":
    main()
