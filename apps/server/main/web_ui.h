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
@media (max-width: 600px) {
  body { padding: 10px; }
  .metrics { grid-template-columns: repeat(2, 1fr); }
}
</style>
</head>
<body>
<h1>AirMon Server</h1>
<div class="status" id="status"><span class="loading">Connecting...</span></div>
<div id="error-bar"></div>
<table id="clients">
<thead><tr><th>ID</th><th>Status</th><th>Records</th><th>Last Seen</th><th>IP</th></tr></thead>
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

async function fetchClients() {
  try {
    const clients = await fetchJSON('/api/clients');
    const tbody = document.querySelector('#clients tbody');
    tbody.innerHTML = '';
    if (clients.length === 0) {
      tbody.innerHTML = '<tr><td colspan="5"><span class="waiting">Waiting for client data...</span></td></tr>';
      return;
    }
    clients.forEach(function(c) {
      const tr = document.createElement('tr');
      tr.onclick = function() { showDetail(c.id); };
      const dt = new Date(c.last_seen * 1000);
      var statusClass = c.online ? 'online' : 'offline';
      var statusText = c.online ? 'ONLINE' : 'OFFLINE';
      tr.innerHTML =
        '<td>' + c.id + '</td>' +
        '<td class="' + statusClass + '">' + statusText + '</td>' +
        '<td>' + c.records + '</td>' +
        '<td>' + (isNaN(dt.getTime()) ? 'N/A' : dt.toLocaleTimeString()) + '</td>' +
        '<td>' + (c.ip || 'N/A') + '</td>';
      tbody.appendChild(tr);
    });
  } catch (e) {
    setError('Cannot fetch clients: ' + e.message);
  }
}

function setTimeRange(hours) {
  currentTimeRange = hours;
  var btns = document.querySelectorAll('#controls button');
  for (var i = 0; i < btns.length; i++) {
    btns[i].className = '';
  }
  var active = null;
  for (var i = 0; i < btns.length; i++) {
    if (parseInt(btns[i].textContent) === hours) {
      btns[i].className = 'active';
      break;
    }
  }
  drawAllCharts();
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

async function showDetail(id) {
  currentClientId = id;
  document.getElementById('detail').style.display = 'block';
  document.getElementById('metrics').innerHTML = '<span class="loading">Loading data...</span>';
  currentData = null;

  try {
    var since = Math.floor(Date.now() / 1000) - 24 * 3600;
    var dataPromise = fetchJSON('/api/data?id=' + encodeURIComponent(id) + '&since=' + since);
    var clientPromise = fetchJSON('/api/client?id=' + encodeURIComponent(id));

    var results = await Promise.all([dataPromise, clientPromise]);
    var data = results[0];
    var d = results[1];

    currentData = data;
    document.getElementById('detail-title').textContent = d.name || d.id;

    var latest = data.length > 0 ? data[data.length - 1] : null;
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

    setTimeRange(currentTimeRange);
  } catch (e) {
    document.getElementById('metrics').innerHTML = '<span class="error-msg">Failed to load data for ' + id + ': ' + e.message + '</span>';
  }
}

setInterval(function() { fetchStatus(); fetchClients(); }, 5000);
fetchStatus();
fetchClients();
</script>
</body>
</html>
)rawliteral";

#endif
