#!/usr/bin/env python3
"""Generate launcher and web icons from the native screen's shared Monero orange mark."""
from pathlib import Path
import subprocess,struct,zlib
root=Path(__file__).resolve().parents[1];work=root/'build/native-app';work.mkdir(parents=True,exist_ok=True)
source=work/'icon-generator.cpp';binary=work/'icon-generator'
# Call the renderer's exact logo predicate for both output sizes and backgrounds.
source.write_text('''#include "native-logo.hpp"
#include <cstdio>
int main(int argc,char**){bool web=argc>1;int size=web?128:512;
for(int y=0;y<size;y++)for(int x=0;x<size;x++){
 bool ink=web?native::logo_pixel(x,y,size):(x>=96&&x<416&&y>=96&&y<416&&native::logo_pixel(x-96,y-96,320));
 // Keep transparent web pixels and the launcher's existing dark background.
 unsigned char p[4]={static_cast<unsigned char>(ink?native::monero_red:16),static_cast<unsigned char>(ink?native::monero_green:19),static_cast<unsigned char>(ink?native::monero_blue:25),static_cast<unsigned char>(ink?255:0)};
 fwrite(p,1,web?4:3,stdout);
}}\n''')
subprocess.run(['/usr/bin/clang++','-std=c++20','-O2','-I'+str(root/'homebrew'),str(source),'-o',str(binary)],check=True)
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
def png(size,channels,pixels):
    stride=size*channels;raw=b''.join(b'\0'+pixels[y*stride:(y+1)*stride] for y in range(size))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6 if channels==4 else 2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b'')
(work/'icon0.png').write_bytes(png(512,3,subprocess.check_output([str(binary)])))
(work/'web-icon.png').write_bytes(png(128,4,subprocess.check_output([str(binary),'web'])))
