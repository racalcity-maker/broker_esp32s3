import argparse
import random
import socket
import struct
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("ERROR: paho-mqtt is required: pip install paho-mqtt")
    sys.exit(1)


RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
RESET = "\033[0m"


def ok(msg):
    print(f"  {GREEN}✓{RESET} {msg}")


def fail(msg):
    print(f"  {RED}✗ FAIL: {msg}{RESET}")


def info(msg):
    print(f"  {CYAN}→{RESET} {msg}")


def warn(msg):
    print(f"  {YELLOW}!{RESET} {msg}")


@dataclass
class Results:
    passed: int = 0
    failed: int = 0
    errors: List[str] = field(default_factory=list)

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
        print(f"\n{'─' * 60}")
        print(f"  Result: {self.passed}/{total}  {status}")
        if self.errors:
            print("  Failed:")
            for e in self.errors:
                print(f"    {RED}• {e}{RESET}")
        print(f"{'─' * 60}")


R = Results()


def raw_connect_tcp(host: str, port: int, timeout: float = 3.0):
    try:
        return socket.create_connection((host, port), timeout=timeout)
    except Exception:
        return None


def raw_disconnect(client):
    if not client:
        return
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


def make_client(host: str, port: int, client_id: str, keepalive: int = 20,
                connect_timeout: float = 3.0, verbose: bool = False):
    connected = threading.Event()
    disconnected = threading.Event()
    refused = threading.Event()

    def on_connect(c, userdata, flags, reason_code, properties=None):
        rc = getattr(reason_code, "value", reason_code)
        if rc == 0:
            connected.set()
        else:
            refused.set()
        if verbose:
            info(f"[{client_id}] connect rc={rc}")

    def on_disconnect(c, userdata, disconnect_flags, reason_code, properties=None):
        rc = getattr(reason_code, "value", reason_code)
        disconnected.set()
        if verbose:
            info(f"[{client_id}] disconnect rc={rc}")

    c = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=client_id,
        clean_session=True,
        reconnect_on_failure=False,
    )
    c.on_connect = on_connect
    c.on_disconnect = on_disconnect
    c._stress_disconnected = disconnected

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


def disconnect_clean(client):
    if not client:
        return
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


def connect_n_clients(host: str, port: int, prefix: str, count: int, keepalive: int,
                      verbose: bool, connect_timeout: float = 2.0):
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


def wait_until_slots_available(host: str, port: int, required_clients: int, prefix: str,
                               keepalive: int, verbose: bool,
                               timeout: float = 4.0, connect_timeout: float = 1.0):
    deadline = time.time() + timeout
    last_connected = 0
    while time.time() < deadline:
        probe_clients, probe_connected = connect_n_clients(
            host, port, prefix, required_clients, keepalive, verbose, connect_timeout=connect_timeout
        )
        last_connected = probe_connected
        for c in probe_clients:
            disconnect_clean(c)
        if probe_connected == required_clients:
            return True, probe_connected
        time.sleep(0.25)
    return False, last_connected


def pre_test_health_probe(host: str, port: int, required_clients: int, verbose: bool, tag: str):
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
    if required_clients <= 0:
        ok(f"{tag}: pre-check TCP reachable")
        return True
    slots_ok, connected = wait_until_slots_available(
        host, port, required_clients, f"proto_pre_{tag.lower().replace(' ', '_')}",
        keepalive=20, verbose=verbose, timeout=4.0, connect_timeout=1.0
    )
    if slots_ok:
        ok(f"{tag}: pre-check room for {required_clients} clients ({connected}/{required_clients})")
    else:
        warn(f"{tag}: pre-check room only {connected}/{required_clients}, continuing to actual test")
    return True


def _normalize_reason_codes(reason_codes) -> List[int]:
    if reason_codes is None:
        return []
    if isinstance(reason_codes, (list, tuple)):
        seq = reason_codes
    else:
        seq = [reason_codes]
    return [int(getattr(rc, "value", rc)) for rc in seq]


def subscribe_and_wait(client, topic: str, qos: int = 0, timeout: float = 2.0) -> Tuple[bool, List[int]]:
    if not client:
        return False, []
    subacked = threading.Event()
    granted_codes: List[int] = []

    def on_subscribe(c, userdata, mid, reason_codes, properties=None):
        granted_codes[:] = _normalize_reason_codes(reason_codes)
        subacked.set()

    prev = getattr(client, "on_subscribe", None)
    client.on_subscribe = on_subscribe
    try:
        result = client.subscribe(topic, qos=qos)
        rc = result[0] if isinstance(result, tuple) else getattr(result, "rc", mqtt.MQTT_ERR_UNKNOWN)
        if rc != mqtt.MQTT_ERR_SUCCESS:
            return False, granted_codes
        return subacked.wait(timeout=timeout), granted_codes
    finally:
        client.on_subscribe = prev


def unsubscribe_and_wait(client, topic: str, timeout: float = 2.0) -> bool:
    if not client:
        return False
    unsubacked = threading.Event()

    def on_unsubscribe(c, userdata, mid, reason_codes=None, properties=None):
        unsubacked.set()

    prev = getattr(client, "on_unsubscribe", None)
    client.on_unsubscribe = on_unsubscribe
    try:
        result = client.unsubscribe(topic)
        rc = result[0] if isinstance(result, tuple) else getattr(result, "rc", mqtt.MQTT_ERR_UNKNOWN)
        if rc != mqtt.MQTT_ERR_SUCCESS:
            return False
        return unsubacked.wait(timeout=timeout)
    finally:
        client.on_unsubscribe = prev


def publish_and_wait(client, topic: str, payload: str, qos: int = 0, retain: bool = False,
                     timeout: float = 2.0) -> bool:
    if not client:
        return False
    try:
        msg = client.publish(topic, payload, qos=qos, retain=retain)
        if hasattr(msg, "wait_for_publish"):
            msg.wait_for_publish(timeout=timeout)
        return msg.rc == mqtt.MQTT_ERR_SUCCESS
    except Exception:
        return False


def test_retained_messages(host: str, port: int, verbose: bool):
    print(f"\n{'─' * 60}")
    print("  TEST 1 — Retained messages")
    print(f"{'─' * 60}")

    if not pre_test_health_probe(host, port, 3, verbose, "Retained"):
        return

    topic = "stress/retain/value"
    publisher, ok_pub = make_client(host, port, "proto_retain_pub", keepalive=20, verbose=verbose)
    if not ok_pub:
        R.check(False, "Retained: publisher failed to connect")
        return

    R.check(publish_and_wait(publisher, topic, "value1", qos=0, retain=True),
            "Retained: initial retained publish succeeded")
    time.sleep(0.3)

    got_first = threading.Event()
    got_second = threading.Event()
    got_cleared = threading.Event()
    first_payload = {"value": None}
    second_payload = {"value": None}

    sub1, ok_sub1 = make_client(host, port, "proto_retain_sub_1", keepalive=20, verbose=verbose)
    if not ok_sub1:
        R.check(False, "Retained: first subscriber failed to connect")
        disconnect_clean(publisher)
        return

    def on_message_first(c, userdata, msg):
        first_payload["value"] = msg.payload.decode(errors="ignore")
        got_first.set()

    sub1.on_message = on_message_first
    sub1.subscribe(topic, qos=0)
    R.check(got_first.wait(timeout=2.0) and first_payload["value"] == "value1",
            "Retained: fresh subscriber received retained value1")
    disconnect_clean(sub1)

    R.check(publish_and_wait(publisher, topic, "value2", qos=0, retain=True),
            "Retained: retained overwrite publish succeeded")
    time.sleep(0.3)

    sub2, ok_sub2 = make_client(host, port, "proto_retain_sub_2", keepalive=20, verbose=verbose)
    if not ok_sub2:
        R.check(False, "Retained: second subscriber failed to connect")
        disconnect_clean(publisher)
        return

    def on_message_second(c, userdata, msg):
        second_payload["value"] = msg.payload.decode(errors="ignore")
        got_second.set()

    sub2.on_message = on_message_second
    sub2.subscribe(topic, qos=0)
    R.check(got_second.wait(timeout=2.0) and second_payload["value"] == "value2",
            "Retained: fresh subscriber received overwritten retained value2")
    disconnect_clean(sub2)

    R.check(publish_and_wait(publisher, topic, "", qos=0, retain=True),
            "Retained: retained clear publish succeeded")
    time.sleep(0.3)

    sub3, ok_sub3 = make_client(host, port, "proto_retain_sub_3", keepalive=20, verbose=verbose)
    if not ok_sub3:
        R.check(False, "Retained: third subscriber failed to connect")
        disconnect_clean(publisher)
        return

    def on_message_third(c, userdata, msg):
        got_cleared.set()

    sub3.on_message = on_message_third
    sub3.subscribe(topic, qos=0)
    R.check(not got_cleared.wait(timeout=1.5),
            "Retained: fresh subscriber received no retained message after clear")

    disconnect_clean(sub3)
    disconnect_clean(publisher)
    time.sleep(0.5)


def test_wildcard_routing(host: str, port: int, verbose: bool):
    print(f"\n{'─' * 60}")
    print("  TEST 2 — Wildcard routing")
    print(f"{'─' * 60}")

    if not pre_test_health_probe(host, port, 4, verbose, "Wildcard"):
        return

    sub_all, ok_all = make_client(host, port, "proto_wc_all", keepalive=20, verbose=verbose)
    sub_plus, ok_plus = make_client(host, port, "proto_wc_plus", keepalive=20, verbose=verbose)
    sub_exact, ok_exact = make_client(host, port, "proto_wc_exact", keepalive=20, verbose=verbose)
    pub, ok_pub = make_client(host, port, "proto_wc_pub", keepalive=20, verbose=verbose)

    if not all([ok_all, ok_plus, ok_exact, ok_pub]):
        R.check(False, f"Wildcard: connect failed all={ok_all} plus={ok_plus} exact={ok_exact} pub={ok_pub}")
        for c in [sub_all, sub_plus, sub_exact, pub]:
            disconnect_clean(c)
        return

    received: Dict[str, List[Tuple[str, str]]] = {"all": [], "plus": [], "exact": []}
    lock = threading.Lock()

    def make_on_message(key: str):
        def _cb(c, userdata, msg):
            with lock:
                received[key].append((msg.topic, msg.payload.decode(errors="ignore")))
        return _cb

    sub_all.on_message = make_on_message("all")
    sub_plus.on_message = make_on_message("plus")
    sub_exact.on_message = make_on_message("exact")

    sub_all.subscribe("stress/#", qos=0)
    sub_plus.subscribe("stress/+/beta", qos=0)
    sub_exact.subscribe("stress/exact/one", qos=0)
    time.sleep(0.3)

    publishes = [
        ("stress/alpha/beta", "m1"),
        ("stress/exact/one", "m2"),
        ("stress/exact/two", "m3"),
    ]
    for topic, payload in publishes:
        publish_and_wait(pub, topic, payload, qos=0, retain=False)
        time.sleep(0.1)
    time.sleep(0.5)

    with lock:
        all_topics = {t for t, _ in received["all"]}
        plus_topics = {t for t, _ in received["plus"]}
        exact_topics = {t for t, _ in received["exact"]}

    R.check(all_topics == {"stress/alpha/beta", "stress/exact/one", "stress/exact/two"},
            "Wildcard: # subscriber received all matching topics")
    R.check(plus_topics == {"stress/alpha/beta"},
            "Wildcard: + subscriber received only single-level beta match")
    R.check(exact_topics == {"stress/exact/one"},
            "Wildcard: exact subscriber received only exact match")

    for c in [sub_all, sub_plus, sub_exact, pub]:
        disconnect_clean(c)
    time.sleep(0.5)


def test_max_subscriptions_per_client(host: str, port: int, max_subs: int, verbose: bool):
    print(f"\n{'─' * 60}")
    print("  TEST 3 — Max subscriptions per client")
    print(f"{'─' * 60}")

    if not pre_test_health_probe(host, port, 2, verbose, "Max subs"):
        return

    client, ok_client = make_client(host, port, "proto_maxsubs_client", keepalive=20, verbose=verbose)
    pub, ok_pub = make_client(host, port, "proto_maxsubs_pub", keepalive=20, verbose=verbose)
    if not (ok_client and ok_pub):
        R.check(False, f"Max subs: connect failed client={ok_client} pub={ok_pub}")
        disconnect_clean(client)
        disconnect_clean(pub)
        return

    granted_ok = True
    denied_extra = False
    reasons_log = []

    for i in range(max_subs):
        topic = f"stress/maxsubs/{i}"
        ok_sub, granted = subscribe_and_wait(client, topic, qos=0, timeout=2.0)
        reasons_log.append((topic, granted))
        if not ok_sub or (granted and granted[0] == 0x80):
            granted_ok = False
            break

    extra_topic = f"stress/maxsubs/{max_subs}"
    ok_extra, granted_extra = subscribe_and_wait(client, extra_topic, qos=0, timeout=2.0)
    reasons_log.append((extra_topic, granted_extra))
    denied_extra = ok_extra and bool(granted_extra) and granted_extra[0] == 0x80

    if verbose or not (granted_ok and denied_extra):
        for topic, granted in reasons_log:
            info(f"[max-subs] {topic} -> granted={granted}")

    R.check(granted_ok, f"Max subs: first {max_subs} subscriptions accepted")
    R.check(denied_extra, f"Max subs: extra subscription #{max_subs + 1} rejected with SUBACK failure")

    # Smoke that the accepted subscriptions still route messages.
    got = threading.Event()
    got_payload = {"value": None}

    def on_message(c, userdata, msg):
        got_payload["value"] = msg.payload.decode(errors="ignore")
        got.set()

    client.on_message = on_message
    publish_and_wait(pub, "stress/maxsubs/0", "hello", qos=0, retain=False)
    R.check(got.wait(timeout=1.5) and got_payload["value"] == "hello",
            "Max subs: accepted subscriptions still receive messages")

    disconnect_clean(client)
    disconnect_clean(pub)
    time.sleep(0.5)


def test_random_protocol_soak(host: str, port: int, max_clients: int, duration: int, verbose: bool):
    print(f"\n{'─' * 60}")
    print(f"  TEST 4 — Random protocol soak ({duration}s)")
    print(f"{'─' * 60}")

    active_limit = max(2, min(max_clients, 6))
    if not pre_test_health_probe(host, port, active_limit, verbose, "Soak"):
        return

    rng = random.Random(1337)
    active: List[Tuple[str, object]] = []
    metrics = {
        "connect_ok": 0,
        "connect_fail": 0,
        "publish_ok": 0,
        "publish_fail": 0,
        "subscribe_ok": 0,
        "subscribe_fail": 0,
        "unsubscribe_ok": 0,
        "unsubscribe_fail": 0,
        "clean_disconnect": 0,
        "raw_disconnect": 0,
        "dup_reconnect_ok": 0,
        "dup_reconnect_fail": 0,
    }

    def add_client(name_prefix: str) -> bool:
        cid = f"{name_prefix}_{rng.randint(1000, 9999)}"
        c, ok_flag = make_client(host, port, cid, keepalive=20, verbose=verbose)
        if ok_flag:
            active.append((cid, c))
            metrics["connect_ok"] += 1
            return True
        metrics["connect_fail"] += 1
        return False

    for _ in range(active_limit // 2):
        add_client("proto_soak")

    deadline = time.time() + duration
    while time.time() < deadline:
        if not active:
            add_client("proto_soak")
            time.sleep(0.1)
            continue

        action = rng.choice(["connect", "publish", "subscribe", "unsubscribe", "clean", "raw", "dup"])
        if action == "connect" and len(active) < active_limit:
            add_client("proto_soak")
        elif action == "publish":
            cid, c = rng.choice(active)
            topic = f"stress/soak/{rng.randint(0, 4)}"
            payload = f"soak_{rng.randint(0, 9999)}"
            if publish_and_wait(c, topic, payload, qos=0, retain=False):
                metrics["publish_ok"] += 1
            else:
                metrics["publish_fail"] += 1
        elif action == "subscribe":
            cid, c = rng.choice(active)
            topic = f"stress/soak/{rng.randint(0, 4)}"
            ok_sub, granted = subscribe_and_wait(c, topic, qos=0, timeout=1.5)
            if ok_sub and (not granted or granted[0] != 0x80):
                metrics["subscribe_ok"] += 1
            else:
                metrics["subscribe_fail"] += 1
        elif action == "unsubscribe":
            cid, c = rng.choice(active)
            topic = f"stress/soak/{rng.randint(0, 4)}"
            if unsubscribe_and_wait(c, topic, timeout=1.5):
                metrics["unsubscribe_ok"] += 1
            else:
                metrics["unsubscribe_fail"] += 1
        elif action == "clean":
            idx = rng.randrange(len(active))
            _, c = active.pop(idx)
            disconnect_clean(c)
            metrics["clean_disconnect"] += 1
        elif action == "raw":
            idx = rng.randrange(len(active))
            _, c = active.pop(idx)
            raw_disconnect(c)
            metrics["raw_disconnect"] += 1
        elif action == "dup":
            cid, _ = rng.choice(active)
            c, ok_flag = make_client(host, port, cid, keepalive=20, verbose=verbose)
            if ok_flag:
                active.append((cid, c))
                metrics["dup_reconnect_ok"] += 1
            else:
                metrics["dup_reconnect_fail"] += 1

        time.sleep(0.1)

    for _, c in active:
        disconnect_clean(c)
    time.sleep(1.0)

    all_free, probe_connected = wait_until_slots_available(
        host, port, max_clients, "proto_soak_probe", keepalive=20, verbose=verbose,
        timeout=5.0, connect_timeout=1.0
    )
    if verbose or not all_free:
        info(f"[soak] metrics={metrics}")
    R.check(all_free,
            f"Soak: broker stayed healthy and all {max_clients} slots were available after cleanup "
            f"({probe_connected}/{max_clients})")


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
    parser = argparse.ArgumentParser(description="ESP32 MQTT protocol semantics test")
    parser.add_argument("--host", required=True, help="IP address of ESP32 MQTT broker")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--max-clients", type=int, default=16, dest="max_clients")
    parser.add_argument("--max-subs", type=int, default=8, dest="max_subs")
    parser.add_argument("--duration", type=int, default=60,
                        help="Duration in seconds for random soak test")
    parser.add_argument("--tests", default=None,
                        help="Run only selected tests, e.g. 1-4 or 1,3")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    print(f"\n{'═' * 60}")
    print("  ESP32 MQTT Protocol Semantics Test")
    print(f"  host={args.host}:{args.port}  max_clients={args.max_clients}")
    print(f"{'═' * 60}")

    info("Checking TCP reachability...")
    s = raw_connect_tcp(args.host, args.port, timeout=3.0)
    if not s:
        print(f"\n{RED}FATAL: unable to connect to {args.host}:{args.port}{RESET}")
        sys.exit(1)
    s.close()
    ok(f"Broker reachable at {args.host}:{args.port}")

    tests = {
        1: lambda: test_retained_messages(args.host, args.port, args.verbose),
        2: lambda: test_wildcard_routing(args.host, args.port, args.verbose),
        3: lambda: test_max_subscriptions_per_client(args.host, args.port, args.max_subs, args.verbose),
        4: lambda: test_random_protocol_soak(args.host, args.port, args.max_clients, args.duration, args.verbose),
    }

    selected = parse_test_selection(args.tests)
    if selected is not None:
        unknown = sorted(n for n in selected if n not in tests)
        if unknown:
            print(f"{RED}FATAL: unknown test numbers: {unknown}{RESET}")
            sys.exit(2)
        info(f"Running only tests: {', '.join(str(n) for n in sorted(selected))}")
        for n in sorted(selected):
            tests[n]()
        R.summary()
        sys.exit(0 if R.failed == 0 else 1)

    for n in sorted(tests):
        tests[n]()

    R.summary()
    sys.exit(0 if R.failed == 0 else 1)


if __name__ == "__main__":
    main()
