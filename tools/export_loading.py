"""Export the hall shots as the loading screen's pictures.

assets/shots/<hall>-firstperson.png (5120x1440 in-game shots, no HUD, first person) ->
loading/<hall>.jpg at 3840x1080 (the whole 32:9 shot). The game cover-crops it to the screen's
shape, so a 16:9 screen shows the middle and an ultrawide screen the whole width. Kindlewastes
drops its bottom 240 rows (the player's hand shows on the stairs); Revelwood is brightened (the
same lift as Town Portal's night shots). Run: python -I tools/export_loading.py
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, '..', 'assets', 'shots')
DEST = os.path.join(HERE, '..', 'loading')
W = 3840
# File names as main.cpp loads them, in halls.h kHalls order.
HALLS = ('springlands', 'revelwood', 'nomad', 'veilwater', 'albaneve', 'kindlewastes')
CROP_BOTTOM = {'kindlewastes': 240}
TARGET_MEAN, MAX_GAIN = {'revelwood': (0.06, 6.0)}, None


def brighten(im, mean, max_gain):
    a = np.asarray(im, dtype=np.float32) / 255
    lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
    luma = (lin * [0.2126, 0.7152, 0.0722]).sum(-1).mean()
    gain = min(max_gain, max(1.0, mean / max(luma, 1e-4)))
    lin = lin * gain
    lin = lin / (1 + np.maximum(lin - 0.8, 0) * 2)  # soft roll-off instead of clipping highlights
    out = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(np.clip(lin, 0, 1), 1 / 2.4) - 0.055)
    return Image.fromarray(np.clip(out * 255 + 0.5, 0, 255).astype(np.uint8)), gain


def main():
    os.makedirs(DEST, exist_ok=True)
    for hall in HALLS:
        im = Image.open(os.path.join(SHOTS, hall + '-firstperson.png')).convert('RGB')
        if hall in CROP_BOTTOM:
            im = im.crop((0, 0, im.width, im.height - CROP_BOTTOM[hall]))
        note = ''
        if hall in TARGET_MEAN:
            im, gain = brighten(im, *TARGET_MEAN[hall])
            note = f' (brightened x{gain:.1f})'
        im = im.resize((W, round(im.height * W / im.width)), Image.LANCZOS)
        path = os.path.join(DEST, hall + '.jpg')
        im.save(path, quality=88, optimize=True)
        print(f'{hall}: {im.width}x{im.height}, {os.path.getsize(path) // 1024} KB{note}')


main()
