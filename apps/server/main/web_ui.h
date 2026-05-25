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
.card { background: #2a2a2a; padding: 20px; border-radius: 8px; margin-top: 20px; display: none; }
.card h2 { color: #4CAF50; margin-bottom: 15px; }
.metrics { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 15px; margin-bottom: 20px; }
.metric { background: #333; padding: 15px; border-radius: 6px; text-align: center; }
.metric .label { font-size: 0.9em; color: #888; }
.metric .value { font-size: 1.8em; font-weight: bold; margin-top: 5px; }
canvas { width: 100%; height: 300px; background: #333; border-radius: 6px; }
.aqi-1 { color: #4CAF50; }
.aqi-2 { color: #FFC107; }
.aqi-3 { color: #FF9800; }
.aqi-4 { color: #f44336; }
.aqi-5 { color: #9C27B0; }
</style>
</head>
<body>
<h1>AirMon Server</h1>
<div class="status" id="status">Loading...</div>
<table id="clients">
<thead><tr><th>ID</th><th>Name</th><th>Status</th><th>IP</th><th>Records</th><th>Last Seen</th></tr></thead>
<tbody></tbody>
</table>
<div class="card" id="detail">
<h2 id="detail-title"></h2>
<div class="metrics" id="metrics"></div>
<canvas id="chart"></canvas>
</div>
<script>
let currentClient = null;
async function fetchStatus() {
  const r = await fetch('/api/health');
  const d = await r.json();
  document.getElementById('status').innerHTML = 
    `<span>Uptime: ${d.uptime.toFixed(0)}s</span>` +
    `<span>Heap: ${(d.free_heap/1024).toFixed(1)}KB</span>` +
    `<span>Clients: ${d.clients_online}</span>`;
}
async function fetchClients() {
  const r = await fetch('/api/clients');
  const clients = await r.json();
  const tbody = document.querySelector('#clients tbody');
  tbody.innerHTML = '';
  clients.forEach(c => {
    const tr = document.createElement('tr');
    tr.onclick = () => showDetail(c.id);
    const dt = new Date(c.last_seen * 1000);
    tr.innerHTML = 
      `<td>${c.id}</td>` +
      `<td>${c.name || c.id}</td>` +
      `<td class="${c.online ? 'online' : 'offline'}">${c.online ? 'ONLINE' : 'OFFLINE'}</td>` +
      `<td>${c.ip}</td>` +
      `<td>${c.records}</td>` +
      `<td>${dt.toLocaleTimeString()}</td>`;
    tbody.appendChild(tr);
  });
}
async function showDetail(id) {
  currentClient = id;
  const r = await fetch(`/api/clients/${id}`);
  const d = await r.json();
  document.getElementById('detail').style.display = 'block';
  document.getElementById('detail-title').textContent = d.name || d.id;
  const latest = d.data.length > 0 ? d.data[d.data.length - 1] : null;
  const metrics = document.getElementById('metrics');
  if (latest) {
    metrics.innerHTML = 
      `<div class="metric"><div class="label">Temperature</div><div class="value">${latest.temp.toFixed(1)}°C</div></div>` +
      `<div class="metric"><div class="label">Humidity</div><div class="value">${latest.hum.toFixed(1)}%</div></div>` +
      `<div class="metric"><div class="label">eCO2</div><div class="value">${latest.eco2} ppm</div></div>` +
      `<div class="metric"><div class="label">TVOC</div><div class="value">${latest.tvoc} ppb</div></div>` +
      `<div class="metric"><div class="label">AQI</div><div class="value aqi-${latest.aqi}">${latest.aqi}</div></div>`;
    drawChart(d.data);
  }
}
function drawChart(data) {
  const canvas = document.getElementById('chart');
  const ctx = canvas.getContext('2d');
  canvas.width = canvas.offsetWidth;
  canvas.height = 300;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (data.length < 2) return;
  const padding = 40;
  const w = canvas.width - padding * 2;
  const h = canvas.height - padding * 2;
  const maxTemp = Math.max(...data.map(d => d.temp));
  const minTemp = Math.min(...data.map(d => d.temp));
  const range = maxTemp - minTemp || 1;
  ctx.strokeStyle = '#4CAF50';
  ctx.lineWidth = 2;
  ctx.beginPath();
  data.forEach((d, i) => {
    const x = padding + (i / (data.length - 1)) * w;
    const y = padding + h - ((d.temp - minTemp) / range) * h;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
  ctx.fillStyle = '#888';
  ctx.font = '12px monospace';
  ctx.fillText(`${maxTemp.toFixed(1)}°C`, 5, padding);
  ctx.fillText(`${minTemp.toFixed(1)}°C`, 5, padding + h);
}
setInterval(() => { fetchStatus(); fetchClients(); }, 5000);
fetchStatus();
fetchClients();
</script>
</body>
</html>
)rawliteral";

#endif
