#!/usr/bin/env python3
"""Embed only the verified PPSA99999 app files in a scoped installer payload."""
from pathlib import Path
import hashlib,json,subprocess,zlib,sys
root=Path(__file__).resolve().parents[1];build=root/'build';app=build/'native-app/PPSA99999';manifest=json.loads((app/'build-manifest.json').read_text())
blob=bytearray();entries=[]
for path in sorted(app.rglob('*')):
    if not path.is_file():continue
    name=str(path.relative_to(app));data=path.read_bytes()
    if name!='build-manifest.json' and hashlib.sha256(data).hexdigest()!=manifest['files'].get(name):raise SystemExit(f'Manifest mismatch: {name}')
    if '..' in name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/.-_' for c in name):raise SystemExit('Unsafe app path')
    entries.append((name,len(blob),len(data),zlib.crc32(data)&0xffffffff));blob.extend(data)
(build/'native-install.bin').write_bytes(blob)
header='/* Fixed app manifest generated from checksum-verified native build. */\nstruct app_file {const char *path;unsigned offset,size,crc;};\nstatic const struct app_file app_files[]={\n'
header+=''.join('{'+json.dumps(n)+f',{o},{s},0x{c:08x}'+'},\n' for n,o,s,c in entries)+'};\n'
(build/'native-install-assets.h').write_text(header)
(build/'native-install-image.S').write_text(f'/* Read-only app files embedded for one scoped installation. */\n.section .rodata\n.global app_image\n.global app_image_end\napp_image:\n.incbin "{build}/native-install.bin"\napp_image_end:\n.section .note.GNU-stack,"",@progbits\n')
cc=root/'toolchain/ps5-payload-sdk/bin/prospero-clang'
subprocess.run([str(cc),'-O2','-c',str(build/'native-install-image.S'),'-o',str(build/'native-install-image.o')],check=True)
# Updates use the scoped, closed-title helper and retain the original backup.
update='--update' in sys.argv
source='native-update.c' if update else 'native-install.c'
output='native-update.elf' if update else 'xmrig-native-install.elf'
subprocess.run([str(cc),'-Wall','-Wextra','-O2','-I'+str(build),str(root/'payload'/source),str(build/'native-install-image.o'),'-lSceSystemService','-o',str(build/output)],check=True)
print('Scoped installer:',build/output)
