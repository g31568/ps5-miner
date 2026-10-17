#!/usr/bin/env python3
"""Refresh only PPSA99531's cached system name/icon without uninstalling it."""
from pathlib import Path
from ftplib import FTP
import hashlib, io, json, os, socket, subprocess, time, urllib.request, zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
# Console services are supplied by the user's existing homebrew environment.
HOST = os.environ.get('PS5_HOST', '10.1.1.51')
SQLITE_SHA256 = '1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032'

def read(ftp, path):
    data = bytearray()
    ftp.retrbinary('RETR ' + path, data.extend)
    return bytes(data)

def refresh():
    # Refuse to refresh a running title; deployment already closes this app.
    with socket.create_connection((HOST, 9111), 5) as connection:
        connection.sendall(b'procs\n')
        if 'count=0' not in connection.recv(8192).decode():
            raise SystemExit('Close the foreground app before refreshing metadata')
    archive = BUILD / 'sqlite-amalgamation-3500400.zip'
    if not archive.exists():
        archive.write_bytes(urllib.request.urlopen('https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip', timeout=30).read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SQLITE_SHA256:
        raise SystemExit('SQLite source checksum mismatch')
    with zipfile.ZipFile(archive) as source:
        source.extractall(BUILD)
    sqlite = BUILD / 'sqlite-amalgamation-3500400'
    helper = BUILD / 'refresh-title-metadata.elf'
    env = os.environ.copy()
    env.setdefault('LLVM_CONFIG', '/opt/homebrew/opt/llvm/bin/llvm-config')
    subprocess.run([str(ROOT/'toolchain/ps5-payload-sdk/bin/prospero-clang'), '-O2', '-DSQLITE_THREADSAFE=1', '-DSQLITE_OMIT_LOAD_EXTENSION', '-I'+str(sqlite), str(ROOT/'payload/refresh-title-metadata.c'), str(sqlite/'sqlite3.c'), '-o', str(helper)], env=env, check=True)
    app = ROOT / 'build/native-app/PPSA99531/sce_sys'
    param, icon = (app/'param.json').read_bytes(), (app/'icon0.png').read_bytes()
    identity = json.loads(param)
    assert identity['titleId'] == 'PPSA99531'
    assert identity['localizedParameters']['en-US']['titleName'] == 'XMR Miner'
    backup = BUILD / ('metadata-backup-' + time.strftime('%Y%m%d-%H%M%S'))
    backup.mkdir()
    with FTP() as ftp:
        ftp.connect(HOST, 2121, 10)
        ftp.login('anonymous', 'codex')
        targets = {
            '/user/app/PPSA99531/sce_sys/param.json': param,
            '/user/app/PPSA99531/sce_sys/icon0.png': icon,
            '/user/app/PPSA99531/icon0.png': icon,
            '/user/appmeta/PPSA99531/param.json': param,
            '/user/appmeta/PPSA99531/icon0.png': icon,
            '/system_data/priv/appmeta/PPSA99531/param.json': param,
        }
        # Back up all existing metadata before publishing the new display assets.
        for index, path in enumerate(targets):
            old = read(ftp, path)
            if path.endswith('param.json'):
                assert json.loads(old)['titleId'] == 'PPSA99531'
            (backup / str(index)).write_bytes(old)
        (backup/'paths.json').write_text(json.dumps(list(targets), indent=2))
        for path, data in targets.items():
            ftp.storbinary('STOR '+path+'.refresh', io.BytesIO(data))
            ftp.rename(path+'.refresh', path)
            assert read(ftp, path) == data, path
        # Remove the prior result so a repeated deployment cannot pass on stale logs.
        try:
            ftp.sendcmd('DELE /data/xmr-miner-metadata.log')
        except Exception as error:
            if not str(error).startswith('550'):
                raise
        with socket.create_connection((HOST, 9021), 5) as connection:
            connection.sendall(helper.read_bytes())
            connection.shutdown(socket.SHUT_WR)
        for _ in range(20):
            time.sleep(1)
            try:
                result = read(ftp, '/data/xmr-miner-metadata.log').decode()
            except Exception:
                continue
            if 'metadata refresh status=0 ' in result:
                print('System metadata and icon refresh PASS', flush=True)
                return
            if 'metadata refresh status=' in result:
                raise SystemExit(result)
    raise SystemExit('Metadata helper did not report completion')

if __name__ == '__main__':
    refresh()
