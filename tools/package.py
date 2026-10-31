"""Packs the release zip: dist/Hollow-Halls-Progression-<version>.zip.

The same files go into the game folder and the dedicated server folder: the Greeneye Loader
(dbgcore.dll), greeneye/hollow-halls-progression/ (the release DLL from build.bat, mod.json, the
ini and the loading screen pictures), the readme and the license.
Run build.bat first, then: python -I tools/package.py
"""
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOADER = os.path.join(ROOT, '..', 'GreeneyeLoader', 'bin', 'dbgcore.dll')
MOD = 'greeneye/hollow-halls-progression/'

version = json.load(open(os.path.join(ROOT, 'mod.json')))['version']
dll = os.path.join(ROOT, 'bin', 'loader', 'hollow-halls-progression.dll')
if b'admin.enable' in open(dll, 'rb').read():
    sys.exit('bin\\loader holds a dev build; run build.bat')
files = [
    (LOADER, 'dbgcore.dll'),
    (dll, MOD + 'hollow-halls-progression.dll'),
    (os.path.join(ROOT, 'mod.json'), MOD + 'mod.json'),
    (os.path.join(ROOT, 'hollow-halls-progression.ini'), MOD + 'hollow-halls-progression.ini'),
    (os.path.join(ROOT, 'dist', 'README.txt'), 'README.txt'),
    (os.path.join(ROOT, 'LICENSE'), 'LICENSE'),
]
for name in sorted(os.listdir(os.path.join(ROOT, 'loading'))):
    if name.endswith('.jpg'):
        files.append((os.path.join(ROOT, 'loading', name), MOD + 'loading/' + name))
out = os.path.join(ROOT, 'dist', f'Hollow-Halls-Progression-{version}.zip')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for src, arc in files:
        z.write(src, arc)
print(out, os.path.getsize(out), 'bytes,', len(files), 'files')
