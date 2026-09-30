"""Source Port finish tracker: one final percent from measured parts, plus live work steps.

Serve:   python tools/finish_tracker.py serve            (http://127.0.0.1:8767)
Update:  python tools/finish_tracker.py step "text" [--state current|done|blocked]
         python tools/finish_tracker.py track KEY DONE TOTAL ["note"]
         python tools/finish_tracker.py log "what happened"
Replay counts are read live from the newest run-source/replay*/summary.txt.
"""
import glob, html, json, os, re, sys, time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'run-source', 'finish-tracker.json')
PORT = 8767

DEFAULT = {
    'tracks': {
        'gates': {'title': 'Acceptance gates (M0-M11)', 'done': 61, 'total': 64,
                  'note': 'left: M6 visible + M8 feel (Chandler sign-off), then M11 audit'},
        'replays': {'title': 'Replays bit-exact vs Dolphin', 'done': 114, 'total': 200,
                    'note': 'every frame of every player equal to the Dolphin recording'},
        'drift': {'title': 'Float drift causes fixed', 'done': 0, 'total': 5,
                  'note': 'landing snap, other landing y, shield health, x, late splits'},
    },
    'steps': [], 'log': [],
}


def now():
    return datetime.now(timezone.utc).isoformat(timespec='seconds')


def load():
    try:
        with open(DATA, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return json.loads(json.dumps(DEFAULT))


def save(d):
    d['updated'] = now()
    tmp = DATA + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(d, f, indent=1)
    os.replace(tmp, DATA)


def live_replays(d):
    """Per replay, the newest result.txt from any run-source/replay*/ dir wins, so the count moves
    the moment each replay finishes instead of when a whole batch does."""
    try:                                            # the 200 vanilla replays of the corpus
        with open(os.path.join(ROOT, 'run-source', 'replay200-20260925', 'summary.json'), encoding='utf-8') as f:
            corpus = {os.path.splitext(os.path.basename(r['replay']))[0] for r in json.load(f)}
    except (OSError, ValueError):
        corpus = set()
    newest = {}
    for res in glob.glob(os.path.join(ROOT, 'run-source', 'replay*', '*', 'result.txt')):
        name = os.path.basename(os.path.dirname(res))
        if name not in corpus:
            continue
        t = os.path.getmtime(res)
        if name not in newest or t > newest[name][0]:
            newest[name] = (t, res)
    if len(newest) >= 150:
        exact = 0
        for _, res in newest.values():
            with open(res, encoding='utf-8', errors='replace') as f:
                if 'mismatches from frame 0 on: 0' in f.read():
                    exact += 1
        t = d['tracks']['replays']
        t['done'], t['total'] = exact, len(newest)
        t['source'] = 'newest result per replay, live'
        return
    runs = sorted(glob.glob(os.path.join(ROOT, 'run-source', 'replay*', 'summary.txt')), key=os.path.getmtime)
    for run in reversed(runs):                      # newest full-corpus summary only
        with open(run, encoding='utf-8') as f:
            m = re.match(r'(\d+) replays.*?(\d+) exact from frame 0', f.readline())
        if m and int(m.group(1)) >= 200:
            t = d['tracks']['replays']
            t['total'], t['done'] = int(m.group(1)), int(m.group(2))
            t['source'] = os.path.relpath(run, ROOT)
            return


def snapshot():
    d = load()
    live_replays(d)
    tr = d['tracks'].values()
    d['percent'] = round(100 * sum(t['done'] / max(1, t['total']) for t in tr) / len(d['tracks']), 2)
    return d


PAGE = r'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Source Port Finish</title><style>
:root{--bg:#0b1020;--panel:#141d30;--line:#29364c;--text:#edf3ff;--muted:#a5b1c7;--green:#54d49b;--blue:#83adff;--amber:#f3bd61;--red:#ff8088}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 system-ui,"Segoe UI",sans-serif}
main{max-width:980px;margin:28px auto;padding:0 16px 40px}
.pct{font-size:clamp(64px,14vw,110px);font-weight:850;line-height:1;background:linear-gradient(90deg,#7fb0ff,#5ce0a8);-webkit-background-clip:text;background-clip:text;color:transparent;font-variant-numeric:tabular-nums}
.bar{height:18px;border-radius:99px;background:#1c2740;overflow:hidden;margin:12px 0}.bar i{display:block;height:100%;background:linear-gradient(90deg,#3f74ff,#4fe0a0);transition:width 1s}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:16px;margin-top:14px}
.track{padding:10px 0;border-top:1px solid var(--line)}.track:first-of-type{border-top:0}
.row{display:flex;justify-content:space-between;gap:10px}.muted{color:var(--muted);font-size:13px}
.small{height:8px;margin:6px 0}ul{list-style:none;padding:0;margin:0}li{padding:6px 0;border-top:1px solid #1f2a40;font-size:14px}
.st{display:inline-block;min-width:70px;font-size:11px;font-weight:800;text-transform:uppercase}
.st.done{color:var(--green)}.st.current{color:var(--blue)}.st.blocked{color:var(--red)}
.beat{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--green);margin-right:6px}.beat.stale{background:var(--amber)}
</style></head><body><main>
<div class="pct"><span id="pct">--</span>%</div><div class="bar"><i id="bar" style="width:0"></i></div>
<div class="muted"><span id="beat" class="beat"></span><span id="upd"></span>. Final percent = average of the three parts below. The old 95% was the gates row alone; the bit-exact replay work was never counted before.</div>
<div class="panel" id="tracks"></div>
<div class="panel"><b>Work steps right now</b><ul id="steps"></ul></div>
<div class="panel"><b>Log</b><ul id="log"></ul></div>
<script>
const $=id=>document.getElementById(id),esc=s=>String(s??'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
function ago(t){let m=Math.round((Date.now()-Date.parse(t))/60000);return m<1?'just now':m<120?m+' min ago':Math.round(m/60)+' h ago'}
async function tick(){try{let d=await (await fetch('/api')).json();
$('pct').textContent=d.percent.toFixed(2);$('bar').style.width=d.percent+'%';
$('upd').textContent='Last update '+ago(d.updated);$('beat').className='beat'+((Date.now()-Date.parse(d.updated))>3600e3?' stale':'');
$('tracks').innerHTML=Object.values(d.tracks).map(t=>{let p=100*t.done/Math.max(1,t.total);return `<div class="track"><div class="row"><b>${esc(t.title)}</b><b>${t.done} / ${t.total} (${p.toFixed(1)}%)</b></div><div class="bar small"><i style="width:${p}%"></i></div><div class="muted">${esc(t.note)}</div></div>`}).join('');
$('steps').innerHTML=d.steps.map(s=>`<li><span class="st ${s.state}">${s.state}</span>${esc(s.text)} <span class="muted">${ago(s.time)}</span></li>`).join('');
$('log').innerHTML=d.log.slice().reverse().slice(0,40).map(l=>`<li>${esc(l.text)} <span class="muted">${ago(l.time)}</span></li>`).join('');
}catch(e){$('upd').textContent='tracker server not reachable'}}
tick();setInterval(tick,5000);
</script></main></body></html>'''


class H(BaseHTTPRequestHandler):
    def do_GET(self):
        body, ctype = (json.dumps(snapshot()), 'application/json') if self.path.startswith('/api') else (PAGE, 'text/html')
        b = body.encode('utf-8')
        self.send_response(200)
        self.send_header('Content-Type', ctype + '; charset=utf-8')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def log_message(self, *a):
        pass


def main(a):
    if not a or a[0] == 'serve':
        ThreadingHTTPServer(('127.0.0.1', PORT), H).serve_forever()
    d = load()
    if a[0] == 'step':
        state = a[a.index('--state') + 1] if '--state' in a else 'current'
        text = a[1]
        if state == 'current':
            for s in d['steps']:
                if s['state'] == 'current':
                    s['state'] = 'done'
        hit = [s for s in d['steps'] if s['text'] == text]
        if hit:
            hit[0].update(state=state, time=now())
        else:
            d['steps'].append({'text': text, 'state': state, 'time': now()})
        d['log'].append({'text': ('Done: ' if state == 'done' else 'Now: ' if state == 'current' else 'Blocked: ') + text, 'time': now()})
    elif a[0] == 'track':
        t = d['tracks'][a[1]]
        t['done'], t['total'] = int(a[2]), int(a[3])
        if len(a) > 4:
            t['note'] = a[4]
        d['log'].append({'text': '%s: %s/%s' % (t['title'], a[2], a[3]), 'time': now()})
    elif a[0] == 'log':
        d['log'].append({'text': a[1], 'time': now()})
    d['log'] = d['log'][-200:]
    save(d)


if __name__ == '__main__':
    main(sys.argv[1:])
