# Generates assets/hair/hair_strands.png: each column is one strand's colour, dark at the top (root), lighter at the bottom (tip).
import os, random, struct, zlib

W, H = 256, 64
random.seed(7)
MAIN = [(116, 73, 42), (132, 87, 50), (162, 128, 100)]  # the three middle browns of the swatch
HIGHLIGHT = (217, 200, 182)                              # the pale stripe: used rarely, for blond streaks

def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))

columns = []
for _ in range(W):
    base = lerp(random.choice(MAIN), random.choice(MAIN), random.random())  # blend of two random main browns
    if random.random() < 0.08:                                               # about 1 in 12 strands gets a pale streak
        base = lerp(base, HIGHLIGHT, 0.45)
    columns.append(base)

def pixel(x, y):
    t = y / (H - 1)                       # 0 at the root (image top), 1 at the tip (image bottom)
    shade = 0.45 + 0.65 * t               # dark root, slightly brighter tip
    return bytes(max(0, min(255, int(c * shade))) for c in columns[x])

raw = b''.join(b'\x00' + b''.join(pixel(x, y) for x in range(W)) for y in range(H))

def chunk(tag, data):
    body = tag + data
    return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

os.makedirs('assets/hair', exist_ok=True)
with open('assets/hair/hair_strands.png', 'wb') as f:
    f.write(b'\x89PNG\r\n\x1a\n')
    f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)))
    f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
    f.write(chunk(b'IEND', b''))