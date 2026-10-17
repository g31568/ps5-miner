#!/usr/bin/env python3
"""Exercise both engines on the console and retain only non-secret status evidence."""
from pathlib import Path
import importlib.util,io,json,socket,threading,time,urllib.request
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('control',ROOT/'scripts/combined-control.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
with c.connect() as f:
    base=c.sandbox(f);state=json.loads(c.read(f,base+'xmrig-native-ui.json'));url=state['web_url'].rstrip('/')
    gpu=json.loads(c.read(f,'/data/homebrew/PPSA99529/c29-pool.json'))
    gpu['runtime_seconds']=0;gpu['max_graphs']=0;gpu['ca_file']='/app0/assets/cacert.pem'
def request(path,doc=None):
    headers={'Content-Type':'application/json','X-XMRig-Control':'1'} if doc is not None else {}
    r=urllib.request.Request(url+path,data=json.dumps(doc).encode() if doc is not None else None,headers=headers)
    with urllib.request.urlopen(r,timeout=10) as response:return json.load(response)
request('/gpu/config',gpu)
# Use the existing wallet/proxy on the pool's verified plaintext RandomX endpoint.
current=request('/config');cpu=current['config'];pool=cpu['pools'][0]
pool['url']=gpu['url']+':'+str(gpu['port']-10000);pool['user']=gpu['login'];pool['pass']='ps5-cpu'
pool['socks5']=gpu['proxy_host']+':'+str(gpu['proxy_port']) if gpu.get('proxy_host') else None
cpu['cpu']['rx']=[-1]*8
request('/config',{'revision':current['revision'],'config':cpu})
stop=threading.Event();logs=ROOT/'logs';logs.mkdir(exist_ok=True)
def capture():
    with socket.create_connection(('10.1.1.51',3232),5) as s,(logs/'combined-mining.log').open('wb') as out:
        s.settimeout(.5);deadline=time.monotonic()+2
        while time.monotonic()<deadline:
            try:s.recv(65536)
            except socket.timeout:break
        while not stop.is_set():
            try:b=s.recv(65536)
            except socket.timeout:continue
            if not b:break
            out.write(b);out.flush()
t=threading.Thread(target=capture);t.start();time.sleep(2)
with c.connect() as f:f.storbinary('STOR '+c.sandbox(f)+'xmrig-native-test.once',io.BytesIO(b'START\n'))
statuses=[]
( ROOT/'logs/combined-web-url.txt').write_text(url+'\n')
try:
    for i in range(180):
        time.sleep(1)
        try:s=request('/status')
        except Exception as e:print('Status unavailable:',type(e).__name__,flush=True);break
        statuses.append(s)
        if i%15==0:print(json.dumps(s),flush=True)
        if s['hashrate']>0 and s['gpu']['graphs']>=64 and s['gpu']['accepted']>0:break
finally:
    stop.set();t.join(2);(logs/'combined-status.json').write_text(json.dumps({'url':url,'samples':statuses},indent=2))
if statuses:print('FINAL',json.dumps(statuses[-1]),flush=True)
