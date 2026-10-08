#!/usr/bin/env python3
"""df_art.py <game folder> <art folder> [--no-downloads]

The launch window's and the icon's art (install.sh), written to the data folder's art/ (never to this project):
  logo.png      the game's logo with transparency: from Steam's store page for the game (app 32620 / 32630); without a
                download, Delta Force's dagger cut from its own splash screen (Dfbase.pff DFSPLASH.PCX); Delta Force 2
                then has none (the window writes the title)
  backdrop.jpg  Steam's library background (a red texture for Delta Force, a gold one for Delta Force 2)
  menu.png/jpg  without that download, the game's own main-menu screen (DF_MAIN.PCX / DF2_MAIN.JPG): the launch
                window's backdrop then (the icon keeps a plain tile; the screen is too busy behind the logo)
Only the standard library: NovaLogic's PFF3 archives and 8-bit PCX images are read here, PNG is written here.
"""
import os, struct, sys, urllib.request, zlib

def pff_read(path, name):
    """a file from a PFF3 archive: 20-byte header (size, 'PFF3', count, record size, records' offset), then records of
    (deleted, offset, size, time, name[16])"""
    with open(path, 'rb') as f:
        hs, magic, n, rs, ro = struct.unpack('<I4sIII', f.read(20))
        if magic != b'PFF3': return None
        f.seek(ro); recs = f.read(n * rs)
        for k in range(n):
            r = recs[k * rs:(k + 1) * rs]
            dead, off, size, _ = struct.unpack_from('<IIII', r, 0)
            if not dead and r[16:32].split(b'\0')[0].decode('latin1').upper() == name.upper():
                f.seek(off); return f.read(size)
    return None

def pcx_rgb(d):
    """8-bit run-length PCX with its palette at the end (or 24-bit, three planes): width, height, RGB rows"""
    xmin, ymin, xmax, ymax = struct.unpack_from('<HHHH', d, 4); bpp, planes = d[3], d[65]; bpl = struct.unpack_from('<H', d, 66)[0]
    w, h = xmax - xmin + 1, ymax - ymin + 1; i = 128; raw = bytearray(); need = bpl * planes * h
    while len(raw) < need and i < len(d):
        b = d[i]; i += 1
        if b >= 0xc0: raw += bytes([d[i]]) * (b & 0x3f); i += 1
        else: raw.append(b)
    rows = []
    for y in range(h):
        if planes == 1 and bpp == 8:
            pal = d[-768:]; rows.append(b''.join(pal[3 * c:3 * c + 3] for c in raw[y * bpl:y * bpl + w]))
        else:
            base = y * bpl * planes; R, G, B = raw[base:base + w], raw[base + bpl:base + bpl + w], raw[base + 2 * bpl:base + 2 * bpl + w]
            rows.append(bytes(v for t in zip(R, G, B) for v in t))
    return w, h, rows

def png_write(path, w, h, rows, alpha=False):
    def chunk(t, dat): return struct.pack('>I', len(dat)) + t + dat + struct.pack('>I', zlib.crc32(t + dat) & 0xffffffff)
    raw = b''.join(b'\0' + r for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6 if alpha else 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def download(url, path, magic):
    try:
        with urllib.request.urlopen(url, timeout=20) as r: data = r.read()
    except Exception:
        return False
    if not data.startswith(magic) or len(data) < 20000: return False
    with open(path + '.part', 'wb') as f: f.write(data)
    os.replace(path + '.part', path); return True

def dagger(game, out):
    """Delta Force's logo from its splash screen (the dagger and the name on the left; the right half is text): the
    dark red background made transparent around it"""
    d = pff_read(os.path.join(game, 'Dfbase.pff'), 'DFSPLASH.PCX')
    if not d: return False
    w, h, rows = pcx_rgb(d); x0, x1, y0, y1 = 30, 350, 20, 420
    out_rows = []
    for y in range(y0, y1):
        r = rows[y]; line = bytearray()
        for x in range(x0, x1):
            R, G, B = r[3 * x], r[3 * x + 1], r[3 * x + 2]
            reddish = R > G + 25 and R > B + 25 and max(G, B) < 90          # the backdrop's dark red
            a = 0 if reddish or (R + G + B) < 60 else 255
            line += bytes((R, G, B, a))
        out_rows.append(bytes(line))
    png_write(out, x1 - x0, y1 - y0, out_rows, alpha=True); return True

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]; nodl = '--no-downloads' in sys.argv
    game, art = args[0], args[1]; os.makedirs(art, exist_ok=True)
    df2 = os.path.exists(os.path.join(game, 'Df2.exe')); app = 32630 if df2 else 32620
    cdn = 'https://cdn.cloudflare.steamstatic.com/steam/apps/%d/' % app
    got = []
    if not nodl and download(cdn + 'logo.png', os.path.join(art, 'logo.png'), b'\x89PNG'): got.append('logo (Steam)')
    if not nodl and download(cdn + 'library_hero.jpg', os.path.join(art, 'backdrop.jpg'), b'\xff\xd8\xff'): got.append('backdrop (Steam)')
    if not os.path.exists(os.path.join(art, 'logo.png')) and not df2 and dagger(game, os.path.join(art, 'logo.png')): got.append('logo (splash screen)')
    if not os.path.exists(os.path.join(art, 'backdrop.jpg')):
        if df2:
            d = pff_read(os.path.join(game, 'Df2.pff'), 'DF2_MAIN.JPG')
            if d: open(os.path.join(art, 'menu.jpg'), 'wb').write(d); got.append('backdrop (main menu)')
        else:
            d = pff_read(os.path.join(game, 'Dfbase.pff'), 'DF_MAIN.PCX')
            if d: png_write(os.path.join(art, 'menu.png'), *pcx_rgb(d)); got.append('backdrop (main menu)')
    print(', '.join(got) or 'none')
    return 0 if got else 1

if __name__ == '__main__':
    sys.exit(main())
