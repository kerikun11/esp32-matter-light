// Shared HTTP transport and safe DOM helpers. No product state.
window.DeviceUI = (() => {
  async function request(url, options = {}) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 20000);
    try {
      const response = await fetch(url, {
        ...options, headers: {Accept: 'application/json'},
        signal: controller.signal, cache: 'no-store'
      });
      if (!response.ok) throw new Error('Request failed');
      return await response.json();
    } finally {
      clearTimeout(timeout);
    }
  }
  function infoRow(list, label, value) {
    const row = document.createElement('tr');
    const term = document.createElement('th');
    const description = document.createElement('td');
    term.scope = 'row';
    term.textContent = label;
    description.textContent = value;
    row.append(term, description);
    list.append(row);
  }
  return {request, infoRow};
})();
