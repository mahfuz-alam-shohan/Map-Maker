#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <SD.h>

// ===== ESP32 PIN AND NETWORK CONFIG =====
const char* ssid = "ESP32-MapMaker";
const char* password = "12345678";
#define CS_PIN     5
#define MOSI_PIN   23
#define MISO_PIN   19
#define SCK_PIN    18
#define GPS_TX_PIN 16

HardwareSerial GPSserial(1);
TinyGPSPlus gps;

WebServer server(80);
WebSocketsServer webSocket(81);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="bn">
<head>
  <meta charset="UTF-8">
  <title>প্রো ম্যাপ মেকার</title>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <style>
    body{margin:0;background:#F6F8FC;font-family:'Noto Sans Bengali',Arial,sans-serif;}
    .maincard{background:#fff;max-width:520px;margin:22px auto 0;
      box-shadow:0 6px 24px #0002;border-radius:14px;padding:16px 0 0 0;position:relative;}
    h2{font-size:2rem;font-weight:700;color:#1E88E5;margin:0 0 8px;}
    #fixStatus{padding:7px 0;min-height:28px;font-size:1rem;color:#fff;
      background:#d32f2f;border-radius:0 0 8px 8px;margin-bottom:10px;transition:background .2s;}
    #fixStatus.ok{background:#388e3c;}
    #gpsInfo{font-size:1.07rem;padding:2px 0 7px 0;color:#444;}
    #stepper{display:flex;justify-content:center;margin:10px 0 12px 0;}
    .stepper-dot{width:22px;height:22px;margin:0 8px;border-radius:50%;background:#bbb;display:inline-block;
      line-height:22px;color:#fff;font-size:1rem;text-align:center;font-weight:bold;box-shadow:0 2px 8px #0001;
      border:3px solid #FFF;transition:background .3s;}
    .stepper-dot.active{background:#1E88E5;}
    #canvasWrap{position:relative;}
    #mapCanvas{width:100%;height:340px;border-radius:14px;background:#fafbfc;display:block;
      margin-bottom:10px;box-shadow:0 2px 10px #0002;touch-action:none;cursor:grab;}
    .fab{position:absolute;bottom:16px;right:18px;z-index:10;}
    .fab button{background:#1E88E5;color:#fff;border:none;font-size:2rem;width:48px;height:48px;
      border-radius:50%;box-shadow:0 2px 8px #0002;}
    #controls button,#controls input,#controls select{font-size:1.1rem;padding:9px 15px;margin:4px 2px 10px;
      border:none;border-radius:14px;box-shadow:0 2px 8px #0001;transition:background .15s;}
    #controls button{background:#1E88E5;color:#fff;font-weight:600;}
    #controls button[disabled]{background:#bbb;}
    #controls input{border:1px solid #eee;background:#fafafc;}
    #sidebar{background:#fff;border-radius:14px;padding:10px 12px 12px 12px;
      margin:18px 12px 0 12px;box-shadow:0 6px 24px #0002;font-size:1.09rem;text-align:left;
      display:none;min-width:200px;max-height:280px;overflow-y:auto;}
    #sidebar.active{display:block;}
    .map-item{padding:7px 0;cursor:pointer;transition:background .15s;}
    .map-item.selected{color:#1E88E5;font-weight:700;background:#e3f2fd;}
    .polygon-item{padding:6px 0;cursor:pointer;transition:background .15s;}
    .polygon-item.selected{color:#1E88E5;font-weight:700;background:#e3f2fd;}
    .colorpicker{width:34px;height:24px;display:inline-block;vertical-align:middle;cursor:pointer;
      border-radius:8px;border:2px solid #eee;}
    .delbtn{background:#e53935;color:#fff;border:none;font-size:1rem;padding:4px 8px;border-radius:7px;margin-left:8px;}
    .popup{position:fixed;left:50%;top:18%;transform:translate(-50%,0);background:#fff;
      border-radius:14px;box-shadow:0 6px 24px #0002;padding:22px 16px;min-width:240px;font-size:1.12rem;
      z-index:99;border:2px solid #1E88E5;}
    .popup h4{margin:0 0 8px;}
    .popup .closebtn{margin-top:8px;}
    .measurement{background:rgba(33,150,243,0.04);border-radius:8px;font-size:.96rem;
      padding:3px 7px;margin:2px 0 0 6px;color:#0d47a1;display:inline-block;}
    @media (max-width:540px){.maincard{max-width:100vw;border-radius:0;}
      #mapCanvas{border-radius:0;}#sidebar{border-radius:0;}.popup{min-width:160px;}}
  </style>
</head>
<body>
  <div class="maincard">
    <h2>ম্যাপ মেকার</h2>
    <div id="fixStatus">জিপিএস সিগন্যাল নেই...</div>
    <div id="gpsInfo">
      অবস্থান: <span id="gpsLat">-</span>, <span id="gpsLon">-</span>
      <span id="gpsHDOP"></span>
    </div>
    <div id="stepper"></div>
    <div id="canvasWrap">
      <canvas id="mapCanvas" width="480" height="340"></canvas>
      <div class="fab">
        <button id="showSidebarBtn" title="সব পলিগন দেখুন">☰</button>
      </div>
    </div>
    <div id="controls"></div>
    <div id="sidebar"></div>
  </div>
  <div id="popup" class="popup" style="display:none;"></div>
<script>
const steps=["নতুন ম্যাপ","পলিগন আঁকুন","তথ্য দিন","ম্যাপ সংরক্ষণ"];
let step=0, hasFix=false, hdop=99.9;
let userPos={lat:0,lon:0}, points=[], polygons=[], selectedPoly=-1;
let mapName="", scale=1, panX=0, panY=0, dragging=false, lastTouch={x:0,y:0}, lastInteraction=Date.now();
let undoStack=[], redoStack=[];
let polyColors=['#43A047','#E53935','#F9A825','#8E24AA','#1E88E5','#00ACC1','#FF7043','#6D4C41','#7CB342'];
let gpsSamples = [];
let mapsList = [];

const canvas=document.getElementById('mapCanvas'), ctx=canvas.getContext('2d');
canvas.width=window.innerWidth<520?window.innerWidth-4:480; canvas.height=340;

// UI step logic
function updateStepper() {
  let html="";
  for(let i=0;i<steps.length;i++)
    html+=`<span class="stepper-dot${i===step?' active':''}">${i+1}</span>`;
  document.getElementById('stepper').innerHTML=html;
}
function showStep(s) {step=s; updateStepper(); draw(); renderControls(); if(s===1) {panX=0;panY=0;}}

function renderControls(){
  const d=document.getElementById('controls');
  d.innerHTML="";
  if(step===0){
    d.innerHTML=`<button id="newMapBtn">নতুন ম্যাপ</button>
      <button id="openLibBtn">ম্যাপ লাইব্রেরি</button>
      <button id="exportBtn" ${polygons.length<1?'disabled':''}>ডাউনলোড</button>`;
    document.getElementById('newMapBtn').onclick=()=>{polygons=[];points=[];undoStack=[];redoStack=[];showStep(1);};
    document.getElementById('openLibBtn').onclick=()=>openMapLibrary();
    document.getElementById('exportBtn').onclick=()=>exportMap();
  }
  else if(step===1){
    d.innerHTML=`
      <button id="addPointBtn" ${!hasFix?'disabled':''}>পয়েন্ট যোগ করুন (মধ্যমান)</button>
      <button id="endPolyBtn" ${points.length<3?'disabled':''}>পলিগন শেষ</button>
      <button id="undoBtn" ${points.length<1?'disabled':''}>আনডু</button>
      <button id="redoBtn" ${redoStack.length<1?'disabled':''}>রিডু</button>
      <button id="finishMapBtn" ${polygons.length<1?'disabled':''}>ম্যাপ শেষ</button>
    `;
    document.getElementById('addPointBtn').onclick=()=>{startMedianSample();};
    document.getElementById('endPolyBtn').onclick=()=>{showStep(2);};
    document.getElementById('undoBtn').onclick=()=>{if(undoStack.length){redoStack.push([...points]);points=undoStack.pop();draw(); renderControls();}};
    document.getElementById('redoBtn').onclick=()=>{if(redoStack.length){undoStack.push([...points]);points=redoStack.pop();draw(); renderControls();}};
    document.getElementById('finishMapBtn').onclick=()=>{showStep(3);};
  }
  else if(step===2){
    let color = polyColors[(polygons.length)%polyColors.length];
    d.innerHTML=`
      <input id="ownerName" placeholder="মালিকের নাম" style="width:54%">
      <input id="colorPick" type="color" class="colorpicker" value="${color}">
      <select id="unitSelect">
        <option value="acre">একর</option><option value="bigha">বিঘা</option>
        <option value="katha">কাঠা</option><option value="shotok">শতক</option>
        <option value="decimal">ডেসিমেল</option><option value="sqft">বর্গফুট</option>
        <option value="sqm">বর্গমিটার</option>
      </select>
      <button id="savePolyBtn">সংরক্ষণ করুন</button>
    `;
    document.getElementById('savePolyBtn').onclick=()=>{
      const areaM2 = calculateArea(points);
      const unit = document.getElementById('unitSelect').value;
      const areaC = convertUnits(areaM2,unit);
      const owner = document.getElementById('ownerName').value||"অজানা";
      const color = document.getElementById('colorPick').value;
      polygons.push({points:[...points],area:areaC.toFixed(2),unit:unit,owner:owner,color:color});
      points=[];undoStack=[];redoStack=[];draw();showStep(1);
    };
  }
  else if(step===3){
    d.innerHTML=`
      <input id="mapName" placeholder="ম্যাপের নাম" style="width:60%" value="${mapName}">
      <button id="saveMapBtn">ম্যাপ সেভ করুন</button>
      <button id="openLibBtn">ম্যাপ লাইব্রেরি</button>
      <button id="exportBtn">ডাউনলোড</button>
    `;
    document.getElementById('saveMapBtn').onclick=()=>{
      mapName = document.getElementById('mapName').value||"নতুন ম্যাপ";
      fetch('/saveMap',{method:'POST',body:JSON.stringify({name:mapName,polygons:polygons})})
        .then(()=>{alert("ম্যাপ সেভ হয়েছে!");showStep(0);openMapLibrary();});
    };
    document.getElementById('openLibBtn').onclick=()=>openMapLibrary();
    document.getElementById('exportBtn').onclick=()=>exportMap();
  }
}

// --- Median GPS sample for better accuracy ---
function startMedianSample() {
  gpsSamples = [];
  let sampleN = 7;
  let i = 0;
  const d=document.getElementById('addPointBtn');
  d.disabled=true;
  d.textContent='নমুনা নেওয়া হচ্ছে...';
  let interval = setInterval(()=>{
    gpsSamples.push({...userPos});
    i++;
    if(i>=sampleN){
      let lat = gpsSamples.map(s=>s.lat).sort((a,b)=>a-b)[Math.floor(sampleN/2)];
      let lon = gpsSamples.map(s=>s.lon).sort((a,b)=>a-b)[Math.floor(sampleN/2)];
      undoStack.push([...points]);
      redoStack=[];
      points.push({lat:lat,lon:lon});
      draw();
      d.disabled=false;d.textContent='পয়েন্ট যোগ করুন (মধ্যমান)';
      clearInterval(interval);
      renderControls();
    }
  }, 350);
}

// --- Map library functions with progress ---
function openMapLibrary(){
  document.getElementById('sidebar').innerHTML='<div style="padding:22px">লোড হচ্ছে...</div>';
  fetch('/listMaps').then(r=>r.json()).then(list=>{
    mapsList=list;
    renderMapSidebar();
  });
}
function renderMapSidebar(){
  const d=document.getElementById('sidebar');
  d.classList.add('active');
  let current = mapName ? `<div style='font-weight:bold;color:#1976D2;padding:6px 0 5px 0'>বর্তমান: ${mapName}</div>` : "";
  let html=current+'<b>সংরক্ষিত ম্যাপসমূহ</b><br>';
  if(mapsList.length==0) html+="<div style='padding:10px;color:#e53935'>কোনো ম্যাপ নেই</div>";
  mapsList.forEach((m,i)=>{
    html+=`<div class="map-item" data-idx="${i}">
      <span>${m.replace('.json','')}</span>
      <button class="delbtn" onclick="deleteSavedMap('${m}');event.stopPropagation();">ডিলিট</button>
      <button class="delbtn" style="background:#1e88e5" onclick="loadSavedMap('${m}');event.stopPropagation();">লোড</button>
    </div>`;
  });
  html+='<br><button onclick="closeSidebar();">বন্ধ করুন</button>';
  d.innerHTML=html;
}
window.closeSidebar=function(){document.getElementById('sidebar').classList.remove('active');}
window.deleteSavedMap=function(name){
  document.getElementById('sidebar').innerHTML = "<div style='padding:20px'>ডিলিট হচ্ছে...</div>";
  fetch('/deleteMap?file='+encodeURIComponent(name)).then(()=>openMapLibrary());
}
window.loadSavedMap=function(name){
  document.getElementById('sidebar').innerHTML = "<div style='padding:20px'>লোড হচ্ছে...</div>";
  fetch('/loadMap?file='+encodeURIComponent(name))
    .then(r=>r.text())
    .then(txt=>{
      try{
        let m=JSON.parse(txt);
        if (!m.polygons || !Array.isArray(m.polygons)) {
          alert("এই ম্যাপে কোনো পলিগন নেই! ফাইলটি হয়তো নষ্ট।");
          return;
        }
        polygons=m.polygons||[]; mapName=m.name||""; points=[];undoStack=[];redoStack=[];draw();showStep(1);closeSidebar();
      }catch(e){
        alert("লোড করতে ব্যর্থ! ফাইলটি নষ্ট বা ভুল ফরম্যাট।");
      }
    });
}

// --- Canvas: Zoom, pan, touch, mouse ---
let lastPinchDist = null;
canvas.addEventListener('touchmove', function(e) {
  if (e.touches.length === 2) {
    let dx = e.touches[0].clientX - e.touches[1].clientX;
    let dy = e.touches[0].clientY - e.touches[1].clientY;
    let dist = Math.sqrt(dx*dx + dy*dy);
    if (lastPinchDist) {
      let dz = (dist - lastPinchDist) * 0.01;
      scale += dz;
      scale = Math.max(0.4, Math.min(6,scale));
      draw();
    }
    lastPinchDist = dist;
    e.preventDefault();
  }
});
canvas.addEventListener('touchend', function(e) {
  if (e.touches.length < 2) lastPinchDist = null;
});
canvas.addEventListener('wheel',e=>{
  scale+=e.deltaY*-0.001; scale=Math.max(0.4,Math.min(6,scale)); draw(); lastInteraction=Date.now(); e.preventDefault();
});
canvas.addEventListener('touchstart',e=>{
  dragging=true; lastTouch={x:e.touches[0].clientX, y:e.touches[0].clientY}; lastInteraction=Date.now();
});
canvas.addEventListener('touchmove',e=>{
  if(e.touches.length===1 && dragging){
    const dx=e.touches[0].clientX-lastTouch.x, dy=e.touches[0].clientY-lastTouch.y;
    panX+=dx; panY+=dy; lastTouch={x:e.touches[0].clientX,y:e.touches[0].clientY}; draw();
  } lastInteraction=Date.now();
});
canvas.addEventListener('touchend',e=>{if(e.touches.length<2)dragging=false;lastInteraction=Date.now();});
setInterval(()=>{if(!dragging&&Date.now()-lastInteraction>3000){panX=0;panY=0;draw();}},500);

function draw(){
  ctx.clearRect(0,0,canvas.width,canvas.height);
  polygons.forEach((poly,idx)=>{
    ctx.save();ctx.beginPath();
    poly.points.forEach((pt,i)=>{
      const xy=gpsToCanvas(pt);
      i===0?ctx.moveTo(xy.x,xy.y):ctx.lineTo(xy.x,xy.y);
    });
    ctx.closePath();ctx.globalAlpha = idx===selectedPoly ? 0.55 : 0.33;
    ctx.fillStyle = poly.color || polyColors[idx%polyColors.length];ctx.fill();ctx.globalAlpha=1.0;
    ctx.lineWidth = idx===selectedPoly?3:1.2;ctx.strokeStyle = idx===selectedPoly ? "#1976D2" : "#444";ctx.stroke();ctx.restore();
    let centroid = getCentroid(poly.points);let xy=gpsToCanvas(centroid);
    ctx.beginPath(); ctx.arc(xy.x,xy.y,6,0,2*Math.PI);ctx.fillStyle=idx===selectedPoly ? "#1976D2" : "#111"; ctx.globalAlpha=0.28; ctx.fill(); ctx.globalAlpha=1;
    let xy0 = gpsToCanvas(poly.points[0]);
    ctx.save();ctx.font="bold 1rem sans-serif";ctx.fillStyle="#222";
    ctx.fillText(`${poly.owner} (${poly.area} ${poly.unit})`, xy0.x+7, xy0.y-8);ctx.restore();
  });
  points.forEach(pt=>{
    const xy=gpsToCanvas(pt);
    ctx.beginPath(); ctx.arc(xy.x,xy.y,5,0,2*Math.PI);
    ctx.fillStyle="red"; ctx.fill();
  });
  if(points.length>1){
    ctx.save();ctx.strokeStyle="#555";ctx.setLineDash([4,4]);ctx.beginPath();
    points.forEach((pt,i)=>{
      let xy=gpsToCanvas(pt);
      if(i===0) ctx.moveTo(xy.x,xy.y);
      else ctx.lineTo(xy.x,xy.y);
    });
    ctx.stroke(); ctx.setLineDash([]);
    ctx.font="0.89rem sans-serif";ctx.fillStyle="#1976D2";
    for(let i=1;i<points.length;i++){
      let a=points[i-1], b=points[i];
      let d=haversine(a.lat,a.lon,b.lat,b.lon);
      let mid={lat:(a.lat+b.lat)/2,lon:(a.lon+b.lon)/2};
      let xy=gpsToCanvas(mid);
      ctx.fillText(d.toFixed(1)+"m",xy.x+5,xy.y-2);
    }
    ctx.restore();
  }
  if(points.length){
    let last=gpsToCanvas(points[points.length-1]),user=gpsToCanvas(userPos);
    ctx.save();ctx.beginPath();ctx.moveTo(last.x,last.y);ctx.lineTo(user.x,user.y);
    ctx.strokeStyle="#1565C0";ctx.setLineDash([5,5]);ctx.stroke();ctx.setLineDash([]);ctx.restore();
  }
  let u = gpsToCanvas(userPos);
  ctx.save();ctx.beginPath();ctx.arc(u.x,u.y,13+3*Math.sin(Date.now()/280),0,2*Math.PI);
  ctx.strokeStyle="#1976D2";ctx.globalAlpha=0.17;ctx.lineWidth=7;ctx.stroke();ctx.globalAlpha=1.0;
  ctx.beginPath();ctx.arc(u.x,u.y,9,0,2*Math.PI);
  ctx.fillStyle="#1976D2";ctx.globalAlpha=0.32;ctx.fill();ctx.globalAlpha=1.0;ctx.restore();
}
function animateUserDot(){ draw(); requestAnimationFrame(animateUserDot);}
requestAnimationFrame(animateUserDot);

function gpsToCanvas(pt){
  return {x:canvas.width/2 + panX + (pt.lon-userPos.lon)*100000*scale,
          y:canvas.height/2 + panY - (pt.lat-userPos.lat)*100000*scale}
}
function calculateArea(pts){
  // Spherical excess formula using Haversine for international accuracy
  if(pts.length<3) return 0;
  let area=0;
  for(let i=0;i<pts.length;i++){
    const j=(i+1)%pts.length;
    area += pts[i].lon*pts[j].lat - pts[j].lon*pts[i].lat;
  }
  return Math.abs(area/2)*111139*111139;
}
function convertUnits(m2,u){
  switch(u){
    case 'acre': return m2/4046.86;
    case 'bigha': return m2/1337.8;
    case 'katha': return m2/67.89;
    case 'shotok':
    case 'decimal': return m2/40.47;
    case 'sqft': return m2*10.7639;
    case 'sqm': return m2;
  }
}
function haversine(lat1,lon1,lat2,lon2){
  const R=6371e3;
  const φ1=lat1*Math.PI/180, φ2=lat2*Math.PI/180;
  const dφ=(lat2-lat1)*Math.PI/180, dλ=(lon2-lon1)*Math.PI/180;
  const a=Math.sin(dφ/2)**2 + Math.cos(φ1)*Math.cos(φ2)*Math.sin(dλ/2)**2;
  return R*2*Math.atan2(Math.sqrt(a),Math.sqrt(1-a));
}
function getCentroid(pts){
  let x=0,y=0;
  pts.forEach(pt=>{x+=pt.lon;y+=pt.lat;});
  return {lat:y/pts.length, lon:x/pts.length};
}
function exportMap(){
  const mapData={name:mapName,polygons:polygons};
  let blob = new Blob([JSON.stringify(mapData)],{type:"application/json"});
  let url = URL.createObjectURL(blob);
  let a = document.createElement('a');
  a.href=url;a.download=(mapName||'map')+'.json';document.body.appendChild(a);a.click();
  setTimeout(()=>{document.body.removeChild(a);URL.revokeObjectURL(url);},500);
}
showStep(0);

const ws = new WebSocket(`ws://${location.hostname}:81`);
ws.onmessage = e => {
  const d=JSON.parse(e.data);
  hasFix = (d.lat !== 0 && d.lon !== 0 && d.hdop && d.hdop < 8);
  userPos.lat = d.lat; userPos.lon = d.lon; hdop = d.hdop || 99.9;
  showFixStatus(hasFix); draw();
  document.getElementById('gpsLat').textContent = d.lat.toFixed(6);
  document.getElementById('gpsLon').textContent = d.lon.toFixed(6);
  document.getElementById('gpsHDOP').textContent = ` (নির্ভুলতা ${hdop.toFixed(1)}m)`;
};
function showFixStatus(ok) {
  const d=document.getElementById('fixStatus');
  if(ok) {
    d.textContent="ঠিকানা পাওয়া গেছে! "+(hdop<8?"দৈর্ঘ্যিক নির্ভুলতা "+hdop.toFixed(1)+"m":"");
    d.classList.add('ok');
  } else {
    d.textContent="জিপিএস সিগন্যাল নেই...";
    d.classList.remove('ok');
  }
}
</script>
</body>
</html>
)rawliteral";

// ==== ESP32 BACKEND ====
// SD card map CRUD
void handleRoot() { server.send_P(200, "text/html", index_html); }
void handleSaveMap() {
  if(!server.hasArg("plain")) return server.send(400,"text/plain","Missing");
  if(!SD.exists("/maps")) SD.mkdir("/maps");
  String fn="/maps/map_"+String(millis())+".json";
  File f=SD.open(fn,FILE_WRITE);
  f.print(server.arg("plain")); f.close();
  server.send(200,"text/plain","Saved");
}
void handleListMaps(){
  if(!SD.exists("/maps")) SD.mkdir("/maps");
  File dir=SD.open("/maps"), file;
  String out="[", comma="";
  while((file=dir.openNextFile())){
    out += comma + "\"" + String(file.name()).substring(6) + "\"";
    comma=","; file.close();
  }
  out+="]";
  server.send(200,"application/json",out);
}
void handleLoadMap(){
  if(!server.hasArg("file")) return server.send(400,"text/plain","Missing");
  String fn="/maps/"+server.arg("file");
  if(!SD.exists(fn)) return server.send(404,"text/plain","NotFound");
  File f=SD.open(fn,FILE_READ);
  server.streamFile(f,"application/json");
  f.close();
}
void handleDeleteMap() {
  if(!server.hasArg("file")) return server.send(400,"text/plain","Missing");
  String fn="/maps/"+server.arg("file");
  if(SD.exists(fn)) SD.remove(fn);
  server.send(200,"text/plain","OK");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){
  if(type==WStype_CONNECTED) Serial.printf("Client %u connected\n", num);
}
void setup(){
  Serial.begin(115200);
  GPSserial.begin(9600,SERIAL_8N1,GPS_TX_PIN,-1);
  WiFi.softAP(ssid,password);
  Serial.println("AP IP: "+WiFi.softAPIP().toString());
  server.on("/",              handleRoot);
  server.on("/saveMap",        HTTP_POST,handleSaveMap);
  server.on("/listMaps",       HTTP_GET, handleListMaps);
  server.on("/loadMap",        HTTP_GET, handleLoadMap);
  server.on("/deleteMap",      HTTP_GET, handleDeleteMap);
  server.begin();
  webSocket.begin(); webSocket.onEvent(webSocketEvent);
  SPI.begin(SCK_PIN,MISO_PIN,MOSI_PIN,CS_PIN);
  if(!SD.begin(CS_PIN)){
    Serial.println("SD fail");
  } else {
    Serial.println("SD OK");
    if(!SD.exists("/maps")) SD.mkdir("/maps");
  }
}
void loop(){
  server.handleClient();
  webSocket.loop();
  while(GPSserial.available()){
    char c = GPSserial.read();
    if(gps.encode(c) && gps.location.isUpdated()){
      float lat = gps.location.lat();
      float lon = gps.location.lng();
      float hdop= gps.hdop.isValid() ? gps.hdop.hdop() : 99.9;
      String js = "{\"lat\":"+String(lat,6)+",\"lon\":"+String(lon,6)+",\"hdop\":"+String(hdop,1)+"}";
      webSocket.broadcastTXT(js);
    }
  }
}
