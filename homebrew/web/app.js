'use strict';
// Never replace unsaved JSON during background status refreshes.
const $=id=>document.getElementById(id);
let revision='',saved='',busy=false,loaded=false;
function message(text,kind=''){ $('message').textContent=text;$('message').className=kind; }
function controls(){ $('save').disabled=busy||!loaded||$('editor').value===saved;$('reload').disabled=busy;$('editor').disabled=busy||!loaded;$('user').disabled=busy||!loaded; }
async function request(path,options={}){
  const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),15000);
  try{const response=await fetch(path,{...options,cache:'no-store',signal:controller.signal});const body=await response.json();if(!response.ok)throw new Error(body.error||`HTTP ${response.status}`);return body;}
  finally{clearTimeout(timer);}
}
function syncUser(){try{$('user').value=JSON.parse($('editor').value).pools[0].user;}catch{/* Keep incomplete JSON available for correction. */}}
async function load(){busy=true;controls();try{const data=await request('/config');revision=data.revision;$('editor').value=JSON.stringify(data.config,null,2)+'\n';saved=$('editor').value;loaded=true;syncUser();message('Saved configuration loaded.');}catch(error){message(error.message,'error');}finally{busy=false;controls();}}
$('editor').addEventListener('input',()=>{syncUser();controls();message('Unsaved changes.');});
$('user').addEventListener('input',()=>{try{const config=JSON.parse($('editor').value);config.pools[0].user=$('user').value;$('editor').value=JSON.stringify(config,null,2)+'\n';controls();message('Unsaved changes.');}catch{message('Fix the JSON before changing the User field.','error');}});
$('reload').addEventListener('click',load);
$('save').addEventListener('click',async()=>{
  let config;try{config=JSON.parse($('editor').value);}catch(error){message(`Invalid JSON: ${error.message}`,'error');return;}
  busy=true;controls();message('Saving and applying…');
  try{const result=await request('/config',{method:'POST',headers:{'Content-Type':'application/json','X-XMRig-Control':'1'},body:JSON.stringify({revision,config})});revision=result.revision;saved=$('editor').value;message('Saved and applied.','success');}
  catch(error){message(error.message,'error');}finally{busy=false;controls();}
});
async function status(){try{const s=await request('/status');$('connection').textContent='Connected to PS5';$('mining').textContent=s.mining?'Started':'Stopped';$('pool').textContent=s.connected?'Connected':'Disconnected';$('rate').textContent=`${Math.trunc(s.hashrate)} H/s`;
  const g=s.gpu;$('gpu-status').textContent=`${g.faulted?'Fault — relaunch app':g.stopping?'Stopping':g.running?'Running':'Stopped'} · ${Number(g.graphs_per_second).toFixed(3)} graphs/s · ${g.connected?'Connected':'Disconnected'} · ${g.accepted} accepted / ${g.rejected} rejected${g.result?' · error '+g.result:''}`;
  // Disable unavailable actions while retaining each engine's independent state.
  $('cpu-start').disabled=s.mining;$('cpu-stop').disabled=!s.mining;
  $('gpu-start').disabled=g.running||g.stopping||g.faulted;$('gpu-stop').disabled=!g.running||g.stopping;$('gpu-save').disabled=g.running||g.stopping;
  // Display the assigned API port without an automatic-port label.
  $('http-api').textContent=!s.http_api_enabled?'Off':s.http_api_port?String(s.http_api_port):'Not running';}catch{$('connection').textContent='PS5 unavailable';}finally{setTimeout(status,1000);}}
async function log(){try{$('log').textContent=(await request('/log')).log||'No mining log yet.';}catch(error){$('log').textContent=error.message;}}
$('log-refresh').addEventListener('click',log);document.querySelector('details').addEventListener('toggle',e=>{if(e.target.open)log();});
load();status();

// Explicit GPU actions preserve unsaved text and the independent CPU configuration.
const gpuRequest=(path,body)=>request(path,{method:'POST',headers:{'Content-Type':'application/json','X-XMRig-Control':'1'},body});
// CPU requests preserve GPU state and unsaved editor text.
for(const action of ['start','stop'])$('cpu-'+action).addEventListener('click',async()=>{try{await gpuRequest('/cpu/'+action,'{}');$('cpu-message').textContent='XMRig '+action+' requested.';}catch(e){$('cpu-message').textContent=e.message;}});
async function gpuLoad(){try{$('gpu-editor').value=JSON.stringify(await request('/gpu/config'),null,2);$('gpu-message').textContent='GPU settings loaded.';}catch(e){$('gpu-message').textContent=e.message;}}
for(const action of ['start','stop'])$('gpu-'+action).addEventListener('click',async()=>{try{await gpuRequest('/gpu/'+action,'{}');$('gpu-message').textContent='GPU '+action+' requested.';}catch(e){$('gpu-message').textContent=e.message;}});
$('gpu-save').addEventListener('click',async()=>{try{const config=JSON.parse($('gpu-editor').value);await gpuRequest('/gpu/config',JSON.stringify(config));$('gpu-message').textContent='GPU settings saved.';}catch(e){$('gpu-message').textContent=e.message;}});
$('gpu-reload').addEventListener('click',gpuLoad);gpuLoad();
