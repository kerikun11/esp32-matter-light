#!/usr/bin/env python3

from dataclasses import dataclass, replace
from html import escape
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Lock
from urllib.parse import parse_qs


FIRMWARE_ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = FIRMWARE_ROOT / "main/smart_light_web_page.inc"
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


@dataclass(frozen=True)
class ToggleValues:
    action: str
    css_class: str
    label: str


STATE = PreviewState()


def toggle_values(
    enabled: bool, on_text: str = "オン", off_text: str = "オフ"
) -> ToggleValues:
    return ToggleValues(
        action="off" if enabled else "on",
        css_class="on" if enabled else "off",
        label=on_text if enabled else off_text,
    )


def status_notice(message: str, is_error: bool) -> str:
    if not message:
        return ""
    kind = "error" if is_error else "success"
    return f'<div class="status {kind}">{escape(message)}</div>'


def night_control(enabled: bool) -> str:
    values = toggle_values(enabled)
    return (
        '<div class="control-item"><span class="label">常夜灯</span>'
        '<form class="toggle-form" method="post" action="/action">'
        '<input type="hidden" name="target" value="night">'
        f'<input type="hidden" name="state" value="{values.action}">'
        f'<button class="toggle-btn {values.css_class}">'
        f"{values.label}</button></form></div>"
    )


def read_template() -> str:
    source = TEMPLATE.read_text(encoding="utf-8").strip()
    prefix = 'R"HTML('
    suffix = ')HTML"'
    if not source.startswith(prefix) or not source.endswith(suffix):
        raise RuntimeError(f"Unexpected template format: {TEMPLATE}")
    return source[len(prefix) : -len(suffix)]


def consume_state() -> PreviewState:
    with STATE_LOCK:
        state = replace(STATE)
        STATE.status_message = ""
        STATE.status_is_error = False
        STATE.settings_open = False
    return state


def render() -> bytes:
    state = consume_state()

    light = toggle_values(state.light_enabled)
    switch = toggle_values(state.switch_enabled)
    night_feature = toggle_values(
        state.night_feature_enabled, "有効", "無効"
    )
    values = {
        "{{DEVICE_NAME}}": escape(state.device_name, quote=True),
        "{{PREVIEW_NOTICE}}": (
            '<div class="notice" style="margin-top:0;margin-bottom:14px;'
            'border-color:#93c5fd;background:#eff6ff;color:#1e40af">'
            "PC確認用プレビューです。操作しても実機の設定は変更されません。</div>"
        ),
        "{{SETTINGS_OPEN}}": "open" if state.settings_open else "",
        "{{LIGHT_ACTION}}": light.action,
        "{{LIGHT_CLASS}}": light.css_class,
        "{{LIGHT_STATE}}": light.label,
        "{{SWITCH_ACTION}}": switch.action,
        "{{SWITCH_CLASS}}": switch.css_class,
        "{{SWITCH_STATE}}": switch.label,
        "{{NIGHT_CONTROL}}": (
            night_control(state.night_enabled)
            if state.night_feature_enabled
            else ""
        ),
        "{{AMBIENT_VALUE}}": "42",
        "{{AMBIENT_STATUS_CLASS}}": "on" if state.ambient_enabled else "off",
        "{{AMBIENT_STATUS_STATE}}": "オン" if state.ambient_enabled else "オフ",
        "{{AMBIENT_ACTION}}": "off" if state.ambient_enabled else "on",
        "{{REBOOT_NOTICE}}": "",
        "{{STATUS_NOTICE}}": status_notice(
            state.status_message, state.status_is_error
        ),
        "{{HOSTNAME}}": escape(state.hostname, quote=True),
        "{{TIMEOUT}}": str(state.timeout),
        "{{AMBIENT_THRESHOLD}}": str(state.ambient_threshold),
        "{{NIGHT_FEATURE_ACTION}}": night_feature.action,
        "{{NIGHT_FEATURE_CLASS}}": night_feature.css_class,
        "{{NIGHT_FEATURE_STATE}}": night_feature.label,
        "{{NIGHT_RECORD_BUTTON}}": (
            '<button class="warn" name="target" value="night">'
            "常夜灯ボタンを記録</button>"
            if state.night_feature_enabled
            else ""
        ),
    }

    html = read_template()
    for key, value in values.items():
        html = html.replace(key, value)
    return html.encode("utf-8")


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
        try:
            content = render()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)
        except Exception as error:
            content = str(error).encode("utf-8")
            self.send_response(500)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(content)))
            self.end_headers()
            self.wfile.write(content)

    def redirect_root(self):
        self.send_response(303)
        self.send_header("Location", "/")
        self.end_headers()

    def read_form(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8")
        return parse_qs(body, keep_blank_values=True)

    def do_GET(self):
        self.send_html()

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
        self.redirect_root()

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
