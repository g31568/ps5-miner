#!/usr/bin/env python3
"""Scope controller markers and state reads to the combined title's live sandbox."""
from pathlib import Path
from ftplib import FTP
import argparse,json,io,struct,zlib,os
ROOT=Path(__file__).resolve().parents[1]
# Keep every command scoped to the selected console and fixed title.
HOST=os.environ.get('PS5_HOST','10.1.1.51')
def connect():
    f=FTP();f.connect(HOST,2121,10);f.login('anonymous','codex');return f
def sandbox(f):
    f.cwd('/mnt/sandbox');entries=[];f.retrlines('LIST',entries.append)
    names=[line.split()[-1] for line in entries if line.split()[-1].startswith('PPSA99531_')]
    if len(names)!=1:raise SystemExit('Combined title is not running')
    return '/mnt/sandbox/'+names[0]+'/download0/'
def read(f,path):
    b=bytearray();f.retrbinary('RETR '+path,b.extend);return bytes(b)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('action',choices=['command','report','configure','capture']);p.add_argument('value',nargs='?',default='');a=p.parse_args()
    with connect() as f:
        base=sandbox(f)
        if a.action=='command':f.storbinary('STOR '+base+'xmrig-native-test.once',io.BytesIO((a.value+'\n').encode()));print('Command sent')
        elif a.action=='report':
            d=json.loads(read(f,base+'xmrig-native-ui.json'));print(json.dumps({k:d[k] for k in ['mining','connected','hashrate','threads','web_url','notice']},indent=2))
        elif a.action=='configure':
            # Import existing settings in memory only; never publish credentials in logs or ZIPs.
            cpu=json.loads(read(f,'/data/homebrew/PPSA99999/assets/config.json'))
            cpu['cpu']['rx']=[-1]*8;cpu['randomx']['scratchpad_prefetch_mode']=3
            f.storbinary('STOR '+base+'xmrig-native.json',io.BytesIO(json.dumps(cpu).encode()))
            gpu=json.loads(read(f,'/data/homebrew/PPSA99529/c29-pool.json'))
            gpu['runtime_seconds']=0;gpu['max_graphs']=0;gpu['ca_file']='/app0/assets/cacert.pem'
            f.storbinary('STOR '+base+'c29-pool.json',io.BytesIO(json.dumps(gpu).encode()))
            print('Existing CPU/GPU pool settings copied to combined title only')
        else:
            data=read(f,base+'xmrig-native-frame.rgba');assert len(data)==1920*1080*4
            # Encode the unmodified capture with the standard library; no image dependencies.
            def chunk(kind,payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
            raw=b''.join(b'\0'+data[y*7680:(y+1)*7680] for y in range(1080))
            png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1920,1080,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
            (ROOT/'logs').mkdir(exist_ok=True);output=ROOT/'logs/combined-screen.png'
            output.write_bytes(png);print(output)
