#pragma once

#include <Arduino.h>

// Main dashboard page HTML
const char HTML_STATUS_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MODBUS Proxy</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#1a1a2e;color:#eee;padding:15px;font-size:14px}
.container{max-width:800px;margin:0 auto}
h1{color:#0ff;margin-bottom:20px;font-size:1.5em;display:flex;align-items:center;justify-content:space-between}
.indicators{display:flex;gap:10px}
.indicator{display:flex;align-items:center;gap:5px;padding:5px 10px;background:#16213e;border-radius:20px;font-size:12px}
.indicator .dot{width:10px;height:10px;border-radius:50%;background:#666}
.indicator .dot.ok{background:#4caf50;box-shadow:0 0 8px #4caf50}
.indicator .dot.warn{background:#ff9800;box-shadow:0 0 8px #ff9800}
.indicator .dot.error{background:#f44336}
.card{background:#16213e;border-radius:8px;padding:15px;margin-bottom:15px;border:1px solid #0f3460}
.card.highlight{background:linear-gradient(135deg,#1a3a5c 0%,#16213e 100%);border:2px solid #0ff;text-align:center}
.card h2{color:#e94560;font-size:1.1em;margin-bottom:10px;border-bottom:1px solid #0f3460;padding-bottom:8px}
.card.highlight h2{border-bottom:none;color:#0ff;font-size:1em;margin-bottom:5px}
.big-value{font-size:3.5em;font-weight:bold;color:#0ff;font-family:monospace;margin:15px 0;text-shadow:0 0 20px rgba(0,255,255,0.5)}
.row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #0f346040}
.row:last-child{border-bottom:none}
.label{color:#aaa}
.value{color:#0ff;font-family:monospace;font-size:1.1em}
.power-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:15px;text-align:center}
.power-item{background:#0f3460;padding:15px;border-radius:8px}
.power-item .label{font-size:11px;text-transform:uppercase;margin-bottom:5px}
.power-item .value{font-size:1.4em;display:block}
nav{margin-bottom:20px;display:flex;gap:5px}
nav a{color:#aaa;text-decoration:none;padding:8px 16px;background:#0f3460;border-radius:5px;font-size:13px}
nav a:hover{background:#1a4a7a}
nav a.active{background:#0ff;color:#1a1a2e;font-weight:bold}
.update-time{text-align:right;color:#666;font-size:11px;margin-top:10px}
</style>
</head>
<body>
<div class="container">
<h1>
<span>MODBUS Proxy</span>
<div class="indicators">
<div class="indicator"><span class="dot" id="mqttDot"></span>MQTT</div>
<div class="indicator"><span class="dot" id="dtsuDot"></span>DTSU</div>
<div class="indicator"><span class="dot" id="sun2000Dot"></span>SUN2000</div>
</div>
</h1>
<nav>
<a href="/" class="active">Dashboard</a>
<a href="/status">Status</a>
<a href="/setup">Setup</a>
</nav>

<div class="card highlight">
<h2>Wallbox Power</h2>
<div class="big-value" id="wallboxPower">-- W</div>
</div>

<div class="card">
<h2>Power Readings</h2>
<div class="power-grid">
<div class="power-item">
<div class="label">DTSU Meter</div>
<span class="value" id="dtsuPower">--</span>
</div>
<div class="power-item">
<div class="label">Correction</div>
<span class="value" id="correction">--</span>
</div>
<div class="power-item">
<div class="label">SUN2000 Sees</div>
<span class="value" id="sun2000Power">--</span>
</div>
</div>
</div>

<div class="update-time">Last update: <span id="lastUpdate">--</span></div>
</div>

<script>
function updateStatus(){
  fetch('/api/status')
  .then(r=>r.json())
  .then(d=>{
    // Wallbox power
    let wbEl=document.getElementById('wallboxPower');
    wbEl.textContent=Math.abs(d.wallbox_power).toFixed(0)+' W';
    if(d.wallbox_power>1000)wbEl.style.color='#ff9800';
    else if(d.wallbox_power>0)wbEl.style.color='#4caf50';
    else wbEl.style.color='#0ff';

    // Power readings
    document.getElementById('dtsuPower').textContent=d.dtsu_power.toFixed(0)+' W';
    document.getElementById('correction').textContent=(d.correction_active?'+':'')+d.wallbox_power.toFixed(0)+' W';
    document.getElementById('correction').style.color=d.correction_active?'#4caf50':'#666';
    document.getElementById('sun2000Power').textContent=d.sun2000_power.toFixed(0)+' W';

    // Status indicators
    document.getElementById('mqttDot').className='dot '+(d.mqtt_connected?'ok':'error');
    document.getElementById('dtsuDot').className='dot '+(d.dtsu_updates>0?'ok':'warn');
    document.getElementById('sun2000Dot').className='dot '+(d.correction_active||d.dtsu_updates>0?'ok':'warn');

    document.getElementById('lastUpdate').textContent=new Date().toLocaleTimeString();
  })
  .catch(e=>console.error('Status fetch error:',e));
}
updateStatus();
setInterval(updateStatus,2000);
</script>
</body>
</html>
)rawliteral";

// Status page HTML
const char HTML_INFO_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MODBUS Proxy - Status</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#1a1a2e;color:#eee;padding:15px;font-size:14px}
.container{max-width:800px;margin:0 auto}
h1{color:#0ff;margin-bottom:20px;font-size:1.5em}
.card{background:#16213e;border-radius:8px;padding:15px;margin-bottom:15px;border:1px solid #0f3460}
.card h2{color:#e94560;font-size:1.1em;margin-bottom:10px;border-bottom:1px solid #0f3460;padding-bottom:8px}
.row{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #0f346040}
.row:last-child{border-bottom:none}
.label{color:#aaa}
.value{color:#0ff;font-family:monospace}
.value.ok{color:#4caf50}
.value.warn{color:#ff9800}
.value.error{color:#f44336}
nav{margin-bottom:20px;display:flex;gap:5px}
nav a{color:#aaa;text-decoration:none;padding:8px 16px;background:#0f3460;border-radius:5px;font-size:13px}
nav a:hover{background:#1a4a7a}
nav a.active{background:#0ff;color:#1a1a2e;font-weight:bold}
.btn{background:#e94560;color:#fff;border:none;padding:10px 20px;border-radius:5px;cursor:pointer;font-size:14px;margin:5px}
.btn:hover{background:#c73e54}
.btn.secondary{background:#0f3460}
.btn.secondary:hover{background:#1a4a7a}
</style>
</head>
<body>
<div class="container">
<h1>Status</h1>
<nav>
<a href="/">Dashboard</a>
<a href="/status" class="active">Status</a>
<a href="/setup">Setup</a>
</nav>

<div class="card">
<h2>System</h2>
<div class="row"><span class="label">Uptime</span><span class="value" id="uptime">--</span></div>
<div class="row"><span class="label">Free Heap</span><span class="value" id="heap">--</span></div>
<div class="row"><span class="label">Min Free Heap</span><span class="value" id="minHeap">--</span></div>
</div>

<div class="card">
<h2>WiFi</h2>
<div class="row"><span class="label">Status</span><span class="value" id="wifiStatus">--</span></div>
<div class="row"><span class="label">SSID</span><span class="value" id="wifiSSID">--</span></div>
<div class="row"><span class="label">IP Address</span><span class="value" id="wifiIP">--</span></div>
<div class="row"><span class="label">RSSI</span><span class="value" id="wifiRSSI">--</span></div>
</div>

<div class="card">
<h2>MQTT</h2>
<div class="row"><span class="label">Status</span><span class="value" id="mqttStatus">--</span></div>
<div class="row"><span class="label">Server</span><span class="value" id="mqttServer">--</span></div>
<div class="row"><span class="label">Reconnects</span><span class="value" id="mqttReconnects">--</span></div>
</div>

<div class="card">
<h2>MODBUS Statistics</h2>
<div class="row"><span class="label">DTSU Updates</span><span class="value" id="dtsuUpdates">--</span></div>
<div class="row"><span class="label">Wallbox Updates</span><span class="value" id="wallboxUpdates">--</span></div>
<div class="row"><span class="label">Wallbox Errors</span><span class="value" id="wallboxErrors">--</span></div>
<div class="row"><span class="label">Proxy Errors</span><span class="value" id="proxyErrors">--</span></div>
</div>

<div class="card">
<button class="btn secondary" onclick="location.reload()">Refresh</button>
<button class="btn" onclick="restart()">Restart Device</button>
</div>
</div>

<script>
function formatUptime(ms){
  let s=Math.floor(ms/1000);
  let d=Math.floor(s/86400);s%=86400;
  let h=Math.floor(s/3600);s%=3600;
  let m=Math.floor(s/60);s%=60;
  return d+'d '+h+'h '+m+'m '+s+'s';
}
function formatBytes(b){return (b/1024).toFixed(1)+' KB';}
function updateStatus(){
  fetch('/api/status')
  .then(r=>r.json())
  .then(d=>{
    document.getElementById('uptime').textContent=formatUptime(d.uptime);
    document.getElementById('heap').textContent=formatBytes(d.free_heap);
    document.getElementById('minHeap').textContent=formatBytes(d.min_free_heap);
    document.getElementById('wifiStatus').textContent=d.wifi_connected?'Connected':'Disconnected';
    document.getElementById('wifiStatus').className='value '+(d.wifi_connected?'ok':'error');
    document.getElementById('wifiSSID').textContent=d.wifi_ssid||'--';
    document.getElementById('wifiIP').textContent=d.wifi_ip||'--';
    document.getElementById('wifiRSSI').textContent=d.wifi_rssi+' dBm';
    document.getElementById('mqttStatus').textContent=d.mqtt_connected?'Connected':'Disconnected';
    document.getElementById('mqttStatus').className='value '+(d.mqtt_connected?'ok':'error');
    document.getElementById('mqttServer').textContent=d.mqtt_host+':'+d.mqtt_port;
    document.getElementById('mqttReconnects').textContent=d.mqtt_reconnects;
    document.getElementById('dtsuUpdates').textContent=d.dtsu_updates;
    document.getElementById('wallboxUpdates').textContent=d.wallbox_updates;
    document.getElementById('wallboxErrors').textContent=d.wallbox_errors;
    document.getElementById('proxyErrors').textContent=d.proxy_errors;
  })
  .catch(e=>console.error('Status fetch error:',e));
}
function restart(){
  if(confirm('Restart the device?')){
    fetch('/api/restart',{method:'POST'}).then(()=>alert('Device restarting...'));
  }
}
updateStatus();
setInterval(updateStatus,5000);
</script>
</body>
</html>
)rawliteral";

// Setup/Configuration page HTML
const char HTML_CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MODBUS Proxy - Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#1a1a2e;color:#eee;padding:15px;font-size:14px}
.container{max-width:600px;margin:0 auto}
h1{color:#0ff;margin-bottom:20px;font-size:1.5em}
.card{background:#16213e;border-radius:8px;padding:15px;margin-bottom:15px;border:1px solid #0f3460}
.card h2{color:#e94560;font-size:1.1em;margin-bottom:15px;border-bottom:1px solid #0f3460;padding-bottom:8px}
.form-group{margin-bottom:15px}
label{display:block;color:#aaa;margin-bottom:5px;font-size:13px}
input,select{width:100%;padding:10px;border:1px solid #0f3460;border-radius:5px;background:#1a1a2e;color:#fff;font-size:14px}
input:focus,select:focus{outline:none;border-color:#0ff}
.btn{background:#e94560;color:#fff;border:none;padding:12px 24px;border-radius:5px;cursor:pointer;font-size:14px;width:100%;margin-top:10px}
.btn:hover{background:#c73e54}
.btn.secondary{background:#0f3460}
.btn.secondary:hover{background:#1a4a7a}
nav{margin-bottom:20px;display:flex;gap:5px}
nav a{color:#aaa;text-decoration:none;padding:8px 16px;background:#0f3460;border-radius:5px;font-size:13px}
nav a:hover{background:#1a4a7a}
nav a.active{background:#0ff;color:#1a1a2e;font-weight:bold}
.msg{padding:10px;border-radius:5px;margin-bottom:15px;display:none}
.msg.success{background:#4caf5020;border:1px solid #4caf50;color:#4caf50;display:block}
.msg.error{background:#f4433620;border:1px solid #f44336;color:#f44336;display:block}
.inline{display:flex;gap:10px}
.inline input{flex:1}
.inline input:last-child{max-width:100px}
.toggle-row{display:flex;justify-content:space-between;align-items:center;padding:10px 0}
.toggle{position:relative;display:inline-block;width:50px;height:26px}
.toggle input{opacity:0;width:0;height:0}
.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#0f3460;border-radius:26px;transition:.3s}
.slider:before{position:absolute;content:"";height:20px;width:20px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}
input:checked+.slider{background:#4caf50}
input:checked+.slider:before{transform:translateX(24px)}
.toggle-label{color:#eee}
</style>
</head>
<body>
<div class="container">
<h1>Setup</h1>
<nav>
<a href="/">Dashboard</a>
<a href="/status">Status</a>
<a href="/setup" class="active">Setup</a>
</nav>

<div id="message" class="msg"></div>

<div class="card">
<h2>Debug Mode</h2>
<div class="toggle-row">
<span class="toggle-label">Verbose MQTT Logging</span>
<label class="toggle">
<input type="checkbox" id="debugToggle" onchange="toggleDebug()">
<span class="slider"></span>
</label>
</div>
</div>

<div class="card">
<h2>MQTT Settings</h2>
<form id="mqttForm">
<div class="form-group inline">
<input type="text" id="mqttHost" placeholder="MQTT Host" required>
<input type="number" id="mqttPort" placeholder="Port" value="1883" required>
</div>
<div class="form-group">
<input type="text" id="mqttUser" placeholder="Username">
</div>
<div class="form-group">
<input type="password" id="mqttPass" placeholder="Password">
</div>
<button type="submit" class="btn">Save MQTT Settings</button>
</form>
</div>

<div class="card">
<h2>Wallbox Topic</h2>
<form id="wbForm">
<div class="form-group">
<input type="text" id="wbTopic" placeholder="Wallbox MQTT Topic" required>
</div>
<button type="submit" class="btn">Save Topic</button>
</form>
</div>

<div class="card">
<h2>Log Level</h2>
<form id="logForm">
<div class="form-group">
<select id="logLevel">
<option value="0">DEBUG - All messages</option>
<option value="1">INFO - Info and above</option>
<option value="2">WARN - Warnings and errors</option>
<option value="3">ERROR - Errors only</option>
</select>
</div>
<button type="submit" class="btn">Save Log Level</button>
</form>
</div>

<div class="card">
<h2>Factory Reset</h2>
<p style="color:#aaa;margin-bottom:15px;font-size:13px">Reset all settings to defaults. Device will restart.</p>
<button class="btn secondary" onclick="factoryReset()">Factory Reset</button>
</div>
</div>

<script>
function showMsg(msg,isError){
  let el=document.getElementById('message');
  el.textContent=msg;
  el.className='msg '+(isError?'error':'success');
  setTimeout(()=>{el.className='msg';},5000);
}
function loadConfig(){
  fetch('/api/config')
  .then(r=>r.json())
  .then(d=>{
    document.getElementById('mqttHost').value=d.mqtt_host||'';
    document.getElementById('mqttPort').value=d.mqtt_port||1883;
    document.getElementById('mqttUser').value=d.mqtt_user||'';
    document.getElementById('wbTopic').value=d.wallbox_topic||'';
    document.getElementById('logLevel').value=d.log_level||2;
  });
  fetch('/api/status')
  .then(r=>r.json())
  .then(d=>{
    document.getElementById('debugToggle').checked=d.debug_mode;
  });
}
function toggleDebug(){
  let enabled=document.getElementById('debugToggle').checked;
  fetch('/api/debug',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:enabled})})
  .then(r=>r.json())
  .then(d=>{
    if(d.status==='ok')showMsg('Debug mode '+(enabled?'enabled':'disabled'),false);
    else showMsg('Failed to toggle debug mode',true);
  })
  .catch(e=>showMsg('Error: '+e,true));
}
document.getElementById('mqttForm').onsubmit=function(e){
  e.preventDefault();
  fetch('/api/config',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({
      type:'mqtt',
      host:document.getElementById('mqttHost').value,
      port:parseInt(document.getElementById('mqttPort').value),
      user:document.getElementById('mqttUser').value,
      pass:document.getElementById('mqttPass').value
    })
  })
  .then(r=>r.json())
  .then(d=>{showMsg(d.status==='ok'?'MQTT settings saved':'Save failed',d.status!=='ok');})
  .catch(e=>showMsg('Error: '+e,true));
};
document.getElementById('wbForm').onsubmit=function(e){
  e.preventDefault();
  fetch('/api/config',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({type:'wallbox',topic:document.getElementById('wbTopic').value})
  })
  .then(r=>r.json())
  .then(d=>{showMsg(d.status==='ok'?'Wallbox topic saved':'Save failed',d.status!=='ok');})
  .catch(e=>showMsg('Error: '+e,true));
};
document.getElementById('logForm').onsubmit=function(e){
  e.preventDefault();
  fetch('/api/config',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({type:'loglevel',level:parseInt(document.getElementById('logLevel').value)})
  })
  .then(r=>r.json())
  .then(d=>{showMsg(d.status==='ok'?'Log level saved':'Save failed',d.status!=='ok');})
  .catch(e=>showMsg('Error: '+e,true));
};
function factoryReset(){
  if(confirm('Reset all settings to defaults? Device will restart.')){
    fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:'reset'})})
    .then(r=>r.json())
    .then(d=>{showMsg('Factory reset complete. Restarting...',false);setTimeout(()=>location.reload(),3000);})
    .catch(e=>showMsg('Error: '+e,true));
  }
}
loadConfig();
</script>
</body>
</html>
)rawliteral";

// Captive portal page HTML
const char HTML_PORTAL_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MODBUS Proxy Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#1a1a2e;color:#eee;padding:20px;font-size:14px;min-height:100vh;display:flex;align-items:center;justify-content:center}
.container{max-width:400px;width:100%}
h1{color:#0ff;margin-bottom:10px;font-size:1.5em;text-align:center}
.subtitle{color:#aaa;text-align:center;margin-bottom:25px}
.card{background:#16213e;border-radius:8px;padding:20px;border:1px solid #0f3460}
.form-group{margin-bottom:15px}
label{display:block;color:#aaa;margin-bottom:5px;font-size:13px}
input,select{width:100%;padding:12px;border:1px solid #0f3460;border-radius:5px;background:#1a1a2e;color:#fff;font-size:14px}
input:focus,select:focus{outline:none;border-color:#0ff}
.btn{background:#e94560;color:#fff;border:none;padding:14px 24px;border-radius:5px;cursor:pointer;font-size:14px;width:100%;margin-top:10px}
.btn:hover{background:#c73e54}
.btn:disabled{background:#666;cursor:not-allowed}
.msg{padding:10px;border-radius:5px;margin-bottom:15px;text-align:center}
.msg.success{background:#4caf5020;border:1px solid #4caf50;color:#4caf50}
.msg.error{background:#f4433620;border:1px solid #f44336;color:#f44336}
.network{padding:10px;margin:5px 0;background:#1a1a2e;border-radius:5px;cursor:pointer;display:flex;justify-content:space-between;align-items:center}
.network:hover{background:#0f3460}
.network.selected{border:1px solid #0ff}
.rssi{color:#888;font-size:12px}
.scan-btn{background:#0f3460;margin-bottom:15px}
.scan-btn:hover{background:#1a4a7a}
#networks{max-height:200px;overflow-y:auto;margin-bottom:15px}
.timer{text-align:center;color:#888;margin-top:15px;font-size:12px}
</style>
</head>
<body>
<div class="container">
<h1>WiFi Setup</h1>
<p class="subtitle">MODBUS Proxy Configuration</p>

<div class="card">
<div id="message" class="msg" style="display:none"></div>

<button class="btn scan-btn" onclick="scanNetworks()">Scan Networks</button>

<div id="networks"></div>

<form id="wifiForm">
<div class="form-group">
<label>WiFi Network (SSID)</label>
<input type="text" id="ssid" placeholder="Enter or select network" required>
</div>
<div class="form-group">
<label>Password</label>
<input type="password" id="password" placeholder="WiFi password">
</div>
<button type="submit" class="btn" id="saveBtn">Save and Connect</button>
</form>

<div class="timer">Portal timeout: <span id="timeout">5:00</span></div>
</div>
</div>

<script>
let selectedSSID='';
let timeoutSeconds=300;

function showMsg(msg,isError){
  let el=document.getElementById('message');
  el.textContent=msg;
  el.className='msg '+(isError?'error':'success');
  el.style.display='block';
}

function scanNetworks(){
  document.getElementById('networks').innerHTML='<div style="text-align:center;padding:20px;color:#888">Scanning...</div>';
  fetch('/api/scan')
  .then(r=>r.json())
  .then(d=>{
    let html='';
    if(d.networks&&d.networks.length>0){
      d.networks.forEach(n=>{
        let signal=n.rssi>-50?'Strong':n.rssi>-70?'Good':'Weak';
        html+='<div class="network" onclick="selectNetwork(\''+n.ssid.replace(/'/g,"\\'")+'\')">';
        html+='<span>'+n.ssid+(n.encrypted?' [Protected]':'')+'</span>';
        html+='<span class="rssi">'+signal+' ('+n.rssi+' dBm)</span>';
        html+='</div>';
      });
    }else{
      html='<div style="text-align:center;padding:20px;color:#888">No networks found</div>';
    }
    document.getElementById('networks').innerHTML=html;
  })
  .catch(e=>{
    document.getElementById('networks').innerHTML='<div style="text-align:center;padding:20px;color:#f44336">Scan failed</div>';
  });
}

function selectNetwork(ssid){
  selectedSSID=ssid;
  document.getElementById('ssid').value=ssid;
  document.querySelectorAll('.network').forEach(el=>{
    el.classList.remove('selected');
    if(el.querySelector('span').textContent.startsWith(ssid)){
      el.classList.add('selected');
    }
  });
}

document.getElementById('wifiForm').onsubmit=function(e){
  e.preventDefault();
  let btn=document.getElementById('saveBtn');
  btn.disabled=true;
  btn.textContent='Saving...';

  fetch('/api/wifi',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({
      ssid:document.getElementById('ssid').value,
      password:document.getElementById('password').value
    })
  })
  .then(r=>r.json())
  .then(d=>{
    if(d.status==='ok'){
      showMsg('WiFi saved! Device will restart and connect...',false);
      setTimeout(()=>{
        showMsg('Restarting device...',false);
      },2000);
    }else{
      showMsg(d.message||'Save failed',true);
      btn.disabled=false;
      btn.textContent='Save and Connect';
    }
  })
  .catch(e=>{
    showMsg('Error: '+e,true);
    btn.disabled=false;
    btn.textContent='Save and Connect';
  });
};

function updateTimer(){
  if(timeoutSeconds>0){
    timeoutSeconds--;
    let m=Math.floor(timeoutSeconds/60);
    let s=timeoutSeconds%60;
    document.getElementById('timeout').textContent=m+':'+(s<10?'0':'')+s;
    setTimeout(updateTimer,1000);
  }else{
    document.getElementById('timeout').textContent='Restarting...';
  }
}

scanNetworks();
updateTimer();
</script>
</body>
</html>
)rawliteral";

// Success redirect page
const char HTML_REDIRECT_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta http-equiv="refresh" content="0;url=http://192.168.4.1/">
<title>Redirecting...</title>
</head>
<body>
<p>Redirecting to configuration portal...</p>
</body>
</html>
)rawliteral";
