#!/usr/bin/env python3
"""Build a native FSELF title on macOS without the payload CRT or kernel helpers."""
from pathlib import Path
import hashlib,json,shlex,subprocess,shutil,zipfile,os
# SDK wrappers require the LLVM location for direct compiler invocations.
os.environ.setdefault('LLVM_CONFIG', shutil.which('llvm-config') or '/opt/homebrew/opt/llvm/bin/llvm-config')
root=Path(__file__).resolve().parents[1]; upstream=root/'upstream/native-app'; sdk=root/'toolchain/ps5-payload-sdk'; work=root/'build/native-app'; native=upstream/'tooling/native'
work.mkdir(parents=True,exist_ok=True)
def run(*args):
    subprocess.run([str(x) for x in args],check=True,cwd=root)
# Native framework revision is fixed so runtime hashes and converter match.
revision='2f672d1c2f508e26f82ce6e27cef289a0861413c'
if not upstream.exists():
    run('git','init',upstream)
    run('git','-C',upstream,'remote','add','origin','https://github.com/blackbearreloaded/ps5-native-app-boilerplate.git')
    run('git','-C',upstream,'fetch','--depth','1','origin',revision)
    run('git','-C',upstream,'checkout','--detach','FETCH_HEAD')
if subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip()!=revision:raise SystemExit('Unexpected native-app revision; preserving checkout')
tool=work/'ps5-native-tool';libc_builder=work/'libc-builder'
if not tool.exists():run('/usr/bin/clang++','-std=c++20','-O2',*[native/f'{name}.cpp' for name in ['native_app_builder','self_container','elf_object','sce_module_writer']],'-lz','-o',tool)
if not libc_builder.exists():run('/usr/bin/clang++','-std=c++20','-O2',native/'libc_builder.cpp','-o',libc_builder)
run(libc_builder,native/'runtime/api-surface.txt',native/'runtime/imports.txt',work/'libc.raw.elf')
run(tool,'self','--sign','--in',work/'libc.raw.elf','--out',work/'libc.prx')
for name,expected in [('libc.raw.elf','8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb'),('libc.prx','e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036')]:
    if hashlib.sha256((work/name).read_bytes()).hexdigest()!=expected:raise SystemExit(f'Runtime hash mismatch: {name}')
# Reuse XMRig's compiled core, excluding its payload entrypoint and console App.
# x86 RandomX JIT is available only when the native core enables assembly.
core_build=root/'build/ps5-xmrig-native'
run('env','PS5_XMRIG_BUILD_DIR='+str(core_build),'PS5_XMRIG_ASM=ON','bash',root/'scripts/build-xmrig.sh')
flags=(core_build/'CMakeFiles/xmrig-notls.dir/flags.make').read_text().splitlines()
options=[]
for key in ['CXX_DEFINES','CXX_INCLUDES']:
    options+=shlex.split(next(line.split('=',1)[1] for line in flags if line.startswith(key+' =')))
compiler=sdk/'bin/prospero-clang++';objects=[]
# Compare direct-memory alignment through native APIs; this does not certify huge pages.
direct_alignment=int(os.environ.get('PS5_NATIVE_DIRECT_ALIGNMENT','16384'),0)
if direct_alignment not in (16384,2097152):raise SystemExit('Direct alignment must be 16384 or 2097152')
# Repeated PS5 measurements favor mode 3; keep overrides for comparisons.
prefetch_mode=int(os.environ.get('PS5_NATIVE_PREFETCH','3'))
if prefetch_mode not in range(4):raise SystemExit('Prefetch mode must be 0-3')
cpu_yield=os.environ.get('PS5_NATIVE_CPU_YIELD','1')
if cpu_yield not in ('0','1'):raise SystemExit('CPU yield must be 0 or 1')
# Keep the pinned renderer untouched; a debug marker captures only this app's frame.
renderer=(upstream/'src/demo_renderer.cpp').read_text()
renderer=renderer.replace('namespace ps5::demo\n{','extern "C" void xmrigNativeCaptureFrame(const std::uint32_t *);\n\nnamespace ps5::demo\n{',1)
assert renderer.count('            draw(canvas);')==1
renderer=renderer.replace('            draw(canvas);','            draw(canvas);\n            xmrigNativeCaptureFrame(static_cast<const std::uint32_t *>(buffers[back].data));')
(work/'native-renderer.cpp').write_text(renderer)
for source in [root/'homebrew/main.cpp',root/'homebrew/native-compat.cpp',root/'homebrew/native-memory.cpp',root/'homebrew/native-keyboard.cpp',root/'homebrew/native-web.cpp',root/'homebrew/native-gpu.cpp',work/'native-renderer.cpp',native/'app_crt.cpp']:
    obj=work/(source.stem+'.o');run(compiler,'-std=c++20','-O2','-fno-rtti','-DPS5_NATIVE_DIRECT_ALIGNMENT='+str(direct_alignment),'-I'+str(upstream/'src'),*options,'-c',source,'-o',obj);objects.append(obj)
# Vendor the small MIT QR encoder; no external QR service is used.
obj=work/'qrcodegen.o'
run(sdk/'bin/prospero-clang','-O2','-c',root/'homebrew/vendor/qrcodegen/qrcodegen.c','-o',obj)
objects.append(obj)
link_args=shlex.split((core_build/'CMakeFiles/xmrig-notls.dir/link.txt').read_text())
core=[core_build/p for p in link_args if p.endswith('.o') and Path(p).name not in ['xmrig.cpp.o','App.cpp.o','App_unix.cpp.o']]
# Compile only memory/cache overrides with the native-app allocator switch.
for name in ['crypto/common/VirtualMemory.cpp','crypto/common/VirtualMemory_unix.cpp','crypto/rx/RxCache.cpp','crypto/cn/CnHash.cpp','base/net/tools/TcpServer.cpp','base/net/http/HttpServer.cpp']:
    source=root/'upstream/xmrig/src'/name;obj=work/(source.stem+'-native.o')
    run(compiler,'-std=c++20','-O2','-fno-rtti','-DXMRIG_NATIVE_APP',*options,*(['-UXMRIG_FEATURE_ASM'] if name=='crypto/cn/CnHash.cpp' else []),'-c',source,'-o',obj)
    core=[p for p in core if p.name!=source.name+'.o'];objects.append(obj)
# RandomX copies templates; CryptoNight's unused global code patcher stays disabled above.
source=root/'upstream/xmrig/src/crypto/randomx/jit_compiler_x86_static.S';obj=work/'randomx-templates-native.o'
run(sdk/'bin/prospero-clang','-DXMRIG_NATIVE_APP',*options,'-c',source,'-o',obj)
core=[p for p in core if p.name!=source.name+'.o'];objects.append(obj)
# Donation changes also recreate the native strategy during a live reload.
network=(root/'upstream/xmrig/src/net/Network.cpp').read_text()
anchor='void xmrig::Network::onConfigChanged(Config *config, Config *previousConfig)\n{'
assert network.count(anchor)==1
network=network.replace(anchor,anchor+"""
    // The stock CLI assumes donation stays fixed for the process lifetime.
    if (config->pools().donateLevel() != previousConfig->pools().donateLevel()) {
        delete m_donate;
        m_donate = nullptr;
        if (config->pools().donateLevel() > 0) m_donate = new DonateStrategy(m_controller, this);
    }
""")
source=work/'Network-native.cpp';source.write_text(network);obj=work/'Network-native.o'
run(compiler,'-std=c++20','-O2','-fno-rtti',*options,'-c',source,'-o',obj)
core=[p for p in core if p.name!='Network.cpp.o'];objects.append(obj)
# The app owns atomic JSON persistence; upstream reload must not rewrite the editor file.
base=(root/'upstream/xmrig/src/base/kernel/Base.cpp').read_text()
assert base.count('const bool saved = config->save();')==1
base=base.replace('const bool saved = config->save();','const bool saved = true; // Native UI already persisted the validated JSON atomically.')
source=work/'Base-native.cpp';source.write_text(base);obj=work/'Base-native.o'
run(compiler,'-std=c++20','-O2','-fno-rtti',*options,'-c',source,'-o',obj)
core=[p for p in core if p.name!='Base.cpp.o'];objects.append(obj)
# Native accepted sockets need the same unsupported-cloexec handling as pipes.
uv_options=[]
uv_flags=(root/'build/ps5-libuv-1.48/CMakeFiles/uv_a.dir/flags.make').read_text().splitlines()
for key in ['C_DEFINES','C_INCLUDES']:
    uv_options+=shlex.split(next(line.split('=',1)[1] for line in uv_flags if line.startswith(key+' =')))
obj=work/'uv-core-native.o'
run(sdk/'bin/prospero-clang','-O2','-fPIC','-DUV_PS5_NATIVE_APP',*uv_options,'-c',root/'upstream/libuv/src/unix/core.c','-o',obj)
objects.append(obj)
libs=[sdk/'target/lib'/n for n in ['libc++.a','libc++abi.a','libunwind.a','libc.a']]
libs += [root/'build/ps5-libuv-1.48/libuv.a',core_build/'src/3rdparty/argon2/libargon2.a',core_build/'src/3rdparty/argon2/libargon2-sse2.a',core_build/'src/3rdparty/argon2/libargon2-ssse3.a',core_build/'src/3rdparty/argon2/libargon2-avx2.a',core_build/'src/3rdparty/argon2/libargon2-avx512f.a',core_build/'src/3rdparty/argon2/libargon2-xop.a']
libs.append(core_build/'libxmrig-asm.a')
# Generate the missing public CommonDialog stub without modifying the SDK.
stubs=work/'stubs';stubs.mkdir(exist_ok=True)
for stub in (sdk/'target/lib').glob('*.so'):
    target=stubs/stub.name
    if not target.exists():target.symlink_to(stub)
common=work/'common-dialog-stub.c';common.write_text('/* Link metadata only; firmware supplies this function. */\nvoid sceCommonDialogInitialize(void) {}\n')
run(sdk/'bin/prospero-clang','-shared','-nostdlib','-nodefaultlibs','-fPIC','-Wl,-soname,libSceCommonDialog.sprx',common,'-o',stubs/'libSceCommonDialog.so')
# Combine C29 and XMRig in one native process with RADV's platform wrappers.
import runpy
prepare=runpy.run_path(str(root/'scripts/combined-build.py'))['prepare']
gpu_objects,gpu_flags,gpu_sdk=prepare(root,work,sdk)
objects+=gpu_objects
# RADV supplies the shared C++ runtime; keep a single copy of its allocation ABI.
libs=[p for p in libs if p.name not in ('libc++.a','libc++abi.a','libunwind.a','libc.a')]
# Retain native dialog providers even though their addresses are checked first.
run(sdk/'bin/prospero-lld','-L'+str(stubs),'-L'+str(sdk/'target/lib'),'--eh-frame-hdr','--defsym=__eh_frame_start=ADDR(.eh_frame)','--defsym=__eh_frame_end=ADDR(.eh_frame)+SIZEOF(.eh_frame)','--defsym=__eh_frame_hdr_start=ADDR(.eh_frame_hdr)','--defsym=__eh_frame_hdr_end=ADDR(.eh_frame_hdr)+SIZEOF(.eh_frame_hdr)','--gc-sections','--version-script',native/'app-symbols.map','-e','_start','-o',work/'llvm-pie.elf',*objects,*core,*gpu_flags,'--start-group',*libs,'--end-group','--no-as-needed','-lSceImeDialog','-lSceCommonDialog','--as-needed',*sorted((sdk/'target/lib').glob('*.so')))
run(tool,'link','--in',work/'llvm-pie.elf','--out',work/'eboot.elf','--stub-dir',stubs,'--stub',root/'build/ps5-c29-native/stubs/libSceAgc.so','--stub',root/'build/ps5-c29-native/stubs/libSceAgcDriver.so','--module-sdk','0x02000009','--companion-sdk','0x08050001','--file-name','eboot.elf')
title='PPSA99531';app=work/title
# Remove only the generated package tree; private console settings are elsewhere.
if app.exists():shutil.rmtree(app)
for directory in ['sce_sys','sce_module','assets','assets/web','licenses']:(app/directory).mkdir(parents=True,exist_ok=True)
run(tool,'self','--sign','--in',work/'eboot.elf','--out',app/'eboot.bin','--magic','0x1D3D154F')
shutil.copy2(root/'build/ps5-vulkan-radv/runtime/libc.prx',app/'sce_module/libc.prx')
# Keep the launcher name aligned with the native and web screens.
# Games context admitted the dataset plus cache; Media context refused the cache.
param=json.loads((upstream/'sce_sys/param.json').read_text());param.update(titleId=title,conceptId='99531',contentId='UP9000-PPSA99531_00-PS5CPUGPUMINER01',applicationCategoryType=0,contentBadgeType=1);param.pop('gameIntent',None);param['localizedParameters']['en-US']['titleName']='XMR Miner'
# Power Saver capability is attribute4 bit 0; preserve any other capabilities.
param['attribute4']=param.get('attribute4',0)|1
(app/'sce_sys/param.json').write_text(json.dumps(param,indent=2))
# Generate the orange tile icon from the screen's shared logo geometry.
run('python3',root/'scripts/build-native-icon.py')
shutil.copy2(work/'icon0.png',app/'sce_sys/icon0.png')
# Use AES-NI reported by the console while retaining the non-executable interpreter.
config=json.loads((root/'config/xmrig-p2pool.json').read_text());config['pools'][0]['url']='gulf.moneroocean.stream:128';config['pools'][0]['user']='YOUR_WALLET';config['pools'][0]['pass']='PS5-CPU';config['pools'][0]['socks5']=None;config['donate-level']=0;config['log-file']='/download0/xmrig-native.log';config['cpu']['priority']=0;config['cpu']['hw-aes']=True;config['cpu']['rx']=[-1]*8;config['cpu']['yield']=cpu_yield=='1';config['http']['port']=0;config['api']={'worker-id':'PS5-CPU'}
# Public starter settings contain no wallet or previous project's private pool configuration.
gpu_config={'url':'gulf.moneroocean.stream','port':10128,'login':'YOUR_WALLET','pass':'PS5-GPU','is_tls':True,'tls_verify':True,'ca_file':'/app0/assets/cacert.pem','runtime_seconds':0,'max_graphs':0}
(app/'assets/gpu-config.json').write_text(json.dumps(gpu_config,indent=2))
shutil.copy2(root/'build/ps5-c29/cacert.pem',app/'assets/cacert.pem')
# Compare dataset modes without changing the legacy payload configuration.
mode=os.environ.get('PS5_NATIVE_RANDOMX_MODE','fast')
if mode not in ('light','fast'):raise SystemExit('Native RandomX mode must be light or fast')
config['randomx']['mode']=mode
config['randomx']['scratchpad_prefetch_mode']=prefetch_mode
# Compare AMD-specific JIT scheduling while retaining the eight-worker profile.
asm_mode=os.environ.get('PS5_NATIVE_CPU_ASM','none')
if asm_mode not in ('none','ryzen'):raise SystemExit('Native assembly mode must be none or ryzen')
config['cpu']['asm']=False if asm_mode=='none' else asm_mode
# Build the full dataset with eight initializer threads to shorten startup.
config['randomx']['init']=8 if mode=='fast' else 1
(app/'assets/config.json').write_text(json.dumps(config,indent=2))
for src,dest in [(upstream/'LICENSE','native-app-GPL.txt'),(root/'upstream/xmrig/LICENSE','xmrig-GPL.txt')]:shutil.copy2(src,app/'licenses'/dest)
# Carry third-party license texts alongside the statically linked release.
for source,name in [(root/'LICENSE','PS5-XMR-Miner-GPL.txt'),(root/'upstream/libuv/LICENSE','libuv-LICENSE.txt'),(root/'build/MbedTLS/LICENSE','MbedTLS-LICENSE.txt'),(root/'build/ps5-vulkan-radv/LICENSE','PS5-Vulkan-LICENSE.txt')]:
    shutil.copy2(source,app/'licenses'/name)
shutil.copy2(root/'THIRD_PARTY.md',app/'licenses/THIRD_PARTY.md')
shutil.copy2(root/'build/ps5-vulkan-radv/NOTICE.md',app/'licenses/PS5-Vulkan-NOTICE.md')
# Mesa retains multiple upstream licenses; include the complete pinned license directory.
mesa=root/'build/ps5-vulkan-radv/.deps/work/radv-src'
shutil.copytree(mesa/'licenses',app/'licenses/Mesa',dirs_exist_ok=True)
shutil.copy2(mesa/'docs/license.rst',app/'licenses/Mesa-license.rst')
# Retain the notices for copied and statically linked dependency code.
shutil.copytree(root/'licenses',app/'licenses/additional',dirs_exist_ok=True)
shutil.copy2(root/'ps5/third-party/nlohmann/LICENSE.MIT',app/'licenses/nlohmann-json-MIT.txt')
# The native server serves only these fixed, local editor assets.
for asset in (root/'homebrew/web').iterdir():
    if asset.is_file():shutil.copy2(asset,app/'assets/web'/asset.name)
# The header, favicon and launcher share the native screen's logo geometry.
shutil.copy2(work/'web-icon.png',app/'assets/web/icon.png')
shutil.copy2(root/'homebrew/vendor/qrcodegen/qrcodegen.h',app/'licenses/qrcodegen-MIT.txt')
# The baked dashboard glyphs retain the bundled font's redistribution notice.
shutil.copy2(root/'homebrew/vendor/vera/LICENSE.txt',app/'licenses/Bitstream-Vera-LICENSE.txt')
# Record exactly which source and binary revisions produced the deployable title.
manifest={'native_framework':revision,'xmrig':'b2ca72480c58d197e18c885d9fc1a0c8d517e60a','project_name':'PS5 XMR Miner','app_name':'XMR Miner','build_type':'release','target_firmware':'13.60','firmware_validation':'See VALIDATION.md for prior console evidence; this build requires separate runtime verification','donate_level':0,'threads':8,'randomx_mode':mode,'hardware_aes':True,'assembly':asm_mode,'thread_range':[1,16],'direct_memory_alignment':direct_alignment,'scratchpad_prefetch_mode':prefetch_mode,'cpu_yield':cpu_yield=='1','gpu_algorithm':'c29','gpu_defaults':{'trim_rounds':68,'seed_threads':256,'redistribute_threads':256,'trim_threads':1024,'stagger':32,'output_cache':0},'files':{str(p.relative_to(app)):hashlib.sha256(p.read_bytes()).hexdigest() for p in app.rglob('*') if p.is_file() and p.name!='build-manifest.json'}}
# Fingerprint the integration sources separately from the deployable file inventory.
source_files=[root/'THIRD_PARTY.md']
# Include headers, shaders, build recipes and starter configs in source provenance.
for directory in ['homebrew','ps5','sycl','xmrig','scripts','patches','config']:
    source_files += [p for p in (root/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
manifest['sources']={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source_files)}
manifest['gpu_dependencies']={'PS5_Vulkan':'252bf912a745e3566a93b69b098bdc8ec4048837','PS5_Mesa':'47e9eb61ee7e1112194557e1e047178562827bc6','PS5_PayloadSDK':'b83202be73e930050e00de0f1b4d9ec46c0391bf','MbedTLS':'e185d7fd85499c8ce5ca2a54f5cf8fe7dbe3f8df'}
(app/'build-manifest.json').write_text(json.dumps(manifest,indent=2))
archive=work/(title+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in app.rglob('*'):
        if p.is_file():
            info=zipfile.ZipInfo(str(p.relative_to(work)));info.external_attr=0o100777<<16;info.compress_type=zipfile.ZIP_DEFLATED;z.writestr(info,p.read_bytes())
run(tool,'self','--inspect','--file',app/'eboot.bin');print('Native app:',archive)
