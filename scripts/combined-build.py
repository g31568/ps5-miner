#!/usr/bin/env python3
"""Compile the C29 adapter and reuse the pinned RADV platform link recipe."""
from pathlib import Path
import os, shutil, subprocess

def prepare(root, work, sdk):
    # Dependency sources stay read-only; all generated link metadata belongs to this app.
    driver=root/'build/ps5-vulkan-radv'
    gpu_sdk=driver/'.deps/native/ps5-payload-sdk'
    archive=driver/'.deps/native/radv-release/lib/libvulkan_radeon.ps5.a'
    if not archive.exists():raise SystemExit('Build the pinned RADV dependency before combining miners')
    compiler=sdk/'bin/prospero-clang'
    def run(*args):subprocess.run([str(x) for x in args],check=True,cwd=root)
    objects=[]
    # Isolate C29's portable Blake2 dispatch from XMRig's runtime SIMD selection.
    hash_names=['blake2b_IV','rx_blake2b','rx_blake2b_compress','rx_blake2b_compress_integer','rx_blake2b_default','rx_blake2b_final','rx_blake2b_init','rx_blake2b_init_key','rx_blake2b_init_param','rx_blake2b_update','rxa2_blake2b_long']
    hash_flags=['-D'+name+'=gpu_'+name for name in hash_names]
    for shader in ['seed','trim','bucket']:
        run('glslc','--target-env=vulkan1.0','-O','-mfmt=c',root/f'ps5/c29/{shader}.comp','-o',root/f'build/ps5-c29/{shader}.spv.inc')
    # Compile C29 with the same ABI and include surfaces used by the pinned driver.
    for name,source in [('gpu-engine',root/'ps5/c29/native-engine.c'),('gpu-manager',root/'ps5/manager.c'),('gpu-blake2b',root/'xmrig/crypto/randomx/blake2/blake2b.c')]:
        obj=work/(name+'.o')
        run(compiler,'-std=c11','-O2','-DMOM_PS5_RADV=1','-DPS5_COMBINED_MINER=1',*hash_flags,
            '-I'+str(driver/'.deps/native/radv-release/include'),'-I'+str(root/'build/ps5-c29'),'-I'+str(root/'xmrig'),'-c',source,'-o',obj)
        objects.append(obj)
    obj=work/'gpu-solver.o'
    run(sdk/'bin/prospero-clang++','-std=c++17','-O2','-fno-exceptions','-fno-rtti','-c',root/'ps5/c29/solver.cpp','-o',obj)
    objects.append(obj)
    obj=work/'gpu-pool.o'
    run(sdk/'bin/prospero-clang++','-std=c++17','-O2','-fno-exceptions','-fno-rtti','-DMOM_POOL_MBEDTLS=1',
        '-DMBEDTLS_USER_CONFIG_FILE="'+str(root/'ps5/mbedtls-config.h')+'"',
        '-I'+str(root/'build/MbedTLS/include'),'-c',root/'ps5/pool.cpp','-o',obj)
    objects.append(obj)
    # Linux compiler builtins are ELF, unlike macOS compiler-rt archives.
    resource=work/'compiler-resource';builtins=resource/'lib/linux/libclang_rt.builtins-x86_64.a';builtins.parent.mkdir(parents=True,exist_ok=True)
    if not builtins.exists():
        context=os.environ.get('PS5_DOCKER_CONTEXT',os.environ.get('DOCKER_CONTEXT','default'))
        with builtins.open('wb') as output:
            subprocess.run(['docker','--context',context,'run','--rm','mom-ps5-radv-builder:llvm18',
                'cat','/usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.builtins-x86_64.a'],stdout=output,check=True)
    local=work/'driver';local.mkdir(exist_ok=True)
    if not (local/'tooling').exists():(local/'tooling').symlink_to(driver/'tooling',target_is_directory=True)
    fake=work/'resource-path';fake.write_text('#!/bin/sh\n# Only used by the upstream link recipe to locate compiler-rt.\nprintf "%s\\n" "'+str(resource)+'"\n');fake.chmod(0o755)
    env=dict(os.environ,PS5_CLANG=str(fake))
    command='source "$1/tools/radv-link.sh"; radv_link_recipe "$2" "$3" "$4"; printf "%s\\0" "${radv_linker_script[@]}" "${radv_link_flags[@]}" "${radv_link_inputs[@]}"'
    flags=subprocess.check_output(['bash','-ec',command,'bash',str(driver),str(local),str(gpu_sdk),str(archive)],env=env).decode().split('\0')[:-1]
    # XMRig needs the native DNS provider; RADV's generic platform resolver is an EAI_FAIL stub.
    flags=[flag for flag in flags if flag not in ['--defsym=getaddrinfo=ps5_getaddrinfo','--defsym=freeaddrinfo=ps5_freeaddrinfo']]
    # Keep the generated export map consistent with the two native resolver imports.
    local_map=local/'build/radv-platform-local.map'
    local_map.write_text(local_map.read_text().replace('        getaddrinfo;\n','').replace('        freeaddrinfo;\n',''))
    # Weak optional Mesa entries must resolve to NULL rather than firmware imports.
    flags+=['-T',str(root/'build/ps5-c29-native/mesa-weak-null.ld')]
    flags += [str(root/f'build/ps5-mbedtls/library/lib{name}.a') for name in ['mbedtls','mbedx509','mbedcrypto']]
    flags += [str(root/f'build/ps5-c29-native/stubs/lib{name}.so') for name in ['SceAgc','SceAgcDriver']]
    # Retain only libuv's missing libc helpers; never link the payload mmap/syscall layer.
    helpers=work/'libc-helpers';helpers.mkdir(exist_ok=True)
    helper_objects=[]
    for name in ['mktemp','scandir','syslog','termios']:
        obj=helpers/(name+'.o')
        obj.write_bytes(subprocess.check_output([str(sdk/'bin/llvm-ar'),'p',str(sdk/'target/lib/libc.a'),name+'.o']))
        helper_objects.append(obj)
    safe_libc=work/'libnative-helpers.a'
    if safe_libc.exists():safe_libc.unlink()
    run(sdk/'bin/llvm-ar','rcs',safe_libc,*helper_objects)
    flags.append(str(safe_libc))
    # Force the SDK's working sceNet resolver implementation ahead of optional firmware stubs.
    resolver=work/'netdb-native.o'
    resolver.write_bytes(subprocess.check_output([str(sdk/'bin/llvm-ar'),'p',str(sdk/'target/lib/libc.a'),'netdb.o']))
    objects.append(resolver)
    return objects,flags,gpu_sdk
