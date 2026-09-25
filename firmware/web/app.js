(() => {
  const qrBase = 'https://project-chip.github.io/connectedhomeip/qrcode.html?data=';
  let submitting = true;
  let infoGeneration = 0;
  let infoLoading = false;
  let infoKnown = false;
  let commissioningOpen = false;
  let fabricSignature = null;
  const get = (id) => document.getElementById(id);
  const buttons = () => document.querySelectorAll('button');
  const labels = new Map(Array.from(buttons(), (button) => [button, button.textContent]));
  const notice = (message, error = false) => {
    const status = get('status');
    status.textContent = message;
    status.className = 'status ' + (error ? 'error' : 'success');
    status.hidden = !message;
  };

  function render(state) {
    for (const key of ['light', 'switch', 'night', 'ambient', 'night_feature', 'error', 'reboot']) {
      if (typeof state[key] !== 'boolean') throw new Error('Invalid state');
    }
    for (const key of ['device_name', 'hostname', 'message']) {
      if (typeof state[key] !== 'string') throw new Error('Invalid state');
    }
    for (const key of ['timeout', 'ambient_threshold', 'ambient_value']) {
      if (!Number.isFinite(state[key])) throw new Error('Invalid state');
    }
    buttons().forEach((button) => {
      button.classList.remove('pending');
      button.removeAttribute('aria-busy');
      button.textContent = labels.get(button) || button.dataset.label;
      button.disabled = !!state.reboot;
    });
    document.querySelectorAll('.toggle-form').forEach((form) => {
      const target = form.elements.target.value;
      const enabled = state[target];
      const button = form.querySelector('button');
      form.elements.state.value = enabled ? 'off' : 'on';
      button.classList.toggle('on', enabled);
      button.classList.toggle('off', !enabled);
      button.textContent = target === 'night_feature'
        ? (enabled ? '有効' : '無効') : (enabled ? 'オン' : 'オフ');
    });
    document.title = state.device_name;
    get('device-name').textContent = state.device_name;
    const settings = get('settings-form');
    for (const key of ['device_name', 'hostname', 'timeout', 'ambient_threshold']) {
      settings.elements[key].value = state[key];
    }
    get('thresholdValue').textContent = state.ambient_threshold;
    get('ambient-value').textContent = state.ambient_value;
    get('night-control').hidden = !state.night_feature;
    get('night-record').hidden = !state.night_feature;
    get('preview').hidden = !state.preview;
    get('reboot').hidden = !state.reboot;
    notice(state.message, state.error);
    document.querySelector('main').removeAttribute('aria-busy');
    submitting = !!state.reboot;
    updateMatterButtons();
  }

  const request = DeviceUI.request;

  document.addEventListener('submit', async (event) => {
    const form = event.target;
    if (form.method.toLowerCase() !== 'post') return;
    event.preventDefault();
    if (submitting) return;
    const isMatter = form.getAttribute('action') === '/matter';
    if (isMatter && !infoKnown) return;
    if (form.dataset.confirm && !window.confirm(form.dataset.confirm)) return;
    submitting = true;
    if (isMatter) {
      ++infoGeneration;
      infoKnown = false;
      get('matter-result').textContent = '';
    }
    const data = new FormData(form);
    const button = event.submitter;
    if (button && button.name) data.append(button.name, button.value);
    buttons().forEach((item) => { item.disabled = true; });
    const linked = form.getAttribute('action') === '/action' &&
      ['light', 'switch', 'night'].includes(data.get('target'));
    const pending = linked
      ? Array.from(document.querySelectorAll('.control-grid button'))
      : [button].filter(Boolean);
    pending.forEach((item) => {
      item.classList.add('pending');
      item.textContent = '送信中…';
      item.setAttribute('aria-busy', 'true');
    });
    notice('');
    try {
      const state = await request(form.getAttribute('action'), {method: 'POST', body: new URLSearchParams(data)});
      render(state);
      if (isMatter) {
        get('matter-result').textContent = state.message;
        await loadDeviceInfo();
      }
    } catch (error) {
      /* A lost response does not mean the operation failed. Never resend it
         automatically or restore the pre-operation state as if it were current. */
      pending.forEach((item) => {
        item.textContent = '結果未確認';
        item.removeAttribute('aria-busy');
      });
      const message = '操作結果を確認できませんでした。ページを再読み込みして状態を確認してください。';
      notice(message, true);
      if (isMatter) get('matter-result').textContent = message;
    }
  });
  async function load() {
    submitting = true;
    buttons().forEach((button) => { button.disabled = true; });
    try { render(await request('/state')); }
    catch (error) { notice('状態を取得できませんでした。ページを再読み込みしてください。', true); }
  }
  const infoRow = DeviceUI.infoRow;
  function updateMatterButtons() {
    document.querySelectorAll('form[action="/matter"] button').forEach((button) => {
      button.disabled = submitting || !infoKnown ||
        (button.id === 'commissioning-start' && commissioningOpen);
    });
  }
  async function loadDeviceInfo() {
    const generation = ++infoGeneration;
    infoLoading = true;
    infoKnown = false;
    updateMatterButtons();
    try {
      const info = await request('/device-info');
      if (generation !== infoGeneration) return;
      if (typeof info.commissioning_open !== 'boolean' || !Array.isArray(info.fabrics)) {
        throw new Error('Invalid device info');
      }
      const list = get('device-info');
      list.replaceChildren();
      infoRow(list, 'IPv4アドレス', info.ipv4 || '未取得');
      infoRow(list, 'IPv6アドレス', info.ipv6.join('\n') || '未取得');
      infoRow(list, 'Wi-Fi', info.connected ? info.ssid + '（' + info.rssi + ' dBm）' : '未接続');
      infoRow(list, 'ファームウェア', info.version);
      infoRow(list, 'ESP-IDF', info.idf_version);
      infoRow(list, '稼働時間', Math.floor(info.uptime_seconds / 3600) + '時間 ' + Math.floor(info.uptime_seconds % 3600 / 60) + '分');
      get('fabric-count').textContent = '（' + info.fabrics.length + '件）';
      const signature = JSON.stringify(info.fabrics);
      if (signature !== fabricSignature) {
        get('fabric-list').replaceChildren();
        for (const fabric of info.fabrics) {
          const entry = document.createElement('div');
          entry.className = 'fabric-entry';
          const table = document.createElement('table');
          table.className = 'info-table';
          table.setAttribute('aria-label', 'Fabric #' + fabric.index);
          const card = document.createElement('tbody');
          table.append(card);
          infoRow(card, 'Fabric #' + fabric.index, fabric.label || 'ラベルなし');
          infoRow(card, 'Fabric ID', fabric.fabric_id);
          infoRow(card, 'Node ID', fabric.node_id);
          infoRow(card, 'Vendor ID', fabric.vendor_id);
          const form = document.createElement('form');
          form.method = 'post';
          form.action = '/matter';
          form.dataset.confirm = 'Fabric #' + fabric.index + '（' +
            (fabric.label || 'ラベルなし') + '）を削除しますか？\n' +
            'このFabricに属するコントローラーから操作できなくなります。Wi-Fi接続は維持されます。' +
            (info.fabrics.length === 1 ? '\n最後のFabricです。削除後、再登録にはペアリング受付の開始が必要です。' : '');
          for (const [name, value] of Object.entries({
            action: 'remove', index: fabric.index, fabric_id: fabric.fabric_id,
            node_id: fabric.node_id, vendor_id: fabric.vendor_id
          })) {
            const input = document.createElement('input');
            input.type = 'hidden';
            input.name = name;
            input.value = value;
            form.append(input);
          }
          const button = document.createElement('button');
          button.type = 'submit';
          button.className = 'danger';
          button.dataset.label = 'このFabricを削除';
          button.textContent = button.dataset.label;
          button.setAttribute('aria-label', 'Fabric #' + fabric.index + 'を削除');
          form.append(button);
          entry.append(table, form);
          get('fabric-list').append(entry);
        }
        if (!info.fabrics.length) get('fabric-list').textContent = '登録済みのFabricはありません。';
        fabricSignature = signature;
      }
      commissioningOpen = info.commissioning_open;
      infoKnown = true;
      get('commissioning-status').textContent = commissioningOpen
        ? '状態：受付中（最大5分間）' : '状態：受付停止中';
      get('pairing').hidden = !commissioningOpen;
      get('manual-code').textContent = info.manual_code.replace(/^(\d{4})(\d{3})(\d{4})$/, '$1-$2-$3');
      get('qr-link').href = qrBase + encodeURIComponent(info.qr_payload);
      get('info-status').textContent = '設定を開いている間、5秒ごとに更新します。';
    } catch (error) {
      if (generation !== infoGeneration) return;
      get('commissioning-status').textContent = '状態：取得できませんでした';
      get('info-status').textContent = '最新情報を取得できませんでした。自動で再取得します。';
    } finally {
      if (generation === infoGeneration) {
        infoLoading = false;
        updateMatterButtons();
      }
    }
  }
  setInterval(() => {
    if (get('settings-pane').open && !document.hidden && !submitting && !infoLoading) loadDeviceInfo();
  }, 5000);
  get('settings-pane').addEventListener('toggle', () => {
    if (get('settings-pane').open) loadDeviceInfo();
  });
  window.addEventListener('pageshow', (event) => {
    if (event.persisted && get('settings-pane').open) loadDeviceInfo();
  });
  // A browser back/forward-cache restore must also fetch fresh device state.
  window.addEventListener('pageshow', (event) => { if (event.persisted) load(); });
  load();
})();
