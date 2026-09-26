"""Capture actual settings renderings with isolated preferences and a local gallery."""
import argparse
import html
import json
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['Clean side panel', 'Icon dashboard', 'GD Melee toolkit',
         'Radial', 'Wide tabs', 'Simple', 'Legacy New', 'Legacy Old (v0.6.6)']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--iso', required=True, type=Path)
    ap.add_argument('--exe', type=Path, default=ROOT / 'build-integration/port/Release/melee_port.exe')
    ap.add_argument('--styles', default='0,1,2,3,4')
    ap.add_argument('--pages', default='home,Video')
    ap.add_argument('--sizes', default='800x600,1280x720')
    ap.add_argument('--backend', default='d3d12', choices=['d3d11', 'd3d12'])
    ap.add_argument('--palette', type=int, default=0, choices=range(4))
    ap.add_argument('--icon-variant', choices=['A', 'B'], default='B')
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    records = json.loads((out/'captures.json').read_text()) if (out/'captures.json').is_file() else []
    for style in map(int, args.styles.split(',')):
        for size in args.sizes.split(','):
            for page in args.pages.split(','):
                key = '%s-style%d-%s-%s%s' % (args.backend, style, size, page,
                     '-palette%d' % args.palette if args.palette else '')
                if args.icon_variant == 'A':
                    key += '-iconsA'
                ini = out / (key + '.ini')
                ini.write_text('startup 1\noverlaystyle %d\noverlaypalette%d %d\nbackend %s\nvsync 0\nfps 60\nwindow %s\n' %
                               (style, style, args.palette, args.backend, size))
                ppm = out / (key + '.ppm')
                env = os.environ.copy()
                env['MELEE_UI_ICON_VARIANT'] = args.icon_variant
                env.pop('MELEE_SETTINGS_TAB', None)
                # The older focused-smoke-test hook wins before the gallery hook; do not
                # let an inherited test-shell value silently override the requested page.
                env.pop('MELEE_TEST_SETTINGS_TAB', None)
                if page != 'home':
                    env['MELEE_SETTINGS_TAB'] = page
                cmd = [str(args.exe), '--iso', str(args.iso), '--hidden', '--load-settings', '--pc-settings-open',
                       '--backend', args.backend, '--frames', '360', '--capture-frame', '0', '--capture-sim-frame', '200',
                       '--capture', str(ppm), '--window', size, '--volume', '0', '--no-music',
                       '--settings-path', str(ini), '--log-file', str(out / (key + '.log'))]
                try:
                    with (out / (key + '.stdout.txt')).open('w') as log:
                        result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=log,
                                                stderr=subprocess.STDOUT, timeout=90,
                                                creationflags=subprocess.CREATE_NO_WINDOW)
                    code = result.returncode
                except subprocess.TimeoutExpired:
                    code = 'timeout'
                exists = ppm.is_file()
                if exists:
                    with Image.open(ppm) as img:
                        img.save(ppm.with_suffix('.png'))
                records = [r for r in records if (r['style'],r['page'],r['size'],r['backend'],
                           r.get('palette',0),r.get('icon_variant','B')) !=
                           (style,page,size,args.backend,args.palette,args.icon_variant)]
                records.append(dict(style=style, name=NAMES[style], page=page, size=size,
                                    backend=args.backend, palette=args.palette,
                                    icon_variant=args.icon_variant, exit=code,
                                    image=key + '.png' if exists else None))
                print(key, 'exit=', code, 'capture=', exists, flush=True)
                (out / 'captures.json').write_text(json.dumps(records, indent=2))
    for name in ('overlay-concepts-v2.png', 'overlay-concepts-41-60.png'):
        shutil.copy2(ROOT / 'port/runtime/gx/ui_sources/mockups/assets' / name, out / name)
    records.sort(key=lambda r:(r['style'],r.get('palette',0),r['size'],r['page']!='home',
                               r['page'],r['backend'],r.get('icon_variant','B')))
    cards = []
    for r in records:
        title = '%s / %s / %s / %s%s' % (r['name'], r['page'], r['size'], r['backend'],
            ' / palette %d' % r.get('palette',0) if r.get('palette',0) else '')
        if r['style'] in (1, 3):
            title += ' / icons ' + r.get('icon_variant','B')
        image = ('<a href="{0}"><img src="{0}" loading="lazy"></a>'.format(r['image'])
                 if r['image'] else '<p>Capture missing; inspect log.</p>')
        cards.append('<article><h2>%s</h2>%s</article>' % (html.escape(title), image))
    refs = '''<section class="references"><article><h2>Clean side reference</h2><div class="reference" style="background-image:url(overlay-concepts-v2.png);background-position:25% 0%"></div></article>
<article><h2>Icon dashboard reference</h2><div class="reference" style="background-image:url(overlay-concepts-v2.png);background-position:75% 66.6667%"></div></article>
<article><h2>Radial reference</h2><div class="reference" style="background-image:url(overlay-concepts-41-60.png);background-position:0% 66.6667%"></div></article>
<article><h2>Wide tabs reference</h2><div class="reference" style="background-image:url(overlay-concepts-41-60.png);background-position:25% 66.6667%"></div></article></section>'''
    (out / 'index.html').write_text('''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings rendering review</title>
<style>body{margin:32px;background:#10121a;color:#f1f3fa;font:16px system-ui}
header{max-width:1000px}a{color:#adc9ff}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(600px,1fr));gap:24px}
article{background:#191f2b;padding:20px;border-radius:12px}h2{font-size:18px}img{width:100%;height:auto}
.references{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px;margin:24px 0}.reference{aspect-ratio:1.4;background-size:500% 400%;background-repeat:no-repeat}
@media(max-width:680px){main{display:block}article{margin-bottom:20px}}</style>
<header><h1>Actual in-game settings captures</h1><p>Captured from the executable with isolated preferences.
Click any capture for its original size. These captures establish rendering only, not completed interaction testing.</p>
<p>Original sheets: <a href="overlay-concepts-v2.png">Selections 2 and 14</a> ·
<a href="overlay-concepts-41-60.png">Selections 11 and 12</a></p></header>''' + refs + '<main>' +
        '\n'.join(cards) + '</main>', encoding='utf-8')
    if any(r['exit'] != 0 or not r['image'] for r in records):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
