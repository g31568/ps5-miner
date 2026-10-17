#!/usr/bin/env python3
"""Deploy only the combined title, verify readback, and capture fresh console startup."""
from pathlib import Path
from ftplib import FTP,error_perm
import argparse,hashlib,io,json,socket,threading,time,os
ROOT=Path(__file__).resolve().parents[1]; TITLE='PPSA99531'
# Allow release users to select their own console without editing source.
HOST=os.environ.get('PS5_HOST','10.1.1.51')
def control(command):
    with socket.create_connection((HOST,9111),5) as s:
        s.settimeout(60);s.sendall((command+'\n').encode());return s.recv(8192).decode().strip()
def ftp():
    f=FTP();f.connect(HOST,2121,15);f.login('anonymous','codex');return f
def deploy():
    if 'count=0' not in control('procs'):raise SystemExit('Close the foreground application before deployment')
    app=ROOT/'build/native-app'/TITLE
    manifest=json.loads((app/'build-manifest.json').read_text())
    for name,digest in manifest.get('sources',{}).items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:raise SystemExit('Rebuild before deployment: source differs: '+name)
    with ftp() as f:
        remote='/data/homebrew/'+TITLE
        try:f.mkd(remote)
        except error_perm:
            b=bytearray();f.retrbinary('RETR '+remote+'/sce_sys/param.json',b.extend)
            # Accept current and previous names when upgrading this title.
            if json.loads(b)['localizedParameters']['en-US']['titleName'] not in ('XMR Miner','PS5 CPU + GPU Miner','ps5 miner'):raise SystemExit('Existing title is unrelated')
        # SELF toggles encrypted reads for an exact binary readback.
        response=f.sendcmd('SELF');restore='disabled' in response.lower()
        if not restore:f.sendcmd('SELF')
        try:
            for path in sorted(app.rglob('*')):
                name=str(path.relative_to(app));target=remote+'/'+name
                if path.is_dir():
                    try:f.mkd(target)
                    except error_perm:pass
                else:
                    f.storbinary('STOR '+target+'.upload',io.BytesIO(path.read_bytes()),blocksize=262144)
                    f.rename(target+'.upload',target)
                    h=hashlib.sha256();f.retrbinary('RETR '+target,h.update,blocksize=262144)
                    if h.hexdigest()!=hashlib.sha256(path.read_bytes()).hexdigest():raise SystemExit('Readback mismatch: '+name)
            print('Combined title upload and SHA-256 readback PASS',flush=True)
        finally:
            if restore:f.sendcmd('SELF')
    # A scan skips installed titles; update the system's separate display caches.
    from importlib.util import spec_from_file_location,module_from_spec
    spec=spec_from_file_location('metadata',ROOT/'scripts/refresh-title-metadata.py')
    metadata=module_from_spec(spec);spec.loader.exec_module(metadata);metadata.refresh()
def capture(seconds):
    logs=ROOT/'logs';logs.mkdir(exist_ok=True)
    with socket.create_connection((HOST,3232),5) as s:
        s.settimeout(.5)
        # Drain earlier replay before this title's launch.
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            try:s.recv(65536)
            except socket.timeout:break
        stop=threading.Event()
        def read():
            with (logs/'combined-console.log').open('wb') as out:
                while not stop.is_set():
                    try:b=s.recv(65536)
                    except socket.timeout:continue
                    if not b:break
                    out.write(b);out.flush()
        t=threading.Thread(target=read);t.start()
        print(control('launch '+TITLE),flush=True)
        try:time.sleep(seconds)
        finally:stop.set();t.join(2)
    print(control('procs'),flush=True)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--deploy',action='store_true');p.add_argument('--seconds',type=int,default=20);a=p.parse_args()
    if a.deploy:deploy()
    capture(a.seconds)
