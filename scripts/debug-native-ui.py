#!/usr/bin/env python3
"""Scoped native-title commands, state reports and one-frame captures."""
from pathlib import Path
import argparse,socket,subprocess,re,json,os,struct,zlib
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('action',choices=['command','report','capture']);parser.add_argument('value',nargs='?',default='');args=parser.parse_args()
console='10.1.1.51';host='10.1.1.111'
def send(path):
    with socket.create_connection((console,9021),5) as s:s.sendall(path.read_bytes());s.shutdown(socket.SHUT_WR)
def receive(path):
    with socket.socket() as s:
        s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind((host,19021));s.listen(1);s.settimeout(20);send(path)
        c,_=s.accept();data=bytearray()
        with c:
            c.settimeout(15)
            while b:=c.recv(65536):data.extend(b)
    return bytes(data)
def compile_helper(source):
    src=root/'build/native-ui-debug.c';src.write_text(source);target=src.with_suffix('.elf')
    env=os.environ.copy();env['LLVM_CONFIG']='/opt/homebrew/opt/llvm/bin/llvm-config'
    subprocess.run([str(root/'toolchain/ps5-payload-sdk/bin/prospero-clang'),'-O2',str(src),'-o',str(target)],env=env,check=True);return target
# Match only the mounted title; no general process access or filesystem export.
head='#include <stdio.h>\n#include <dirent.h>\n#include <string.h>\n#include <unistd.h>\n#include <sys/socket.h>\n#include <netinet/in.h>\n#include <arpa/inet.h>\n'
locate='DIR*d=opendir("/mnt/sandbox");if(!d)return 1;struct dirent*e;char path[512]={0};while((e=readdir(d)))if(!strncmp(e->d_name,"PPSA99999_",10)){snprintf(path,sizeof(path),"/mnt/sandbox/%s/download0/%s",e->d_name,name);break;}closedir(d);if(!*path)return 2;'
if args.action=='command':
    command=args.value+'\n'
    source=head+'/* One consumed marker exercises this title only. */\nint main(){const char*name="xmrig-native-test.once";'+locate+'FILE*f=fopen(path,"wb");if(!f)return 3;int r=fputs('+json.dumps(command)+',f);return fclose(f)||r<0;}\n'
    send(compile_helper(source));print('Command sent:',args.value)
elif args.action=='report':
    data=receive(root/'build/native-app-report.elf');(root/'logs/native-options-report.txt').write_bytes(data);text=data.decode(errors='replace')
    states=re.findall(r'FILE /[^\n]+/xmrig-native-ui.json\n([^\n]+)',text)
    if states:
        state=json.loads(states[-1]);(root/'logs/native-options-state.json').write_text(json.dumps(state,indent=2));print(json.dumps(state,indent=2))
    else:
        # Never expose a previous launch's state as a live report.
        (root/'logs/native-options-state.json').unlink(missing_ok=True)
        (root/'logs/native-http-url.txt').unlink(missing_ok=True)
        raise SystemExit('No live native UI state found.\n'+text[-6000:])
    urls=re.findall(r'HTTP status (http://[^\s]+)',text);ports=re.findall(r'HTTP API\s+0\.0\.0\.0:(\d+)',text)
    port=json.loads(states[-1]).get('http_port',0) if states else 0
    if port:(root/'logs/native-http-url.txt').write_text(f'http://{console}:{port}/2/summary')
    else:(root/'logs/native-http-url.txt').unlink(missing_ok=True)
else:
    source=head+'/* Export only this app\'s explicitly captured framebuffer. */\nint main(){const char*name="xmrig-native-frame.rgba";'+locate+f'int c=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in a={{0}};a.sin_family=AF_INET;a.sin_port=htons(19021);inet_pton(AF_INET,"{host}",&a.sin_addr);if(connect(c,(void*)&a,sizeof(a)))return 3;FILE*f=fopen(path,"rb");if(!f)return 4;char b[65536];size_t n;while((n=fread(b,1,sizeof(b),f))){{size_t o=0;while(o<n){{ssize_t k=send(c,b+o,n-o,0);if(k<=0)return 5;o+=k;}}}}fclose(f);close(c);return 0;}}\n'
    data=receive(compile_helper(source));assert len(data)==1920*1080*4
    # Encode the unmodified RGBA capture with standard-library PNG chunks.
    def chunk(kind,payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    raw=b''.join(b'\0'+data[y*7680:(y+1)*7680] for y in range(1080))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1920,1080,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    output=root/'logs'/('native-options-'+(args.value or 'screen')+'.png');output.write_bytes(png);print(output)
