#!/usr/bin/env python3
"""Verify independent CPU/GPU controls and shared start/stop on the live title."""
from pathlib import Path
import importlib.util,io,json,time,urllib.request,urllib.error
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('control',ROOT/'scripts/combined-control.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
# Discover the live port after each launch instead of reusing stale evidence.
with c.connect() as f:
    url=json.loads(c.read(f,c.sandbox(f)+'xmrig-native-ui.json'))['web_url'].rstrip('/')
def request(path,doc=None):
    r=urllib.request.Request(url+path,data=json.dumps(doc).encode() if doc is not None else None,headers={'Content-Type':'application/json','X-XMRig-Control':'1'} if doc is not None else {})
    with urllib.request.urlopen(r,timeout=10) as response:return json.load(response)
def until(predicate,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        s=request('/status')
        if predicate(s):return s
        time.sleep(1)
    raise AssertionError('Console state did not reach the expected condition')
def command(value):
    with c.connect() as f:f.storbinary('STOR '+c.sandbox(f)+'xmrig-native-test.once',io.BytesIO((value+'\n').encode()))
checks=[]
def passed(name):checks.append(name);print('PASS',name,flush=True)
initial=request('/status')
# Cold CPU start must not allocate or launch the GPU worker.
request('/cpu/start',{});until(lambda s:s['mining'] and s['hashrate']>0,90)
assert request('/status')['gpu']['running']==initial['gpu']['running']
passed('CPU start preserves GPU state')
request('/gpu/start',{});until(lambda s:s['hashrate']>0 and s['gpu']['graphs']>=4)
request('/cpu/stop',{});stopped=until(lambda s:not s['mining'] and s['hashrate']==0)
graphs=stopped['gpu']['graphs'];until(lambda s:not s['mining'] and s['gpu']['graphs']>graphs)
passed('Web XMRig stop leaves C29 completing graphs')
request('/cpu/stop',{});request('/cpu/start',{});until(lambda s:s['hashrate']>0 and s['gpu']['running'])
passed('Web XMRig restart leaves C29 running')
command('BUTTON 400');stopped=until(lambda s:not s['mining'])
graphs=stopped['gpu']['graphs'];until(lambda s:not s['mining'] and s['gpu']['graphs']>graphs)
command('BUTTON 400');until(lambda s:s['hashrate']>0 and s['gpu']['running'])
passed('L1 toggles only XMRig')
command('BUTTON 800');until(lambda s:not s['gpu']['running'] and not s['gpu']['stopping'] and s['mining'] and s['hashrate']>0)
command('BUTTON 800');until(lambda s:s['gpu']['running'] and s['gpu']['graphs']>=4 and s['hashrate']>0)
passed('R1 toggles only C29')
config=request('/gpu/config')
try:request('/gpu/config',config);raise AssertionError('Running GPU config was mutable')
except urllib.error.HTTPError as e:assert e.code==409
passed('GPU config edits refused while running')
request('/gpu/stop',{});stopped=until(lambda s:not s['gpu']['running'])
time.sleep(3);s=request('/status');assert s['mining'] and s['hashrate']>0 and not s['gpu']['faulted']
passed('GPU stops and drains while CPU keeps mining')
# Read-only config validation leaves the saved file bytes unchanged.
assert request('/gpu/config')==config
passed('Stopping GPU preserves its saved configuration')
request('/gpu/start',{});until(lambda s:s['gpu']['graphs']>=4 and s['hashrate']>0)
passed('GPU restarts and mines beside CPU')
command('BUTTON 8000');until(lambda s:not s['mining'] and not s['gpu']['running'])
passed('Square stops both engines')
command('BUTTON 8000');final=until(lambda s:s['mining'] and s['connected'] and s['hashrate']>0 and s['gpu']['graphs']>=8)
passed('Square restarts both engines')
command('CAPTURE');time.sleep(2)
(ROOT/'logs/combined-controls.json').write_text(json.dumps({'checks':checks,'initial':initial,'final':final},indent=2))
print(json.dumps(final),flush=True)
