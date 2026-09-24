#!/usr/bin/env python3

from dataclasses import dataclass, replace
import hashlib
import json
import sys
from functools import lru_cache
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Lock
from urllib.parse import parse_qs, urlsplit


FIRMWARE_ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = FIRMWARE_ROOT / "main/web/index.html"
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_web import build
STATE_LOCK = Lock()


@dataclass
class PreviewState:
    device_name: str = "スマートライト"
    hostname: str = "smart-light"
    timeout: int = 60
    ambient_threshold: int = 30
    ambient_enabled: bool = True
    night_feature_enabled: bool = True
    light_enabled: bool = True
    switch_enabled: bool = True
    night_enabled: bool = False
    status_message: str = ""
    status_is_error: bool = False
    settings_open: bool = False


STATE = PreviewState()


@lru_cache(maxsize=1)
def assets(mtime):
    return build(TEMPLATE)


def state_json() -> bytes:
    with STATE_LOCK:
        state = replace(STATE)
        STATE.status_message = ""
        STATE.status_is_error = False
    return json.dumps({
        "light": state.light_enabled, "switch": state.switch_enabled,
        "night": state.night_enabled, "ambient": state.ambient_enabled,
        "night_feature": state.night_feature_enabled,
        "ambient_value": 42, "device_name": state.device_name,
        "hostname": state.hostname, "timeout": state.timeout,
        "ambient_threshold": state.ambient_threshold,
        "message": state.status_message, "error": state.status_is_error,
        "reboot": False, "preview": True,
    }, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def encoding_quality(header, coding):
    qualities = {}
    for item in header.split(","):
        name, _, parameter = item.strip().partition(";")
        q = 1.0
        if parameter:
            try:
                q = float(parameter.strip().removeprefix("q="))
                if not 0 <= q <= 1: q = 0.0
            except ValueError:
                q = 0.0
        qualities[name.lower()] = q
    if coding in qualities: return qualities[coding]
    if coding == "identity": return 0 if qualities.get("*") == 0 else 1
    return qualities.get("*", 0)


def set_status(message: str, is_error: bool = False, open_settings: bool = True):
    STATE.status_message = message
    STATE.status_is_error = is_error
    STATE.settings_open = open_settings


def apply_linked_rules(target: str, enabled: bool):
    direct_state = {
        "light": STATE.light_enabled,
        "switch": STATE.switch_enabled,
        "night": STATE.night_enabled,
    }
    direct_state[target] = enabled
    final_state = direct_state.copy()

    if target == "light" and final_state["night"]:
        final_state["night"] = False
    if target == "night" and final_state["light"]:
        final_state["light"] = False
    if final_state["night"]:
        final_state["switch"] = False
    elif target == "light":
        final_state["switch"] = final_state["light"]

    STATE.light_enabled = final_state["light"]
    STATE.switch_enabled = final_state["switch"]
    STATE.night_enabled = final_state["night"]
    return direct_state, final_state


def action_status(target: str, enabled: bool, direct_state, final_state) -> str:
    labels = {
        "light": "照明",
        "switch": "人感センサ連動",
        "night": "常夜灯",
    }
    state_text = "オン" if enabled else "オフ"
    message = f"{labels[target]}を{state_text}にしました。"

    linked_changes = []
    for linked_target in ("light", "switch", "night"):
        if linked_target == target:
            continue
        if direct_state[linked_target] == final_state[linked_target]:
            continue
        linked_state = "オン" if final_state[linked_target] else "オフ"
        linked_changes.append(f"{labels[linked_target]}を{linked_state}")

    if linked_changes:
        message += f" 連動して{'、'.join(linked_changes)}にしました。"
    return message


class PreviewHandler(BaseHTTPRequestHandler):
    def send_html(self):
        plain, compressed = assets(TEMPLATE.stat().st_mtime_ns)
        header = self.headers.get("Accept-Encoding", "")
        gzip = encoding_quality(header, "gzip") > 0
        if not gzip and encoding_quality(header, "identity") == 0:
            self.send_error(406)
            return
        content = compressed if gzip else plain
        etag = '"' + hashlib.sha256(content).hexdigest()[:24] + '"'
        matches = [item.strip().removeprefix("W/") for item in
                   self.headers.get("If-None-Match", "").split(",")]
        unchanged = etag in matches or "*" in matches
        self.send_response(304 if unchanged else 200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Vary", "Accept-Encoding")
        self.send_header("ETag", etag)
        if gzip: self.send_header("Content-Encoding", "gzip")
        if not unchanged: self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        if not unchanged: self.wfile.write(content)

    def send_state(self):
        content = state_json()
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)

    def respond_mutation(self):
        if "application/json" in self.headers.get("Accept", ""):
            self.send_state()
        else:
            self.send_response(303)
            self.send_header("Location", "/")
            self.send_header("Content-Length", "0")
            self.end_headers()

    def read_form(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8")
        return parse_qs(body, keep_blank_values=True)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/": self.send_html()
        elif path == "/state": self.send_state()
        else: self.send_error(404)

    def do_POST(self):
        form = self.read_form()
        with STATE_LOCK:
            if self.path == "/settings":
                self.handle_settings(form)
            elif self.path == "/action":
                self.handle_action(form)
            elif self.path == "/record":
                self.handle_record(form)
            else:
                set_status("不明な操作です。", True)
        self.respond_mutation()

    def handle_settings(self, form):
        device_name = form.get("device_name", [""])[0].strip()
        hostname = form.get("hostname", [""])[0].strip()
        try:
            timeout = int(form.get("timeout", ["0"])[0])
            threshold = int(form.get("ambient_threshold", ["-1"])[0])
        except ValueError:
            timeout = 0
            threshold = -1

        if (
            not device_name
            or len(device_name.encode("utf-8")) > 64
            or not hostname
            or timeout <= 0
            or not 0 <= threshold <= 100
        ):
            set_status(
                "入力内容を確認してください。設定は保存されませんでした。", True
            )
            return

        STATE.device_name = device_name
        STATE.hostname = hostname
        STATE.timeout = timeout
        STATE.ambient_threshold = threshold
        set_status("基本設定を保存しました。")

    def handle_action(self, form):
        target = form.get("target", [""])[0]
        value = form.get("state", [""])[0]
        if value not in ("on", "off"):
            set_status("操作内容が不正です。", True)
            return

        enabled = value == "on"
        state_keys = {
            "light": "light_enabled",
            "switch": "switch_enabled",
            "night": "night_enabled",
        }
        if target in state_keys:
            direct_state, final_state = apply_linked_rules(target, enabled)
            set_status(
                action_status(target, enabled, direct_state, final_state),
                open_settings=False,
            )
        elif target == "ambient":
            STATE.ambient_enabled = enabled
            state_text = "オン" if enabled else "オフ"
            set_status(
                f"明るさ連動を{state_text}にしました。", open_settings=False
            )
        elif target == "night_feature":
            STATE.night_feature_enabled = enabled
            if not enabled:
                STATE.night_enabled = False
            state_text = "有効" if enabled else "無効"
            set_status(
                f"常夜灯エンドポイントを{state_text}にしました。"
                "プレビューでは再起動を省略します。"
            )
        else:
            set_status("操作対象が不正です。", True)

    def handle_record(self, form):
        target = form.get("target", [""])[0]
        names = {"on": "点灯", "off": "消灯", "night": "常夜灯"}
        if target not in names:
            set_status("赤外線リモコンの記録対象が不正です。", True)
            return
        set_status(f"{names[target]}ボタンの赤外線信号を記録しました。")

    def log_message(self, format, *args):
        print(format % args)


if __name__ == "__main__":
    address = ("127.0.0.1", 8000)
    print(f"Web UI preview: http://{address[0]}:{address[1]}")
    ThreadingHTTPServer(address, PreviewHandler).serve_forever()
