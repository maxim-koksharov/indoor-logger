#ifndef WEB_UI_H
#define WEB_UI_H

const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>AirMon Server</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Courier New', monospace; background: #1a1a1a; color: #e0e0e0; padding: 20px; }
h1 { color: #4CAF50; margin-bottom: 20px; }
.status { background: #2a2a2a; padding: 15px; border-radius: 8px; margin-bottom: 20px; }
.status span { margin-right: 20px; }
table { width: 100%; border-collapse: collapse; background: #2a2a2a; border-radius: 8px; overflow: hidden; }
th, td { padding: 12px; text-align: left; border-bottom: 1px solid #3a3a3a; }
th { background: #333; color: #4CAF50; }
tr:hover { background: #333; cursor: pointer; }
.online { color: #4CAF50; }
.offline { color: #f44336; }
.waiting { color: #FFC107; }
.card { background: #2a2a2a; padding: 20px; border-radius: 8px; margin-top: 20px; display: none; }
.card h2 { color: #4CAF50; margin-bottom: 15px; }
.metrics { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 15px; margin-bottom: 20px; }
.metric { background: #333; padding: 15px; border-radius: 6px; text-align: center; }
.metric .label { font-size: 0.9em; color: #888; }
.metric .value { font-size: 1.8em; font-weight: bold; margin-top: 5px; }
.controls { margin-bottom: 20px; }
.controls button { background: #333; color: #e0e0e0; border: 1px solid #555; padding: 8px 16px; cursor: pointer; font-family: inherit; font-size: 0.9em; }
.controls button.active { background: #4CAF50; color: #fff; border-color: #4CAF50; }
.controls button:first-child { border-radius: 4px 0 0 4px; }
.controls button:last-child { border-radius: 0 4px 4px 0; }
.controls button:not(:last-child) { border-right: none; }
.chart-box { margin-bottom: 20px; }
.chart-box h3 { color: #4CAF50; margin-bottom: 8px; font-size: 0.95em; }
canvas { width: 100%; height: 180px; background: #333; border-radius: 6px; display: block; }
.error-msg { color: #f44336; padding: 10px; }
.loading { color: #888; }
.name-edit { background: #333; color: #e0e0e0; border: 1px solid #4CAF50; padding: 4px 8px; font-family: inherit; font-size: 1em; width: 200px; border-radius: 4px; }
.name-edit:focus { outline: none; border-color: #4CAF50; }
.name-display { cursor: pointer; }
.name-display:hover { color: #4CAF50; }
.sync-settings { background: #2a2a2a; padding: 15px; border-radius: 8px; margin-bottom: 20px; }
.sync-settings label { margin-right: 10px; color: #888; }
.sync-settings input { background: #333; color: #e0e0e0; border: 1px solid #555; padding: 6px 10px; width: 80px; font-family: inherit; border-radius: 4px; }
.sync-settings button { background: #4CAF50; color: #fff; border: none; padding: 7px 14px; cursor: pointer; font-family: inherit; border-radius: 4px; margin-left: 10px; }
.sync-settings button:disabled { background: #555; cursor: not-allowed; }
.sync-settings #sync-status { margin-left: 10px; color: #4CAF50; }
.timezone-settings { background: #2a2a2a; padding: 15px; border-radius: 8px; margin-bottom: 20px; }
.timezone-settings label { margin-right: 10px; color: #888; }
.timezone-settings select, .timezone-settings input { background: #333; color: #e0e0e0; border: 1px solid #555; padding: 6px 10px; font-family: inherit; border-radius: 4px; }
.timezone-settings select { width: 180px; }
.timezone-settings input { width: 260px; }
.timezone-settings button { background: #4CAF50; color: #fff; border: none; padding: 7px 14px; cursor: pointer; font-family: inherit; border-radius: 4px; margin-left: 10px; }
.timezone-settings button:disabled { background: #555; cursor: not-allowed; }
.timezone-settings #tz-status { margin-left: 10px; color: #4CAF50; }
@media (max-width: 600px) {
  body { padding: 10px; }
  .metrics { grid-template-columns: repeat(2, 1fr); }
}
</style>
</head>
<body>
<h1>AirMon Server</h1>
<div class="status" id="status"><span class="loading">Connecting...</span></div>
<div class="sync-settings">
  <label for="sync-input">Sync interval (sec):</label>
  <input type="number" id="sync-input" min="60" max="86400" value="600">
  <button id="sync-save" onclick="saveSyncInterval()">Save</button>
  <span id="sync-status"></span>
</div>
<div class="timezone-settings">
  <label for="tz-select">Timezone:</label>
  <select id="tz-select" onchange="onTzSelectChange()">
    <option value="WET0WEST,M3.5.0/1,M10.5.0/2">Lisbon (WET/WEST)</option>
    <option value="UTC0">UTC</option>
    <option value="EST5EDT,M3.2.0/2,M11.1.0/2">New York (EST/EDT)</option>
    <option value="CET-1CEST,M3.5.0/2,M10.5.0/3">Berlin (CET/CEST)</option>
    <option value="MSK-3">Moscow (MSK)</option>
    <option value="custom">Custom...</option>
  </select>
  <input type="text" id="tz-input" value="WET0WEST,M3.5.0/1,M10.5.0/2" style="display:none">
  <button id="tz-save" onclick="saveTimezone()">Save</button>
  <span id="tz-status"></span>
</div>
<div id="error-bar"></div>
<table id="clients">
<thead><tr><th>Name</th><th>ID</th><th>Status</th><th>Records</th><th>Last Seen</th><th>IP</th></tr></thead>
<tbody></tbody>
</table>
<div class="card" id="detail">
<h2 id="detail-title"></h2>
<div class="metrics" id="metrics"><span class="loading">Select a client to view data</span></div>
<div class="controls" id="controls">
  <button onclick="setTimeRange(24)" class="active">24h</button>
  <button onclick="setTimeRange(12)">12h</button>
  <button onclick="setTimeRange(6)">6h</button>
  <button onclick="setTimeRange(3)">3h</button>
</div>
<div class="chart-box"><h3>Temperature (&deg;C)</h3><canvas id="ch-temp"></canvas></div>
<div class="chart-box"><h3>Humidity (%)</h3><canvas id="ch-hum"></canvas></div>
<div class="chart-box"><h3>eCO2 (ppm)</h3><canvas id="ch-eco2"></canvas></div>
<div class="chart-box"><h3>TVOC (ppb)</h3><canvas id="ch-tvoc"></canvas></div>
<div class="chart-box"><h3>AQI</h3><canvas id="ch-aqi"></canvas></div>
</div>
<script>
let currentData = null;
let currentTimeRange = 24;
let currentClientId = null;
let lastError = '';
let fetchCount = 0;
let currentTimezone = 'WET0WEST,M3.5.0/1,M10.5.0/2';

function editName() {
  var display = document.getElementById('name-display');
  var editor = document.getElementById('name-editor');
  display.style.display = 'none';
  editor.style.display = 'inline';
  editor.innerHTML = '<input type="text" id="name-input" class="name-edit" value="' + display.textContent.trim() + '" onblur="saveName()" onkeydown="if(event.key==\'Enter\')saveName();if(event.key==\'Escape\')cancelName()">';
  document.getElementById('name-input').focus();
  document.getElementById('name-input').select();
}

async function saveName() {
  var input = document.getElementById('name-input');
  if (!input) return;
  var name = input.value.trim();
  var editor = document.getElementById('name-editor');
  var display = document.getElementById('name-display');
  if (name && name !== display.textContent.trim()) {
    try {
      var r = await fetchJSON('/api/client/name?id=' + encodeURIComponent(currentClientId) + '&name=' + encodeURIComponent(name));
      display.innerHTML = (r.name || r.id) + ' &nbsp;&#9998;';
    } catch (e) {
      setError('Failed to rename: ' + e.message);
    }
  }
  editor.style.display = 'none';
  display.style.display = 'inline';
}

function cancelName() {
  document.getElementById('name-editor').style.display = 'none';
  document.getElementById('name-display').style.display = 'inline';
}

function setError(msg) {
  const bar = document.getElementById('error-bar');
  if (msg && msg !== lastError) {
    bar.innerHTML = '<div class="error-msg">' + msg + '</div>';
    lastError = msg;
  } else if (!msg) {
    bar.innerHTML = '';
    lastError = '';
  }
}

async function fetchJSON(url) {
  const controller = new AbortController();
  const timer = setTimeout(function() { controller.abort(); }, 8000);
  try {
    const r = await fetch(url, { signal: controller.signal });
    clearTimeout(timer);
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return await r.json();
  } catch (e) {
    clearTimeout(timer);
    throw e;
  }
}

async function fetchStatus() {
  try {
    const d = await fetchJSON('/api/health');
    document.getElementById('status').innerHTML =
      '<span>Uptime: ' + d.uptime.toFixed(0) + 's</span>' +
      '<span>Heap: ' + (d.free_heap / 1024).toFixed(1) + 'KB</span>' +
      '<span>Clients: ' + d.clients_online + '</span>';
    setError('');
    fetchCount = 0;
  } catch (e) {
    document.getElementById('status').innerHTML = '<span class="offline">Disconnected</span>';
    fetchCount++;
    if (fetchCount >= 3) {
      setError('Cannot reach server (retried ' + fetchCount + ' times)');
    }
  }
}

function onTzSelectChange() {
  const select = document.getElementById('tz-select');
  const input = document.getElementById('tz-input');
  if (select.value === 'custom') {
    input.style.display = 'inline';
    input.value = currentTimezone;
  } else {
    input.style.display = 'none';
    input.value = select.value;
  }
}

async function loadTimezone() {
  try {
    const d = await fetchJSON('/api/timezone');
    currentTimezone = d.timezone || 'WET0WEST,M3.5.0/1,M10.5.0/2';
    const select = document.getElementById('tz-select');
    const input = document.getElementById('tz-input');
    let matched = false;
    for (var i = 0; i < select.options.length; i++) {
      if (select.options[i].value === currentTimezone) {
        select.selectedIndex = i;
        matched = true;
        break;
      }
    }
    if (!matched) {
      select.value = 'custom';
      input.style.display = 'inline';
    } else {
      input.style.display = 'none';
    }
    input.value = currentTimezone;
  } catch (e) {
    setError('Cannot load timezone: ' + e.message);
  }
}

async function saveTimezone() {
  const select = document.getElementById('tz-select');
  const input = document.getElementById('tz-input');
  const btn = document.getElementById('tz-save');
  const status = document.getElementById('tz-status');
  const value = (select.value === 'custom') ? input.value.trim() : select.value;
  if (!value) {
    status.textContent = 'Invalid timezone';
    status.style.color = '#f44336';
    return;
  }
  btn.disabled = true;
  status.textContent = 'Saving...';
  status.style.color = '#888';
  try {
    const d = await fetchJSON('/api/timezone?value=' + encodeURIComponent(value));
    currentTimezone = d.timezone;
    status.textContent = 'Saved: ' + d.timezone;
    status.style.color = '#4CAF50';
  } catch (e) {
    status.textContent = 'Failed: ' + e.message;
    status.style.color = '#f44336';
  } finally {
    btn.disabled = false;
  }
}

function formatTime(ts) {
  const dt = new Date(ts * 1000);
  if (isNaN(dt.getTime())) return 'N/A';
  try {
    return dt.toLocaleTimeString('en-GB', { timeZone: currentTimezone });
  } catch (e) {
    return dt.toLocaleTimeString();
  }
}

async function loadSyncInterval() {
  try {
    const d = await fetchJSON('/api/sync-interval');
    document.getElementById('sync-input').value = d.sync_interval;
  } catch (e) {
    setError('Cannot load sync interval: ' + e.message);
  }
}

async function saveSyncInterval() {
  const input = document.getElementById('sync-input');
  const btn = document.getElementById('sync-save');
  const status = document.getElementById('sync-status');
  const value = parseInt(input.value);
  if (!value || value < 60 || value > 86400) {
    status.textContent = 'Invalid value (60-86400)';
    status.style.color = '#f44336';
    return;
  }
  btn.disabled = true;
  status.textContent = 'Saving...';
  status.style.color = '#888';
  try {
    const d = await fetchJSON('/api/sync-interval?value=' + value);
    status.textContent = 'Saved: ' + d.sync_interval + ' sec';
    status.style.color = '#4CAF50';
  } catch (e) {
    status.textContent = 'Failed: ' + e.message;
    status.style.color = '#f44336';
  } finally {
    btn.disabled = false;
  }
}

async function fetchClients() {
  try {
    const clients = await fetchJSON('/api/clients');
    const tbody = document.querySelector('#clients tbody');
    tbody.innerHTML = '';
    if (clients.length === 0) {
      tbody.innerHTML = '<tr><td colspan="6"><span class="waiting">Waiting for client data...</span></td></tr>';
      return;
    }
    clients.forEach(function(c) {
      const tr = document.createElement('tr');
      tr.onclick = function() { showDetail(c.id); };
      var statusClass = c.online ? 'online' : 'offline';
      var statusText = c.online ? 'ONLINE' : 'OFFLINE';
      tr.innerHTML =
        '<td>' + (c.name || c.id) + '</td>' +
        '<td style="color:#888;font-size:0.85em">' + (c.name ? c.id : '') + '</td>' +
        '<td class="' + statusClass + '">' + statusText + '</td>' +
        '<td>' + c.records + '</td>' +
        '<td>' + formatTime(c.last_seen) + '</td>' +
        '<td>' + (c.ip || 'N/A') + '</td>';
      tbody.appendChild(tr);
    });
  } catch (e) {
    setError('Cannot fetch clients: ' + e.message);
  }
}

function drawAllCharts() {
  if (!currentData || currentData.length === 0) return;
  var since = Math.floor(Date.now() / 1000) - currentTimeRange * 3600;
  var filtered = currentData.filter(function(d) { return d.timestamp >= since; });
  if (filtered.length < 2) {
    var ids = ['ch-temp', 'ch-hum', 'ch-eco2', 'ch-tvoc', 'ch-aqi'];
    ids.forEach(function(id) {
      var canvas = document.getElementById(id);
      var ctx = canvas.getContext('2d');
      if (canvas.offsetWidth > 0) canvas.width = canvas.offsetWidth;
      canvas.height = 180;
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = '#888';
      ctx.font = '14px monospace';
      ctx.textAlign = 'center';
      ctx.fillText('Insufficient data for selected range', canvas.width / 2, 95);
    });
    return;
  }
  drawChart('ch-temp', filtered, 'temp', 'Temperature', '°C', '#4CAF50');
  drawChart('ch-hum', filtered, 'hum', 'Humidity', '%', '#2196F3');
  drawChart('ch-eco2', filtered, 'eco2', 'eCO2', ' ppm', '#FFC107');
  drawChart('ch-tvoc', filtered, 'tvoc', 'TVOC', ' ppb', '#FF9800');
  drawChart('ch-aqi', filtered, 'aqi', 'AQI', '', '#9C27B0');
}

function drawChart(canvasId, data, key, label, unit, color) {
  var canvas = document.getElementById(canvasId);
  if (!canvas) return;
  var ctx = canvas.getContext('2d');
  if (canvas.offsetWidth > 0) canvas.width = canvas.offsetWidth;
  canvas.height = 180;
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  var pad = 50;
  var w = canvas.width - pad * 2;
  var h = canvas.height - pad * 2;
  if (w < 20 || h < 20) return;

  var values = data.map(function(d) { return d[key]; });
  var max = Math.max.apply(null, values);
  var min = Math.min.apply(null, values);
  if (min === max) { min = min - 1; max = max + 1; }
  var range = max - min;

  var since = Math.floor(Date.now() / 1000) - currentTimeRange * 3600;
  var timeEnd = since + currentTimeRange * 3600;

  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.beginPath();
  for (var i = 0; i < data.length; i++) {
    var x = pad + ((data[i].timestamp - since) / (currentTimeRange * 3600)) * w;
    var y = pad + h - ((data[i][key] - min) / range) * h;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();

  ctx.fillStyle = '#888';
  ctx.font = '12px monospace';
  if (Number.isInteger(values[0])) {
    ctx.fillText(max.toFixed(0) + unit, 5, pad + 12);
    ctx.fillText(min.toFixed(0) + unit, 5, pad + h);
  } else {
    ctx.fillText(max.toFixed(1) + unit, 5, pad + 12);
    ctx.fillText(min.toFixed(1) + unit, 5, pad + h);
  }

  ctx.strokeStyle = '#555';
  ctx.lineWidth = 1;
  ctx.setLineDash([4, 4]);
  ctx.beginPath();
  ctx.moveTo(pad, pad + h / 2);
  ctx.lineTo(pad + w, pad + h / 2);
  ctx.stroke();
  ctx.setLineDash([]);
}

async function fetchData(id, rangeHours) {
  var since = Math.floor(Date.now() / 1000) - rangeHours * 3600;
  if (rangeHours >= 12) {
    var bucket = 300;
    if (rangeHours >= 24) { bucket = 900; }
    return await fetchJSON('/api/data/aggregated?id=' + encodeURIComponent(id) + '&since=' + since + '&bucket=' + bucket);
  }
  return await fetchJSON('/api/data?id=' + encodeURIComponent(id) + '&since=' + since);
}

async function showDetail(id) {
  currentClientId = id;
  document.getElementById('detail').style.display = 'block';
  document.getElementById('metrics').innerHTML = '<span class="loading">Loading data...</span>';
  currentData = null;

  try {
    currentData = await fetchData(id, currentTimeRange);
    var clientPromise = fetchJSON('/api/client?id=' + encodeURIComponent(id));
    var d = await clientPromise;

    document.getElementById('detail-title').innerHTML = '<span class="name-display" id="name-display" onclick="editName()">' + (d.name || d.id) + ' &nbsp;&#9998;</span><span id="name-editor" style="display:none"></span>';

    var latest = currentData.length > 0 ? currentData[currentData.length - 1] : null;
    var metrics = document.getElementById('metrics');

    if (latest) {
      metrics.innerHTML =
        '<div class="metric"><div class="label">Temperature</div><div class="value">' + latest.temp.toFixed(1) + '&deg;C</div></div>' +
        '<div class="metric"><div class="label">Humidity</div><div class="value">' + latest.hum.toFixed(1) + '%</div></div>' +
        '<div class="metric"><div class="label">eCO2</div><div class="value">' + latest.eco2 + ' ppm</div></div>' +
        '<div class="metric"><div class="label">TVOC</div><div class="value">' + latest.tvoc + ' ppb</div></div>' +
        '<div class="metric"><div class="label">AQI</div><div class="value aqi-' + latest.aqi + '">' + latest.aqi + '</div></div>';
    } else {
      metrics.innerHTML = '<span class="waiting">No data yet for ' + id + '</span>';
    }

    drawAllCharts();
  } catch (e) {
    document.getElementById('metrics').innerHTML = '<span class="error-msg">Failed to load data for ' + id + ': ' + e.message + '</span>';
  }
}

async function setTimeRange(hours) {
  currentTimeRange = hours;
  var btns = document.querySelectorAll('#controls button');
  for (var i = 0; i < btns.length; i++) {
    btns[i].className = '';
  }
  for (var i = 0; i < btns.length; i++) {
    if (parseInt(btns[i].textContent) === hours) {
      btns[i].className = 'active';
      break;
    }
  }
  if (currentClientId) {
    try {
      currentData = await fetchData(currentClientId, hours);
      drawAllCharts();
    } catch (e) {
      setError('Failed to load data: ' + e.message);
    }
  }
}

setInterval(function() { fetchStatus(); fetchClients(); }, 5000);
fetchStatus();
fetchClients();
loadSyncInterval();
loadTimezone();
</script>
</body>
</html>
)rawliteral";

#endif
