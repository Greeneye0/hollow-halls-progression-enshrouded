"""Nexus images for Hollow Halls Progression, in the Embervale Teleport style (dark boxes with a gold
outline, Georgia bold, dashed gold arrows; helpers from UnlockMapMarkers tools/make_nexus_images.py).

Sources: dist/nexus/raw/ (loading-veilwater.png, a 5120x1440 capture of the loading screen;
menu.webp, the world map's Hollow Halls menu) and loading/*.jpg (the six loading pictures).
Writes dist/nexus/images/. Run: python -I tools/make_nexus_images.py
"""
import math
import os
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RAW = os.path.join(ROOT, 'dist', 'nexus', 'raw')
OUT = os.path.join(ROOT, 'dist', 'nexus', 'images')
LOADING = os.path.join(ROOT, 'loading')
GOLD = (232, 200, 130)
HOT = (255, 214, 140)
TXT = (230, 220, 200)
BOX = (22, 18, 14, 215)
HALLS = [('springlands', 'Springlands'), ('revelwood', 'Revelwood'), ('nomad', 'Nomad Highlands'),
         ('veilwater', 'Veilwater Basin'), ('albaneve', 'Albaneve Summits'), ('kindlewastes', 'Kindlewastes')]


def font(bold, size):
    return ImageFont.truetype(f"C:/Windows/Fonts/{'georgiab' if bold else 'georgia'}.ttf", size)


def callout(im, x, y, title, sub, target=None, size=40, anchor_side=None):
    """Box at (x, y) with a title and sub line; optional dashed line + arrow to target."""
    over = Image.new('RGBA', im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    ft, fs = font(True, size), font(False, int(size * 0.72))
    w = int(max(d.textlength(title, font=ft), d.textlength(sub, font=fs) if sub else 0) + size * 1.2)
    h = int(size * (2.45 if sub else 1.55))
    d.rounded_rectangle((x, y, x + w, y + h), radius=int(size * 0.35), fill=BOX, outline=GOLD + (235,), width=3)
    d.text((x + size * 0.55, y + size * 0.28), title, font=ft, fill=HOT + (255,))
    if sub:
        d.text((x + size * 0.57, y + size * 1.42), sub, font=fs, fill=TXT + (240,))
    if target:
        side = anchor_side or ('right' if target[0] > x + w else 'left' if target[0] < x else 'bottom')
        sx, sy = {'right': (x + w, y + h / 2), 'left': (x, y + h / 2), 'bottom': (x + w / 2, y + h), 'top': (x + w / 2, y)}[side]
        tx, ty = target
        dist = math.hypot(tx - sx, ty - sy)
        ux, uy = (tx - sx) / dist, (ty - sy) / dist
        t, end = 0.0, dist - 22
        while t < end:
            a, b = t, min(t + 16, end)
            d.line((sx + ux * a, sy + uy * a, sx + ux * b, sy + uy * b), fill=GOLD + (240,), width=5)
            t += 26
        px, py = -uy, ux
        base = (tx - ux * 26, ty - uy * 26)
        d.polygon([(tx, ty), (base[0] + px * 12, base[1] + py * 12), (base[0] - px * 12, base[1] - py * 12)], fill=GOLD + (250,))
    im.alpha_composite(over)
    return w, h


def wordmark(im, x, y, size=58, sub=None):
    over = Image.new('RGBA', im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    wm, ws = font(True, size), font(False, int(size * 0.5))
    title = 'HOLLOW HALLS PROGRESSION'
    w = int(max(d.textlength(title, font=wm), d.textlength(sub, font=ws) if sub else 0) + size * 1.1)
    h = int(size * (2.2 if sub else 1.55))
    if x is None:
        x = (im.width - w) // 2
    d.rounded_rectangle((x, y, x + w, y + h), radius=14, fill=(22, 18, 14, 205), outline=GOLD + (220,), width=3)
    d.text((x + size * 0.55, y + size * 0.2), title, font=wm, fill=HOT + (255,))
    if sub:
        d.text((x + size * 0.57, y + size * 1.4), sub, font=ws, fill=TXT + (235,))
    im.alpha_composite(over)


def cover(im, w, h, cy=0.5):
    """Scale and crop im to fill w x h (cy: where the crop sits vertically, 0 top .. 1 bottom)."""
    s = max(w / im.width, h / im.height)
    im = im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)
    x, y = (im.width - w) // 2, int((im.height - h) * cy)
    return im.crop((x, y, x + w, y + h))


def grade(im):
    im = ImageEnhance.Contrast(im.convert('RGB')).enhance(1.06)
    return ImageEnhance.Color(im).enhance(1.06).convert('RGBA')


def save(im, name):
    os.makedirs(OUT, exist_ok=True)
    im.convert('RGB').save(os.path.join(OUT, name), quality=92)
    print('wrote', name, im.size)


# 1. Header (1300x372): the wordmark in the top part; Nexus covers the bottom ~45% with its title bar.
hdr = grade(cover(Image.open(os.path.join(LOADING, 'veilwater.jpg')), 1300, 372, 0.45))
hdr = Image.alpha_composite(hdr, Image.new('RGBA', hdr.size, (0, 0, 0, 60)))
wordmark(hdr, None, 40, size=50)
save(hdr, 'header.jpg')

# 2. The loading screen (16:9 crop of the 32:9 capture).
shot = Image.open(os.path.join(RAW, 'loading-veilwater.png')).convert('RGB')
load = grade(cover(shot, 2560, 1440).resize((1920, 1080), Image.LANCZOS))
wordmark(load, 40, 36, size=46, sub='travel back to the Return Beacons you reached')
callout(load, 1250, 640, 'A loading screen per hall', 'its own picture while the area loads', size=34)
callout(load, 1200, 955, 'In your language', 'the caption and progress follow the game', target=(1058, 997), size=34, anchor_side='left')
save(load, 'loading-screen.jpg')

# 3. The map menu on a hall picture.
bg = cover(Image.open(os.path.join(LOADING, 'albaneve.jpg')).convert('RGB'), 1920, 1080)
bg = bg.filter(ImageFilter.GaussianBlur(6)).convert('RGBA')
bg = Image.alpha_composite(bg, Image.new('RGBA', bg.size, (0, 0, 0, 110)))
menu = Image.open(os.path.join(RAW, 'menu.webp')).convert('RGBA')
mh = 980
menu = menu.resize((round(menu.width * mh / menu.height), mh), Image.LANCZOS)
mx, my = 1920 - menu.width - 120, 50
over = Image.new('RGBA', bg.size, (0, 0, 0, 0))
ImageDraw.Draw(over).rounded_rectangle((mx - 6, my - 6, mx + menu.width + 6, my + menu.height + 6), radius=10,
                                       outline=GOLD + (230,), width=4)
bg.alpha_composite(menu, (mx, my))
bg.alpha_composite(over)
wordmark(bg, 60, 50, size=46, sub='the Hollow Halls, one beacon at a time')
# Button rows in the scaled menu: Return Beacon 1 at ~41% of its height, 6 at ~92%.
s = mh / 1157
callout(bg, 60, 330, 'Click a Hollow Halls on the world map', 'its marker opens this menu', target=(mx + 50 * s, my + 560 * s), size=36)
callout(bg, 60, 560, 'One button per Return Beacon', 'every beacon you reached in that hall', target=(mx + 170 * s, my + 715 * s), size=36)
callout(bg, 60, 790, 'Pick one and you are there', 'next to the beacon, after a loading screen', target=(mx + 170 * s, my + 1060 * s), size=36)
save(bg, 'map-menu.jpg')

# 4. All six halls' pictures.
grid = Image.new('RGBA', (1920, 1080), (16, 12, 9, 255))
cw, ch, gx, gy, top = 600, 338, 30, 30, 160
for i, (key, name) in enumerate(HALLS):
    tile = grade(cover(Image.open(os.path.join(LOADING, key + '.jpg')), cw, ch))
    x, y = gx + (i % 3) * (cw + gx), top + (i // 3) * (ch + 100)
    grid.alpha_composite(tile, (x, y))
    d = ImageDraw.Draw(grid)
    d.rounded_rectangle((x - 3, y - 3, x + cw + 3, y + ch + 3), radius=8, outline=GOLD + (230,), width=3)
    f = font(True, 34)
    d.text((x + (cw - d.textlength(name, font=f)) / 2, y + ch + 22), name, font=f, fill=HOT + (255,))
wordmark(grid, None, 30, size=44, sub='a loading screen for each of the six Hollow Halls')
save(grid, 'six-halls.jpg')

# 5. The mod card's preview (shown about 176 px wide): one hall picture and a title big enough to read there.
card = grade(cover(Image.open(os.path.join(LOADING, 'veilwater.jpg')), 1920, 1080, 0.55))
shade = Image.new('RGBA', card.size, (0, 0, 0, 0))
sd = ImageDraw.Draw(shade)
for y in range(560):  # dark at the top, where the title sits, fading out
    sd.line((0, y, 1920, y), fill=(8, 6, 4, int(200 * (1 - y / 560) ** 1.4)))
card = Image.alpha_composite(card, shade)
d = ImageDraw.Draw(card)
for text, size, y, colour in (('HOLLOW HALLS', 190, 40, HOT), ('PROGRESSION', 150, 250, HOT), ('Return Beacon travel', 80, 430, TXT)):
    f = font(True, size)
    x = (1920 - d.textlength(text, font=f)) / 2
    d.text((x + 6, y + 6), text, font=f, fill=(0, 0, 0, 200))
    d.text((x, y), text, font=f, fill=colour + (255,))
save(card, 'card.jpg')
