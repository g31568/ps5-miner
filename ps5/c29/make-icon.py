#!/usr/bin/env python3
"""Create the probe's required launcher icon using only the Python standard library."""
from pathlib import Path
import struct
import sys
import zlib

# A distinct C29 mark keeps this probe separate from the installed XMRig title.
glyphs = (('01111', '11000', '11000', '11000', '11000', '11000', '01111'),
          ('01110', '11011', '00011', '00110', '01100', '11000', '11111'),
          ('01110', '11011', '11011', '01111', '00011', '00110', '11100'))
rows = []
for y in range(512):
    row = bytearray(b'\0')
    for x in range(512):
        gx, gy = (x - 84) // 20, (y - 186) // 20
        letter, column = divmod(gx, 6)
        ink = 0 <= letter < 3 and column < 5 and 0 <= gy < 7 and glyphs[letter][gy][column] == '1'
        row.extend((93, 230, 190) if ink else (15, 24, 40))
    rows.append(row)

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

output = Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 512, 512, 8, 2, 0, 0, 0))
                   + chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))
