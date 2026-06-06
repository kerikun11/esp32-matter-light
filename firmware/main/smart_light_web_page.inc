R"HTML(<!doctype html>
<html lang="ja">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>{{DEVICE_NAME}}</title>
  <style>
    :root{--panel:#fff;--line:#dbe3ef;--ink:#17212b;--muted:#64748b;--accent:#2563eb;--good:#166534;--bad:#991b1b;--shadow:0 10px 30px rgba(15,23,42,.08)}
    *{box-sizing:border-box}
    body{margin:0;background:linear-gradient(180deg,#f8fbff,#eef3f9);color:var(--ink);font:15px/1.6 system-ui,-apple-system,"Segoe UI",sans-serif}
    main{max-width:720px;margin:0 auto;padding:24px 16px 40px}
    .hero,.fold{overflow:hidden;border:1px solid var(--line);border-radius:20px;background:var(--panel);box-shadow:var(--shadow)}
    .hero{padding:24px}.hero h1{margin:0;font-size:28px;line-height:1.2}
    .topbar{display:grid;grid-template-columns:minmax(0,1fr) 220px;gap:14px;margin-top:20px;align-items:stretch}
    .card,.stat{padding:18px;border:1px solid var(--line);border-radius:14px;background:#fbfdff}
    .card{display:flex;align-items:center}.control-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:16px;width:100%}
    .control-item{display:grid;grid-template-rows:auto 42px;gap:10px;align-content:center}
    .label{display:block;margin-bottom:6px;color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.04em}
    .value{font-size:30px;font-weight:750}
    .stat{display:grid;align-content:center;gap:12px}.ambient-row{display:grid;grid-template-columns:minmax(110px,1fr) auto;align-items:center;gap:14px}.ambient-row .value{white-space:nowrap}
    form{margin:0}.toggle-btn{display:flex;align-items:center;justify-content:center;gap:8px;width:100%;min-height:42px;padding:10px 14px;border:none;border-radius:999px;font-size:14px;font-weight:700;cursor:pointer}
    .toggle-btn:before{content:"";width:8px;height:8px;border-radius:50%;background:currentColor}
    .toggle-btn.on{background:#e8f7ee;color:var(--good)}.toggle-btn.off{background:#fdecec;color:var(--bad)}
    .two-col{display:grid;gap:14px;margin-top:14px}.stack{display:grid;gap:16px}
    .field{display:grid;gap:6px}.field>span:first-child{font-weight:650}.mini{color:var(--muted);font-size:13px;font-weight:400}
    .row{display:flex;align-items:center;justify-content:space-between;gap:16px}
    .section-title{margin:0;font-size:16px}.section-description{margin:2px 0 0;color:var(--muted);font-size:13px}
    .settings-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}
    .setting-card{padding:16px;border:1px solid var(--line);border-radius:14px;background:#fbfdff}
    .setting-card.wide{grid-column:1/-1}
    .range-row{display:grid;grid-template-columns:1fr 52px;gap:12px;align-items:center}.range-row strong{text-align:right}
    input[type=text],input[type=number]{width:100%;padding:12px 13px;border:1px solid var(--line);border-radius:12px;background:#fff;color:var(--ink);font:inherit}
    input[type=range]{width:100%;accent-color:var(--accent)}
    button{border:0;border-radius:12px;padding:14px 18px;background:var(--accent);color:#fff;font:700 15px system-ui;cursor:pointer}
    button.warn{background:#0f172a}.group{display:flex;flex-wrap:wrap;gap:10px}
    .save-row{padding:16px;border:1px solid #bfdbfe;border-radius:14px;background:#eff6ff}.save-row .mini{margin:0;color:#1e40af}.save-row button{min-width:150px}
    .device-section{display:grid;gap:12px;margin-top:20px;padding-top:18px;border-top:1px solid var(--line)}
    .ir-section{display:grid;gap:12px;margin-top:20px;padding-top:18px;border-top:1px solid var(--line)}
    .fold summary{display:flex;align-items:center;justify-content:space-between;list-style:none;cursor:pointer;padding:18px 20px;font-size:17px;font-weight:700}
    .fold summary::-webkit-details-marker{display:none}.fold summary span{color:var(--muted);font-size:12px;font-weight:500}.fold .body{padding:0 20px 20px}
    .notice{margin-top:14px;padding:14px 18px;border:1px solid #f59e0b;border-radius:12px;background:#fef3c7;color:#92400e;font-size:14px}
    .status{margin-top:14px;padding:14px 18px;border:1px solid;border-radius:12px;font-weight:650}.status.success{border-color:#86efac;background:#f0fdf4;color:var(--good)}.status.error{border-color:#fca5a5;background:#fef2f2;color:var(--bad)}
    @media(max-width:560px){main{padding:14px 10px 28px}.hero{padding:18px}.hero h1{font-size:25px}.topbar{grid-template-columns:1fr}.control-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.ambient-row{grid-template-columns:1fr auto}.settings-grid{grid-template-columns:1fr}.setting-card.wide{grid-column:auto}.row{align-items:stretch}.save-row{flex-direction:column}.save-row button{width:100%}.fold summary{padding:16px}.fold .body{padding:0 16px 16px}.group{display:grid;grid-template-columns:1fr}}
  </style>
</head>
<body>
  <main>
    {{PREVIEW_NOTICE}}
    <section class="hero">
      <h1><a href="/" style="color:inherit;text-decoration:none">{{DEVICE_NAME}}</a></h1>
      <div class="topbar">
        <section class="card">
          <div class="control-grid">
            <div class="control-item">
              <span class="label">照明</span>
              <form class="toggle-form" method="post" action="/action">
                <input type="hidden" name="target" value="light">
                <input type="hidden" name="state" value="{{LIGHT_ACTION}}">
                <button class="toggle-btn {{LIGHT_CLASS}}">{{LIGHT_STATE}}</button>
              </form>
            </div>
            <div class="control-item">
              <span class="label">人感センサ連動</span>
              <form class="toggle-form" method="post" action="/action">
                <input type="hidden" name="target" value="switch">
                <input type="hidden" name="state" value="{{SWITCH_ACTION}}">
                <button class="toggle-btn {{SWITCH_CLASS}}">{{SWITCH_STATE}}</button>
              </form>
            </div>
            {{NIGHT_CONTROL}}
          </div>
        </section>
        <div class="stat">
          <span class="label">明るさ連動</span>
          <div class="ambient-row">
            <form class="toggle-form" method="post" action="/action">
              <input type="hidden" name="target" value="ambient">
              <input type="hidden" name="state" value="{{AMBIENT_ACTION}}">
              <button class="toggle-btn {{AMBIENT_STATUS_CLASS}}">{{AMBIENT_STATUS_STATE}}</button>
            </form>
            <div class="value">{{AMBIENT_VALUE}}%</div>
          </div>
        </div>
      </div>
    </section>

    {{REBOOT_NOTICE}}
    {{STATUS_NOTICE}}

    <div class="two-col">
      <details class="fold" {{SETTINGS_OPEN}}>
        <summary>設定<span>タップして開閉</span></summary>
        <div class="body">
          <form class="stack" method="post" action="/settings">
            <div>
              <h2 class="section-title">基本設定</h2>
              <p class="section-description">以下の項目は、画面下の「設定を保存」でまとめて反映されます。</p>
            </div>
            <div class="settings-grid">
              <label class="field setting-card wide">
                <span>デバイス名</span>
                <input name="device_name" type="text" maxlength="64" required value="{{DEVICE_NAME}}">
                <span class="mini">タイトルとブラウザのタブに表示されます。</span>
              </label>
              <label class="field setting-card">
                <span>OTAホスト名</span>
                <input name="hostname" value="{{HOSTNAME}}">
                <span class="mini">Arduino OTAで使用する識別名です。Matterが管理するmDNS名は変更しません。</span>
              </label>
              <label class="field setting-card">
                <span>自動消灯時間（秒）</span>
                <input name="timeout" type="number" min="1" inputmode="numeric" value="{{TIMEOUT}}">
                <span class="mini">人感センサ連動がオンの場合、この時間非検出が続いた場合に自動消灯します。</span>
              </label>
              <div class="field setting-card wide">
                <span>明るさ連動の閾値</span>
                <span class="mini">明るさ連動がオンの場合、この値を基準に明るさを判定します。</span>
                <div class="range-row">
                  <input id="threshold" name="ambient_threshold" type="range" min="0" max="100" value="{{AMBIENT_THRESHOLD}}" oninput="thresholdValue.textContent=this.value">
                  <strong><span id="thresholdValue">{{AMBIENT_THRESHOLD}}</span>%</strong>
                </div>
              </div>
            </div>
            <div class="row save-row">
              <p class="mini">デバイス名、OTAホスト名、自動消灯時間、明るさ連動の閾値をまとめて保存します。</p>
              <button>設定を保存</button>
            </div>
          </form>
          <div class="device-section">
            <div class="row">
              <div class="field" style="flex:1">
                <span>常夜灯エンドポイント</span>
                <span class="mini">Matterの常夜灯エンドポイントを有効にします。変更すると自動的に再起動します。</span>
              </div>
              <form class="toggle-form" method="post" action="/action">
                <input type="hidden" name="target" value="night_feature">
                <input type="hidden" name="state" value="{{NIGHT_FEATURE_ACTION}}">
                <button class="toggle-btn {{NIGHT_FEATURE_CLASS}}">{{NIGHT_FEATURE_STATE}}</button>
              </form>
            </div>
          </div>
          <div class="ir-section">
            <div>
              <h2 class="section-title">赤外線リモコン学習</h2>
              <p class="section-description">記録するボタンを押し、10秒以内にリモコンの信号を送信してください。</p>
            </div>
            <form class="group" method="post" action="/record">
              <button class="warn" name="target" value="on">点灯ボタンを記録</button>
              <button class="warn" name="target" value="off">消灯ボタンを記録</button>
              {{NIGHT_RECORD_BUTTON}}
            </form>
          </div>
        </div>
      </details>
    </div>
  </main>
</body>
</html>)HTML"
