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
canvas { width: 100%; height: 300px; background: #333; border-radius: 6px; display: block; }
.aqi-1 { color: #4CAF50; }
.aqi-2 { color: #FFC107; }
.aqi-3 { color: #FF9800; }
.aqi-4 { color: #f44336; }
.aqi-5 { color: #9C27B0; }
.error-msg { color: #f44336; padding: 10px; }
.loading { color: #888; }
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
<canvas id="chart"></canvas>
</div>
<script>
let currentClient = null;
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
  const timer = setTimeout(() => controller.abort(), 5000);
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

async function showDetail(id) {
  currentClient = id;
  document.getElementById('detail').style.display = 'block';
  document.getElementById('metrics').innerHTML = '<span class="loading">Loading data...</span>';

  try {
    const d = await fetchJSON('/api/client?id=' + encodeURIComponent(id));
    document.getElementById('detail-title').textContent = d.name || d.id;
    const data = d.data || [];
    const latest = data.length > 0 ? data[data.length - 1] : null;
    const metrics = document.getElementById('metrics');

    if (latest) {
      metrics.innerHTML =
        '<div class="metric"><div class="label">Temperature</div><div class="value">' + latest.temp.toFixed(1) + '&deg;C</div></div>' +
        '<div class="metric"><div class="label">Humidity</div><div class="value">' + latest.hum.toFixed(1) + '%</div></div>' +
        '<div class="metric"><div class="label">eCO2</div><div class="value">' + latest.eco2 + ' ppm</div></div>' +
        '<div class="metric"><div class="label">TVOC</div><div class="value">' + latest.tvoc + ' ppb</div></div>' +
        '<div class="metric"><div class="label">AQI</div><div class="value aqi-' + latest.aqi + '">' + latest.aqi + '</div></div>';
      drawChart(data);
    } else {
      metrics.innerHTML = '<span class="waiting">No data yet for ' + id + '</span>';
      const canvas = document.getElementById('chart');
      const ctx = canvas.getContext('2d');
      ctx.clearRect(0, 0, canvas.width, canvas.height);
    }
  } catch (e) {
    document.getElementById('metrics').innerHTML = '<span class="error-msg">Failed to load data for ' + id + '</span>';
  }
}

function drawChart(data) {
  const canvas = document.getElementById('chart');
  const ctx = canvas.getContext('2d');
  if (canvas.offsetWidth > 0) {
    canvas.width = canvas.offsetWidth;
  }
  canvas.height = 300;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (data.length < 2) {
    ctx.fillStyle = '#888';
    ctx.font = '14px monospace';
    ctx.textAlign = 'center';
    ctx.fillText('Need at least 2 data points for chart', canvas.width / 2, 150);
    return;
  }
  const pad = 40;
  const w = canvas.width - pad * 2;
  const h = canvas.height - pad * 2;
  const maxTemp = Math.max.apply(null, data.map(function(d) { return d.temp; }));
  const minTemp = Math.min.apply(null, data.map(function(d) { return d.temp; }));
  const range = maxTemp - minTemp || 1;

  ctx.strokeStyle = '#4CAF50';
  ctx.lineWidth = 2;
  ctx.beginPath();
  data.forEach(function(d, i) {
    const x = pad + (i / (data.length - 1)) * w;
    const y = pad + h - ((d.temp - minTemp) / range) * h;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();

  ctx.fillStyle = '#888';
  ctx.font = '12px monospace';
  ctx.fillText(maxTemp.toFixed(1) + '°C', 5, pad);
  ctx.fillText(minTemp.toFixed(1) + '°C', 5, pad + h);
}

setInterval(function() { fetchStatus(); fetchClients(); }, 5000);
fetchStatus();
fetchClients();
</script>
</body>
</html>
)rawliteral";

#endif
