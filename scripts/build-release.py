#!/usr/bin/env python3
"""Build and package a public release without local build caches or console data."""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess, tarfile, zipfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--version', default='2026.10.17')
args = parser.parse_args()
if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.version):
    parser.error('Use only letters, digits, dots, underscores and hyphens in the version')
out = ROOT / 'dist' / args.version
out.mkdir(parents=True, exist_ok=True)
# Use the documented production defaults even if the shell contains benchmark overrides.
env = dict(os.environ, PS5_NATIVE_DIRECT_ALIGNMENT='16384', PS5_NATIVE_PREFETCH='3',
           PS5_NATIVE_CPU_YIELD='1', PS5_NATIVE_RANDOMX_MODE='fast', PS5_NATIVE_CPU_ASM='none')
subprocess.run(['python3', str(ROOT/'scripts/build-homebrew.py')], cwd=ROOT, env=env, check=True)
app = ROOT / 'build/native-app/PPSA99531'
manifest = json.loads((app/'build-manifest.json').read_text())
for name, expected in manifest['files'].items():
    if hashlib.sha256((app/name).read_bytes()).hexdigest() != expected:
        raise SystemExit('Package checksum mismatch: '+name)
cpu = json.loads((app/'assets/config.json').read_text())
gpu = json.loads((app/'assets/gpu-config.json').read_text())
assert cpu['pools'][0]['user'] == gpu['login'] == 'YOUR_WALLET'
assert cpu['pools'][0]['socks5'] is None and not gpu.get('proxy_host')
assert cpu['pools'][0]['url'] == 'gulf.moneroocean.stream:128'
assert manifest['app_name'] == 'XMR Miner'
stem = 'PS5-XMR-Miner-'+args.version
binary = out/(stem+'.zip')
shutil.copy2(ROOT/'build/native-app/PPSA99531.zip', binary)
with zipfile.ZipFile(binary) as archive:
    if archive.testzip() is not None:
        raise SystemExit('Release ZIP failed integrity verification')
source = out/(stem+'-source.tar.gz')
# An explicit source allowlist excludes symlinks, private logs, SDK caches and databases.
entries = ['README.md', 'BUILDING.md', 'THIRD_PARTY.md', 'LICENSE', 'VALIDATION.md',
           '.gitignore', '.github', 'docs', 'licenses', 'SECURITY.md', 'homebrew', 'ps5', 'sycl', 'xmrig', 'config', 'patches', 'scripts', 'payload', 'tests']
def public_owner(info):
    # Keep the build host's account identity out of source archive metadata.
    info.uid = info.gid = 0
    info.uname = info.gname = 'root'
    return info
with tarfile.open(source, 'w:gz') as archive:
    for entry in entries:
        base = ROOT/entry
        paths = [base] if base.is_file() else sorted(base.rglob('*'))
        for path in paths:
            if not path.is_file() or path.is_symlink() or '__pycache__' in path.parts or '.git' in path.parts:
                continue
            archive.add(path, arcname=stem+'/'+str(path.relative_to(ROOT)), recursive=False, filter=public_owner)
for name in ['BUILDING.md', 'README.md', 'THIRD_PARTY.md', 'LICENSE']:
    shutil.copy2(ROOT/name, out/name)
files = sorted(path for path in out.iterdir() if path.is_file() and path.name != 'SHA256SUMS')
(out/'SHA256SUMS').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n' for path in files))
print('Release:', binary)
print('Source:', source)
print('Checksums:', out/'SHA256SUMS')
