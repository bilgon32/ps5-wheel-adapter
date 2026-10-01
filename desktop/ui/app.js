/* Offline UI. Demo measurements are explicit and never reach hardware APIs. */
"use strict";
const $ = id => document.getElementById(id);
const profileNames = ["Standard / linear", "Minimum force · 12%", "Minimum force · 18%", "Progressive", "Measured wheel LUT"];
const profileHints = ["The original game signal", "A gentle lift near center", "A stronger lift near center", "A smooth boost to small forces", "Your wheel’s measured response"];
const profileDescriptions = ["Pass the game’s force signal through unchanged. Always available for comparison.", "Lift small forces above the G27’s mechanical deadzone while preserving full force.", "A stronger minimum-force lift for comparison. Keep the profile that feels best on your wheel.", "A smooth response curve boosts smaller forces and converges to full output.", "Use the bundled measured G27 curve, or import a LUT measured from your own wheel. Zero force always stays zero."];
const defaultSettings = () => ({external_brake:false, brake_vid:0x2341, brake_pid:0x8037, brake_instance:255,
  brake_report_id:3, brake_width:16, brake_signed:true, brake_bit_offset:120, brake_released:-32767,
  brake_pressed:32767, brake_timeout_ms:500, auth_vid:0, auth_pid:0, auth_instance:255,
  default_profile:1, custom_lut:false, lut:Array.from({length:128},(_,i)=>i)});
const state = {settings:defaultSettings(), devices:[], adapters:[], connected:false, demo:false,
  selectedSlot:null, selectedAxis:null, axis:null, dirty:false, trace:[], paused:false, busy:false,
  pollBusy:false, statusBusy:false, deviceBusy:false, identify:null, lutName:null, started:performance.now()};
const demoAxis = {index:0,name:"Z rotation",usage_page:1,usage:0x35,bit_offset:120,report_id:3,width:16,signed:true,minimum:-32767,maximum:32767,value:-32767,valid:true};
const demoDevices = () => [
  {slot:0,address:1,instance:0,vid:0x046d,pid:0xc29b,name:"Logitech G27",axes:[],auth_capable:false,descriptor_valid:true},
  {slot:1,address:2,instance:0,vid:0x0f0d,pid:0x005e,name:"Hori controller",axes:[],auth_capable:true,descriptor_valid:true},
  {slot:2,address:3,instance:0,vid:0x2341,pid:0x8037,name:"Arduino Micro",axes:[{...demoAxis}],auth_capable:false,descriptor_valid:true}];
const clamp = (v,a=0,b=1) => Math.min(b,Math.max(a,v));
const escapeHTML = text => String(text).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));

function notice(message,error=false) {
  $("notice").textContent=message; $("notice").classList.toggle("error",error); $("notice").hidden=false;
}
function markDirty() {state.dirty=true; $("dirty").hidden=false; renderSettings();}
function showPage(page) {
  document.querySelectorAll(".page").forEach(el=>el.classList.toggle("active",el.id===page));
  document.querySelectorAll(".nav").forEach(el=>el.classList.toggle("active",el.dataset.page===page));
  $("page-label").textContent=({overview:"LIVE DASHBOARD",devices:"DEVICES & PEDALS",feedback:"FORCE FEEDBACK",install:"INSTALL & BACKUP",guide:"SETUP GUIDE"})[page];
  drawCharts(); window.scrollTo(0,0);
}
async function api(method,...args) {
  if(state.demo) throw new Error("Exit demo and connect your Pico before changing hardware or using file dialogs.");
  if(!window.pywebview?.api) throw new Error("Open the G27 Studio desktop application to connect hardware and use file dialogs.");
  const result=await window.pywebview.api[method](...args);
  if(!result.ok) throw new Error(result.error);
  return result.data;
}
async function action(fn) {
  if(state.busy) return;
  state.busy=true;
  document.body.setAttribute("aria-busy","true");
  try {await fn();} catch(error) {notice(error.message,true);}
  finally {state.busy=false; document.body.removeAttribute("aria-busy");}
}
function updateConnection() {
  $("connection-label").textContent=state.demo?"Demo workspace":state.connected?"Pico connected":"Connect Pico";
  $("connection-dot").classList.toggle("muted",!state.connected&&!state.demo);
  $("demo-banner").hidden=!state.demo;
  $("demo-toggle").textContent=state.demo?"Exit demo ↗":"Explore the demo ↗";
  $("session-state").textContent=state.demo?"Simulated inputs · no hardware changes":state.connected?"Pico setup mode · live measurements":"Local workspace · offline ready";
}
function startDemo() {
  if(state.connected) {notice("Disconnect the Pico before exploring the demo.",true); return;}
  state.beforeDemo={settings:structuredClone(state.settings),dirty:state.dirty};
  state.demo=true; state.devices=demoDevices(); state.settings=defaultSettings(); state.settings.external_brake=true;
  state.dirty=false; $("dirty").hidden=true; state.trace=[];
  state.selectedSlot=2; state.selectedAxis=0;
  $("notice").hidden=true;
  updateConnection(); renderDevices(); renderSettings();
}
function stopDemo() {
  if(!state.demo)return;
  state.demo=false;state.devices=[];state.axis=null;state.trace=[];state.selectedSlot=null;state.selectedAxis=null;
  if(state.beforeDemo){state.settings=state.beforeDemo.settings;state.dirty=state.beforeDemo.dirty;$("dirty").hidden=!state.dirty;}
  updateConnection();renderDevices();renderSettings();resetMeasurements();refreshStatus();
}
function renderSettings() {
  const s=state.settings;
  $("internal-brake").classList.toggle("selected",!s.external_brake);$("external-brake").classList.toggle("selected",s.external_brake);
  $("external-settings").hidden=!s.external_brake;$("internal-description").hidden=s.external_brake;
  $("released-endpoint").textContent=s.brake_released;$("pressed-endpoint").textContent=s.brake_pressed;
  const knownTimeout=[0,500,1000].includes(s.brake_timeout_ms);
  if(!knownTimeout&&!$("brake-timeout").querySelector(`[value="${s.brake_timeout_ms}"]`)) {
    const option=document.createElement("option");option.value=s.brake_timeout_ms;option.textContent=`Custom timeout · ${s.brake_timeout_ms} ms`;$("brake-timeout").append(option);
  }
  $("brake-timeout").value=String(s.brake_timeout_ms);
  $("brake-state").textContent=s.external_brake?"External load-cell pedal":"Original G27 pedals";
  $("profile-state").textContent=profileNames[s.default_profile];
  renderProfiles();drawCharts();
}
function renderDevices() {
  const list=state.devices;
  $("device-list").innerHTML=list.length?list.map(d=>`<div class="device-row"><span class="device-icon">${d.pid===0xc29b||d.pid===0xc294?"◉":d.auth_capable?"⌘":"▥"}</span><div><strong>${escapeHTML(d.name)}</strong><small>${d.vid.toString(16).padStart(4,"0")}:${d.pid.toString(16).padStart(4,"0")} · Interface ${d.instance}</small></div><span class="badge">${d.pid===0xc29b?"NATIVE G27":d.pid===0xc294?"INITIALIZING":d.auth_capable?"AUTH INTERFACE":d.axes.length?d.axes.length+" AXES":"NO AXES"}</span></div>`).join(""):
    '<div class="empty-state">Connect your Pico to discover the wheel, controller, and pedals.</div>';
  const auth=list.filter(d=>d.auth_capable);
  $("auth-device").innerHTML='<option value="auto">Automatically find an authentication interface</option>'+auth.map(d=>`<option value="${d.slot}">${escapeHTML(d.name)} · interface ${d.instance}</option>`).join("");
  const selectedAuth=auth.find(d=>d.vid===state.settings.auth_vid&&d.pid===state.settings.auth_pid&&(state.settings.auth_instance===255||d.instance===state.settings.auth_instance));
  $("auth-device").value=selectedAuth?String(selectedAuth.slot):"auto";
  if(state.settings.auth_vid&&!selectedAuth){const o=document.createElement("option");o.value="saved";o.textContent="Saved controller · disconnected";$("auth-device").append(o);$("auth-device").value="saved";}
  $("controller-state").textContent=selectedAuth?selectedAuth.name:(!state.settings.auth_vid&&auth.length?"Authentication interface found":"Waiting for controller");
  const pedals=list.filter(d=>d.axes.length&&!(d.vid===0x046d&&[0xc29b,0xc294].includes(d.pid))&&!d.auth_capable);
  $("brake-device").innerHTML='<option value="">Select a connected USB pedal device</option>'+pedals.map(d=>`<option value="${d.slot}">${escapeHTML(d.name)} · interface ${d.instance}</option>`).join("");
  const selected=pedals.find(d=>d.vid===state.settings.brake_vid&&d.pid===state.settings.brake_pid&&(state.settings.brake_instance===255||d.instance===state.settings.brake_instance));
  if(selected){state.selectedSlot=selected.slot;$("brake-device").value=String(selected.slot);}
  else {state.selectedSlot=null;state.selectedAxis=null;state.axis=null;$("raw-value").textContent="—";$("raw-status").textContent="Pedal device not connected";}
  renderAxes(false);
}
function renderAxes(selectNew) {
  const d=state.devices.find(d=>d.slot===state.selectedSlot);
  $("brake-axis").innerHTML='<option value="">Select an axis</option>'+(d?.axes||[]).map(a=>`<option value="${a.index}">${escapeHTML(a.name)} · ${a.width}-bit</option>`).join("");
  const existing=d?.axes.find(a=>a.bit_offset===state.settings.brake_bit_offset&&a.report_id===state.settings.brake_report_id&&a.width===state.settings.brake_width);
  if(existing&&selectNew){chooseAxis(existing);}
  else if(existing){state.selectedAxis=existing.index;$("brake-axis").value=String(existing.index);}
  else if(selectNew&&d?.axes.length){chooseAxis(d.axes[0]);}
  else {state.selectedAxis=null;state.axis=null;}
}
function chooseAxis(a) {
  state.selectedAxis=a.index;$("brake-axis").value=String(a.index);
  Object.assign(state.settings,{brake_report_id:a.report_id,brake_width:a.width,brake_signed:a.signed,
    brake_bit_offset:a.bit_offset,brake_released:a.minimum,brake_pressed:a.maximum});
  state.axis=a.valid?{...a}:null;markDirty();
}
function renderProfiles() {
  $("profile-list").innerHTML=profileNames.map((name,i)=>`<button class="profile-choice ${state.settings.default_profile===i?"selected":""}" data-profile="${i}" aria-pressed="${state.settings.default_profile===i}"><span class="profile-number">0${i+1}</span><span><strong>${escapeHTML(name)}</strong><small>${escapeHTML(profileHints[i])}</small></span>${state.settings.default_profile===i?'<span class="check">✓</span>':""}</button>`).join("");
  $("curve-name").textContent=profileNames[state.settings.default_profile];
  $("curve-description").textContent=profileDescriptions[state.settings.default_profile];
  $("lut-info").hidden=!state.settings.custom_lut;
  $("lut-info").textContent=state.settings.custom_lut?`${state.lutName||"Custom LUT"} · 128 validated samples · replaces profile 5. Save or test settings to use it on the wheel.`:"";
}

function canvasContext(id) {
  const canvas=$(id);if(!canvas.clientWidth||!canvas.clientHeight)return null;
  const dpr=window.devicePixelRatio||1,w=canvas.clientWidth,h=canvas.clientHeight;
  if(canvas.width!==Math.round(w*dpr)||canvas.height!==Math.round(h*dpr)){canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);}
  const ctx=canvas.getContext("2d");ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);return{ctx,w,h};
}
function grid(ctx,w,h,pad=0) {
  ctx.strokeStyle="#35402e";ctx.lineWidth=1;
  for(let i=0;i<=4;i++){const y=pad+(h-2*pad)*i/4;ctx.beginPath();ctx.moveTo(pad,y);ctx.lineTo(w-pad,y);ctx.stroke();}
  for(let i=0;i<=5;i++){const x=pad+(w-2*pad)*i/5;ctx.beginPath();ctx.moveTo(x,pad);ctx.lineTo(x,h-pad);ctx.stroke();}
}
function drawTrace() {
  const context=canvasContext("trace-chart");if(!context)return;
  const {ctx,w,h}=context;grid(ctx,w,h,1);
  if(!state.trace.length)return;
  const now=state.trace.at(-1).t;
  for(const[key,color]of[["throttle","#c7ef82"],["brake","#efb477"],["clutch","#93b9ed"]]){
    ctx.beginPath();ctx.strokeStyle=color;ctx.lineWidth=1.7;
    state.trace.forEach((point,i)=>{const x=w*(1-(now-point.t)/10000),y=(h-4)*(1-clamp(point[key]))+2;i?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();
  }
}
/* Bundled measured G27 samples mirror src/ffb.c. Imported LUTs replace these. */
const measured=[0,16,21,21,21,21,21,22,22,23,23,24,24,25,25,26,26,26,26,27,27,28,28,29,30,30,31,32,32,33,33,34,34,35,35,35,35,35,36,36,36,37,38,38,39,39,39,40,40,40,40,41,42,42,42,43,43,43,44,44,45,45,45,46,46,46,47,47,48,50,51,52,54,56,57,58,60,62,64,65,67,70,71,72,74,77,79,81,82,82,82,86,89,90,91,94,95,95,97,99,100,100,101,102,104,105,106,107,108,110,111,111,111,111,111,111,111,112,112,112,114,117,118,121,124,125,126,127];
function curveSample(i) {
  if(!i)return 0;const profile=state.settings.default_profile;
  if(profile===1||profile===2){const min=profile===1?15:23;return min+Math.floor(((127-min)*(i-1)+63)/126);}
  if(profile===3)return Math.floor(Math.pow(i/127,.65)*127+.5);
  if(profile===4)return(state.settings.custom_lut?state.settings.lut:measured)[i];return i;
}
function drawForce() {
  const context=canvasContext("force-chart");if(!context)return;const{ctx,w,h}=context;grid(ctx,w,h,2);
  ctx.beginPath();ctx.strokeStyle="#6c7c62";ctx.lineWidth=1;ctx.setLineDash([5,5]);ctx.moveTo(2,h-2);ctx.lineTo(w-2,2);ctx.stroke();ctx.setLineDash([]);
  ctx.beginPath();for(let i=0;i<128;i++){const x=2+(w-4)*i/127,y=h-2-(h-4)*curveSample(i)/127;i?ctx.lineTo(x,y):ctx.moveTo(x,y);}
  ctx.strokeStyle="#c7ef82";ctx.lineWidth=2;ctx.stroke();ctx.lineTo(w-2,h-2);ctx.lineTo(2,h-2);ctx.closePath();
  const gradient=ctx.createLinearGradient(0,0,0,h);gradient.addColorStop(0,"#c7ef8219");gradient.addColorStop(1,"#c7ef8200");ctx.fillStyle=gradient;ctx.fill();
}
function drawCharts(){drawTrace();drawForce();}
function resetMeasurements() {paintMeasurement({steering:0,throttle:0,brake:0,clutch:0,gear:"N",buttons:[0,0,0],wheel_connected:false},false);$("trace-empty").hidden=false;}
function paintMeasurement(data,live=true) {
  const steering=clamp(data.steering,-1,1);$("wheel-visual").style.transform=`rotate(${steering*450}deg)`;
  $("steering-degrees").innerHTML=`${Math.round(steering*450)}<span>°</span>`;$("steering-marker").style.left=`${(steering+1)*50}%`;
  for(const key of["throttle","brake","clutch"]){$(key+"-value").innerHTML=`${Math.round(clamp(data[key])*100)}<small>%</small>`;$(key+"-fill").style.height=`${clamp(data[key])*100}%`;}
  $("gear-value").textContent=data.gear;document.querySelectorAll("[data-gear]").forEach(el=>el.classList.toggle("active",el.dataset.gear===data.gear));
  const buttonBits=(data.buttons[0]>>4)|(data.buttons[1]<<4)|((data.buttons[2]&3)<<12);
  const count=buttonBits.toString(2).replace(/0/g,"").length;$("button-count").textContent=`${count} button${count===1?"":"s"} pressed`;
  $("wheel-state").textContent=data.wheel_connected?"Native G27 connected":data.wheel_state==="initializing"?"G27 initializing…":data.wheel_state==="compatibility"?"Native mode unavailable":"Wheel not connected";$("wheel-dot").classList.toggle("muted",!data.wheel_connected);
  $("live-state").textContent=state.demo?"SIMULATED":live&&data.wheel_connected?"LIVE INPUT":"WAITING FOR INPUT";
  $("pedal-live-state").textContent=state.demo?"SIMULATED":live?"LIVE":"WAITING";
  if(live&&!state.paused){const t=performance.now();state.trace.push({t,...data});state.trace=state.trace.filter(p=>t-p.t<=10000);$("trace-empty").hidden=true;drawTrace();}
  if(data.axis){state.axis=data.axis.valid?data.axis:null;$("raw-value").textContent=data.axis.valid?data.axis.value:"—";$("raw-status").textContent=data.axis.valid?"Live input from the selected axis":"Waiting for this axis to report";}
}
async function pollMeasurements() {
  if(state.pollBusy||state.busy||(!state.connected&&!state.demo))return;
  state.pollBusy=true;
  try{
    let data;
    if(state.demo){const t=(performance.now()-state.started)/1000;data={steering:.36*Math.sin(t*.8),throttle:clamp(.5+.5*Math.sin(t*.95)),brake:Math.pow(clamp(Math.sin(t*.95+2)),3)*.86,clutch:Math.pow(clamp(Math.sin(t*1.8+1)),12),gear:String(1+Math.floor((t/2)%6)),buttons:[8,0,0],wheel_connected:true};if(state.selectedSlot===2){data.axis={...demoAxis,value:Math.round(-32767+65534*data.brake)};}}
    else data=await api("measure",state.selectedSlot,state.selectedAxis);
    paintMeasurement(data);
  }catch(error){state.connected=false;updateConnection();resetMeasurements();notice("Live connection lost. Check the Pico’s USB cable and reconnect. "+error.message,true);}
  finally{state.pollBusy=false;}
}
async function refreshDevices() {
  if(state.deviceBusy||state.busy)return;
  if(state.demo){state.devices=demoDevices();renderDevices();return;}
  if(!state.connected)return;
  state.deviceBusy=true;
  try{state.devices=await api("devices");renderDevices();}catch(error){notice(error.message,true);}finally{state.deviceBusy=false;}
}
async function refreshStatus() {
  if(state.statusBusy||state.demo||!window.pywebview?.api)return;
  state.statusBusy=true;
  try{const data=await api("status");state.adapters=data.adapters;$("version").textContent=data.version;
    $("firmware-badge").textContent=data.firmware_available?"FIRMWARE BUNDLED":"FIRMWARE NOT FOUND";
    const previous=$("pico-drive").value;$("pico-drive").innerHTML=data.pico_drives.length?data.pico_drives.map(d=>`<option value="${escapeHTML(d)}">RPI-RP2 · ${escapeHTML(d)}</option>`).join(""):'<option value="">No Pico drive found</option>';
    if(data.pico_drives.includes(previous))$("pico-drive").value=previous;
    renderAdapters();
  }catch(error){notice(error.message,true);}finally{state.statusBusy=false;}
}
function renderAdapters() {
  $("adapter-list").innerHTML=state.adapters.length?state.adapters.map((a,i)=>`<div class="device-row"><span class="device-icon">⌘</span><div><strong>${escapeHTML(a.name)}</strong><small>${a.mode==="setup"?"Ready for configuration":"Console identity · open setup to configure"}</small></div><button class="${a.mode==="setup"?"primary":"secondary"}" data-adapter="${i}">${a.mode==="setup"?"Connect":"Open setup mode"}</button></div>`).join(""):'<div class="empty-state">No adapter found. For your first installation, use BOOTSEL and the bundled firmware.</div>';
}
async function connectAdapter(index) {
  const a=state.adapters[index];if(!a)return;
  if(state.dirty&&!window.confirm("Connecting loads the Pico’s saved settings and replaces unsaved changes. Continue?"))return;
  if(a.mode==="console"){const result=await api("enter_setup",a.path);notice(result.message);return;}
  const result=await api("connect",a.path);state.connected=true;state.settings=result.settings;state.devices=result.devices;state.dirty=false;state.trace=[];
  $("dirty").hidden=true;$("notice").hidden=true;$("connection-dialog").close();updateConnection();renderDevices();renderSettings();
}
async function saveSettings(persistent=true) {
  if(!state.connected&&!state.demo)throw new Error("Connect the Pico first, or export a personalized UF2 from Install & backup.");
  if(state.demo){notice("Demo only: settings are shown locally. Exit demo to save to a real Pico.");return;}
  state.settings=await api("save_settings",state.settings,persistent);
  if(persistent){state.dirty=false;$("dirty").hidden=true;}
  renderSettings();notice(persistent?"Saved to your Pico. Your devices, calibration, and force curve are ready for the next drive.":"Settings applied for this setup session. Save to Pico to keep them after a restart.");
}
async function identifyAxis() {
  const d=state.devices.find(d=>d.slot===state.selectedSlot);if(!d)throw new Error("Choose a connected pedal device first.");
  $("identify-hint").hidden=false;const baseline=d.axes.map(a=>({...a}));const started=performance.now();
  while(performance.now()-started<12000){
    await new Promise(resolve=>setTimeout(resolve,120));
    for(const a of baseline){const current=state.demo?{...a,value:Math.round(32767*Math.sin(performance.now()/1000)),valid:true}:await api("measure",state.selectedSlot,a.index).then(d=>d.axis);
      if(current.valid&&a.valid&&Math.abs(current.value-a.value)>(a.maximum-a.minimum)*.12){chooseAxis(current);$("identify-hint").hidden=true;notice(`Found ${current.name}. Capture released and pressed positions next.`);return;}
      if(current.valid&&!a.valid){a.value=current.value;a.valid=true;}
    }
  }
  $("identify-hint").hidden=true;throw new Error("No axis moved enough. Release the brake, then try again and press it gradually.");
}

document.querySelectorAll(".nav").forEach(el=>el.addEventListener("click",()=>showPage(el.dataset.page)));
document.querySelectorAll("[data-go]").forEach(el=>el.addEventListener("click",()=>showPage(el.dataset.go)));
document.querySelectorAll(".save-settings").forEach(el=>el.addEventListener("click",()=>action(()=>saveSettings(true))));
$("connect").addEventListener("click",()=>{if(state.demo)stopDemo();$("connection-dialog").showModal();refreshStatus();});
$("close-dialog").addEventListener("click",()=>$("connection-dialog").close());
$("connection-dialog").addEventListener("click",event=>{if(event.target===$("connection-dialog"))$("connection-dialog").close();});
$("adapter-list").addEventListener("click",event=>{const el=event.target.closest("[data-adapter]");if(el)action(()=>connectAdapter(Number(el.dataset.adapter)));});
$("scan-adapters").addEventListener("click",refreshStatus);$("first-install").addEventListener("click",()=>{$("connection-dialog").close();showPage("install");});
$("refresh-devices").addEventListener("click",refreshDevices);$("refresh-drives").addEventListener("click",refreshStatus);
$("demo-toggle").addEventListener("click",()=>state.demo?stopDemo():startDemo());$("demo-exit").addEventListener("click",stopDemo);
$("pause-trace").addEventListener("click",()=>{state.paused=!state.paused;$("pause-trace").textContent=state.paused?"Resume":"Pause";});
$("internal-brake").addEventListener("click",()=>{state.settings.external_brake=false;markDirty();});$("external-brake").addEventListener("click",()=>{state.settings.external_brake=true;markDirty();});
$("auth-device").addEventListener("change",()=>{if($("auth-device").value==="saved")return;const d=state.devices.find(d=>String(d.slot)===$("auth-device").value);state.settings.auth_vid=d?.vid||0;state.settings.auth_pid=d?.pid||0;state.settings.auth_instance=d?.instance??255;markDirty();});
$("brake-device").addEventListener("change",()=>{const d=state.devices.find(d=>String(d.slot)===$("brake-device").value);if(!d)return;state.selectedSlot=d.slot;state.settings.brake_vid=d.vid;state.settings.brake_pid=d.pid;state.settings.brake_instance=d.instance;state.settings.brake_timeout_ms=d.vid===0x2341&&d.pid===0x8037?500:0;state.axis=null;renderAxes(true);markDirty();});
$("brake-axis").addEventListener("change",()=>{const d=state.devices.find(d=>d.slot===state.selectedSlot),a=d?.axes.find(a=>String(a.index)===$("brake-axis").value);if(a)chooseAxis(a);});
$("identify-axis").addEventListener("click",()=>action(identifyAxis));
for(const kind of["released","pressed"]){$("capture-"+kind).addEventListener("click",()=>{if(!state.axis){notice("Choose an axis and wait for a live measurement before capturing calibration.",true);return;}state.settings["brake_"+kind]=state.axis.value;markDirty();notice(`Captured the ${kind} position. ${kind==="released"?"Now press to your intended maximum and capture pressed.":"Use Test without saving to check the full travel."}`);});}
$("invert-brake").addEventListener("click",()=>{[state.settings.brake_released,state.settings.brake_pressed]=[state.settings.brake_pressed,state.settings.brake_released];markDirty();});
$("brake-timeout").addEventListener("change",()=>{state.settings.brake_timeout_ms=Number($("brake-timeout").value);markDirty();});
$("preview-settings").addEventListener("click",()=>action(()=>saveSettings(false)));
$("profile-list").addEventListener("click",event=>{const el=event.target.closest("[data-profile]");if(!el)return;state.settings.default_profile=Number(el.dataset.profile);markDirty();if(state.connected)action(()=>api("preview_profile",state.settings.default_profile));});
$("import-lut").addEventListener("click",()=>action(async()=>{const data=await api("import_lut");if(!data)return;state.settings.lut=data.lut;state.settings.custom_lut=true;state.settings.default_profile=4;state.lutName=data.name;markDirty();notice("LUT imported and validated. Save to Pico to use your measured curve.");}));
for(const[button,direction]of[["pulse-left",-1],["pulse-right",1],["pulse-stop",0]]){$(button).addEventListener("click",()=>action(async()=>{await api("force_pulse",direction);notice(direction?"Brief force pulse sent. The firmware stops it automatically.":"Stop command sent.");}));}
$("export-settings").addEventListener("click",()=>action(async()=>{const name=await api("export_settings",state.settings);if(name)notice(`Settings exported to ${name}.`);}));
$("import-settings").addEventListener("click",()=>action(async()=>{const data=await api("import_settings");if(!data)return;state.settings=data;state.lutName=data.custom_lut?"LUT from settings backup":null;markDirty();renderDevices();notice("Backup loaded. Review it before saving to Pico or installing firmware.");}));
$("export-uf2").addEventListener("click",()=>action(async()=>{const data=await api("export_uf2",state.settings);if(data)notice(`Personalized firmware exported to ${data.name}.`);}));
$("enter-bootsel").addEventListener("click",()=>action(async()=>{await api("reboot","bootsel");state.connected=false;updateConnection();notice("Pico restarting into BOOTSEL. Wait for RPI-RP2 to appear below.");}));
$("flash").addEventListener("click",()=>action(async()=>{const drive=$("pico-drive").value;if(!drive)throw new Error("Hold BOOTSEL while connecting the Pico, then refresh to find RPI-RP2.");const data=await api("flash",state.settings,drive);state.connected=false;state.dirty=false;$("dirty").hidden=true;updateConnection();notice(data.message);}));
$("console-mode").addEventListener("click",()=>action(async()=>{await saveSettings(true);await api("reboot","console");state.connected=false;updateConnection();notice("Settings saved. Move the Pico’s built-in USB cable to your PS5. Keep the wheel and controller on its hub.");}));
window.addEventListener("resize",drawCharts);window.addEventListener("pywebviewready",refreshStatus);
window.addEventListener("beforeunload",event=>{if(state.dirty&&!state.demo){event.preventDefault();event.returnValue="";}});
setInterval(pollMeasurements,80);setInterval(()=>{if(!state.busy){refreshStatus();refreshDevices();}},3000);
renderSettings();updateConnection();resetMeasurements();
if(new URLSearchParams(window.location.search).get("demo")==="1")startDemo();
window.g27Studio={state,showPage,drawCharts,curveSample};
