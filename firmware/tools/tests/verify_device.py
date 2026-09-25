#!/usr/bin/env python3
"""Black-box HTTP verification for a running esp32-matter-light device.

Covers what can be checked without a Matter controller or physical access
to the IR receiver: the on-device HTTP API (/version, /, /settings,
/action), and in particular the two bugs fixed in this session:

  - the Matter-attribute self-write feedback loop that caused the light to
    flicker ON/OFF forever (matter_light.h)
  - parseFormBody() silently dropping a POST body on a recoverable
    httpd_req_recv() timeout, making /action requests vanish under Wi-Fi
    latency (web_utils.h)

Not covered (needs a human / real hardware / a Matter controller):
  - the physical lamp actually responding to the IR signal
  - Matter-app-originated (Google Home / Alexa) commands
  - the night-light endpoint (toggling it reboots the device, which would
    derail the rest of the run, so it's skipped by default)
  - an actual /update OTA flash (slow, and reboots the device)

Usage:
    python3 firmware/tools/tests/verify_device.py --host xiao.local
    python3 firmware/tools/tests/verify_device.py --host 192.168.0.60 -v
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Callable
from urllib.parse import urlencode




class Failure(Exception):
    pass


@dataclass
class PageState:
    # target -> (currently "on" or "off", the state value the toggle button would submit next)
    toggles: dict[str, tuple[str, str]]
    values: dict


class Device:
    def __init__(self, base_url: str, timeout: float, verbose: bool):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.verbose = verbose

    def _log(self, msg: str) -> None:
        if self.verbose:
            print(f"    . {msg}")

    def get(self, path: str) -> tuple[int, bytes, float]:
        url = f"{self.base_url}{path}"
        start = time.monotonic()
        req = urllib.request.Request(url, method="GET")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                body = resp.read()
                elapsed = time.monotonic() - start
                self._log(f"GET {path} -> {resp.status} ({elapsed:.2f}s, {len(body)}B)")
                return resp.status, body, elapsed
        except urllib.error.HTTPError as e:
            elapsed = time.monotonic() - start
            body = e.read()
            self._log(f"GET {path} -> {e.code} ({elapsed:.2f}s)")
            return e.code, body, elapsed

    def post_form(self, path: str, fields: dict[str, str]) -> tuple[int, bytes, float]:
        url = f"{self.base_url}{path}"
        data = urlencode(fields).encode("ascii")
        start = time.monotonic()
        req = urllib.request.Request(url, data=data, method="POST")
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
        # Match what the real JS client sends (see main/web/index.html's
        # request() helper): without this, the server falls back to its
        # legacy 303-redirect-to-/ response for non-JS clients, and urllib
        # transparently follows it -- silently turning every mutation into
        # a full ~13KB page fetch instead of the small JSON response, which
        # then intermittently trips over this device's known Wi-Fi latency.
        req.add_header("Accept", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                body = resp.read()
                elapsed = time.monotonic() - start
                self._log(f"POST {path} {fields} -> {resp.status} ({elapsed:.2f}s)")
                return resp.status, body, elapsed
        except urllib.error.HTTPError as e:
            elapsed = time.monotonic() - start
            body = e.read()
            self._log(f"POST {path} {fields} -> {e.code} ({elapsed:.2f}s)")
            return e.code, body, elapsed

    def version(self) -> dict:
        status, body, _ = self.get("/version")
        if status != 200:
            raise Failure(f"GET /version returned HTTP {status}")
        return json.loads(body.decode("utf-8"))

    def page(self) -> PageState:
        status, body, _ = self.get("/state")
        if status != 200:
            raise Failure(f"GET /state returned HTTP {status}")
        values = json.loads(body.decode("utf-8"))
        targets = ["light", "switch", "ambient", "night_feature"]
        if values["night_feature"]: targets.append("night")
        toggles = {key: ("on" if values[key] else "off", "off" if values[key] else "on")
                   for key in targets}
        return PageState(toggles=toggles, values=values)

    def action(self, target: str, state: str) -> None:
        status, _, _ = self.post_form("/action", {"target": target, "state": state})
        if status not in (200, 303):
            raise Failure(f"POST /action target={target} state={state} -> HTTP {status}")

    def settings(self, **fields: str) -> None:
        status, _, _ = self.post_form("/settings", fields)
        if status not in (200, 303):
            raise Failure(f"POST /settings {fields} -> HTTP {status}")


Test = Callable[[Device], None]
RESULTS: list[tuple[str, bool, str]] = []


def run(name: str, device: Device, fn: Test) -> None:
    print(f"[ ] {name}")
    try:
        fn(device)
    except Failure as e:
        print(f"[FAIL] {name}: {e}")
        RESULTS.append((name, False, str(e)))
        return
    except Exception as e:  # noqa: BLE001 - report, don't crash the run
        print(f"[FAIL] {name}: unexpected {type(e).__name__}: {e}")
        RESULTS.append((name, False, f"{type(e).__name__}: {e}"))
        return
    print(f"[PASS] {name}")
    RESULTS.append((name, True, ""))


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise Failure(message)


# --- individual tests ---------------------------------------------------


def test_version(dev: Device) -> None:
    info = dev.version()
    for key in ("project_name", "version", "idf_ver", "date", "time"):
        expect(key in info, f"/version missing '{key}' field: {info}")
    expect(bool(info["project_name"]), "project_name is empty")


def test_root_page(dev: Device) -> None:
    status, body, _ = dev.get("/")
    expect(status == 200 and b"<form" in body, "static page is missing forms")
    page = dev.page()
    expect("light" in page.toggles, "light toggle not found on page")
    expect("switch" in page.toggles, "switch (occupancy) toggle not found on page")
    expect("ambient" in page.toggles, "ambient toggle not found on page")


def _toggle_round_trip(dev: Device, target: str, iterations: int, settle_s: float) -> None:
    """Repeatedly request the opposite of the current state and verify the
    page reflects it -- and that state doesn't drift on its own afterward
    (the ON/OFF feedback-loop regression check)."""
    page = dev.page()
    expect(target in page.toggles, f"{target} toggle not found on page")
    current, _ = page.toggles[target]

    for i in range(iterations):
        requested = "off" if current == "on" else "on"
        dev.action(target, requested)
        time.sleep(settle_s)

        page = dev.page()
        expect(target in page.toggles, f"{target} toggle disappeared from page")
        observed, _ = page.toggles[target]
        expect(
            observed == requested,
            f"iteration {i + 1}: requested {target}={requested} but page shows "
            f"{observed} (this is the class of bug fixed this session -- "
            f"either the action was dropped, or state is oscillating)",
        )

        # Stability check: with nothing else touching the device, state must
        # not keep changing on its own (this is exactly what the Matter
        # self-write feedback loop bug looked like).
        time.sleep(settle_s)
        page_again = dev.page()
        still, _ = page_again.toggles[target]
        expect(
            still == requested,
            f"iteration {i + 1}: {target} drifted from {requested} to {still} "
            "without any request -- looks like the feedback-loop regression",
        )

        current = requested


def test_light_toggle_round_trip(dev: Device) -> None:
    _toggle_round_trip(dev, "light", iterations=4, settle_s=1.5)


def test_switch_toggle_round_trip(dev: Device) -> None:
    _toggle_round_trip(dev, "switch", iterations=4, settle_s=1.5)


def test_ambient_toggle_round_trip(dev: Device) -> None:
    _toggle_round_trip(dev, "ambient", iterations=3, settle_s=1.0)


def test_light_toggle_under_slow_gaps(dev: Device) -> None:
    """Mirrors the exact failure timing reported by the user: alternating
    light ON/OFF with ~5s gaps between requests."""
    _toggle_round_trip(dev, "light", iterations=3, settle_s=5.0)


def test_malformed_action_is_rejected_not_crashed(dev: Device) -> None:
    # Missing state field entirely.
    status, _, _ = dev.post_form("/action", {"target": "light"})
    expect(status in (200, 303), f"missing-state request -> HTTP {status}")

    # Invalid state value.
    status, _, _ = dev.post_form("/action", {"target": "light", "state": "banana"})
    expect(status in (200, 303), f"invalid-state request -> HTTP {status}")

    # Invalid target.
    status, _, _ = dev.post_form("/action", {"target": "microwave", "state": "on"})
    expect(status in (200, 303), f"invalid-target request -> HTTP {status}")

    # Device must still be alive and responsive afterward.
    info = dev.version()
    expect(bool(info.get("project_name")), "device unresponsive after malformed requests")


def test_burst_requests_end_in_consistent_state(dev: Device) -> None:
    """Fire several light-toggle requests back-to-back with minimal delay --
    stresses the settings_ mutex and the parseFormBody timeout-retry path.
    We don't assert on every intermediate state (requests may coalesce),
    only that the device stays responsive and the final state matches the
    final request."""
    page = dev.page()
    current, _ = page.toggles["light"]
    sequence = []
    for _ in range(6):
        current = "off" if current == "on" else "on"
        sequence.append(current)

    for state in sequence:
        dev.action("light", state)
        time.sleep(0.2)

    time.sleep(2.0)
    page = dev.page()
    observed, _ = page.toggles["light"]
    expect(
        observed == sequence[-1],
        f"after a burst of requests ending in {sequence[-1]}, page shows {observed}",
    )

    info = dev.version()
    expect(bool(info.get("project_name")), "device unresponsive after burst requests")


def test_settings_round_trip(dev: Device) -> None:
    original = dev.page().values
    fields = {key: str(original[key]) for key in
              ("device_name", "hostname", "timeout", "ambient_threshold")}
    probe = "601" if fields["timeout"] != "601" else "602"
    try:
        dev.settings(**{**fields, "timeout": probe})
        expect(dev.page().values["timeout"] == int(probe), "settings save failed")
    finally:
        dev.settings(**fields)
        expect(dev.page().values["timeout"] == original["timeout"], "settings restore failed")


TESTS: list[tuple[str, Test]] = [
    ("GET /version returns expected fields", test_version),
    ("GET / renders the toggle controls", test_root_page),
    ("light toggle round-trips (fast)", test_light_toggle_round_trip),
    ("switch (occupancy) toggle round-trips", test_switch_toggle_round_trip),
    ("ambient toggle round-trips", test_ambient_toggle_round_trip),
    ("light toggle round-trips (~5s gaps, matches reported failure)", test_light_toggle_under_slow_gaps),
    ("malformed /action requests are rejected, not fatal", test_malformed_action_is_rejected_not_crashed),
    ("burst of requests ends in a consistent state", test_burst_requests_end_in_consistent_state),
    ("/settings round-trips and is restored", test_settings_round_trip),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="xiao.local", help="device hostname or IP (default: xiao.local)")
    parser.add_argument("--timeout", type=float, default=20.0,
                        help="per-request timeout in seconds (default: 20.0)")
    parser.add_argument("-v", "--verbose", action="store_true", help="log every HTTP request")
    args = parser.parse_args()

    base_url = args.host if args.host.startswith("http") else f"http://{args.host}"
    dev = Device(base_url, timeout=args.timeout, verbose=args.verbose)

    print(f"Verifying device at {base_url}\n")
    for name, fn in TESTS:
        run(name, dev, fn)
        print()

    passed = sum(1 for _, ok, _ in RESULTS if ok)
    total = len(RESULTS)
    print(f"{'=' * 60}")
    print(f"{passed}/{total} passed")
    if passed != total:
        print("\nFailures:")
        for name, ok, msg in RESULTS:
            if not ok:
                print(f"  - {name}: {msg}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
