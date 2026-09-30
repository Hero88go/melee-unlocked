#!/usr/bin/env python3
"""Local progress dashboard. Polling is localhost-only and uses no model/API."""
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import json
import re

ROOT = Path(__file__).resolve().parents[1]
STATUS = ROOT / "MILESTONE_STATUS.md"
ACTIVITY = ROOT / "run-source" / "live-progress.json"
AGENTS = ROOT / "run-source" / "agent-progress.json"   # tools/agent_progress.py
DOCS = {"/milestones.md": STATUS,
        "/handoff.md": ROOT / "HANDOFF_CODEX.md",
        "/plan.md": ROOT / "PLAN_LUNA_JEV.md",
        "/about.md": ROOT / "PROGRESS_SITE.md"}

PAGE = r'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark"><title>Melee Source Port Progress</title>
<style>
.agents-panel{margin:0 0 18px;padding:16px 18px;border-radius:16px;border:1px solid #2c3d5e;background:linear-gradient(145deg,#152038,#0e1526)}.agents-head{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:10px}.agent{padding:9px 0;border-top:1px solid #22304a}.agent:first-of-type{border-top:0}.agent-top{display:flex;justify-content:space-between;gap:10px;align-items:baseline}.agent-name{font-weight:600}.agent-pct{font-variant-numeric:tabular-nums;font-weight:700}.agent-bar{position:relative;height:10px;border-radius:99px;background:#22304a;overflow:hidden;margin:6px 0 4px}.agent-bar i{position:absolute;inset:0 auto 0 0;border-radius:99px;background:linear-gradient(90deg,#558dff,#55d49c);transition:width .6s ease}.agent.running .agent-bar i{background-image:linear-gradient(90deg,#558dff,#55d49c),repeating-linear-gradient(45deg,#ffffff22 0 8px,transparent 8px 16px);background-blend-mode:overlay;animation:stripes 1s linear infinite;background-size:auto,32px 32px}@keyframes stripes{to{background-position:0 0,32px 0}}.agent.done .agent-bar i{background:#54d49b}.agent.blocked .agent-bar i{background:#f3bd61}.agent.stopped .agent-bar i{background:#7b879c}.agent-note{font-size:13px;color:var(--muted)}.agent-state{font-size:12px;padding:1px 8px;border-radius:99px;border:1px solid #34476a;margin-left:8px;color:var(--muted)}:root{color-scheme:dark;--bg:#0b1020;--panel:#141d30;--line:#29364c;--text:#edf3ff;--muted:#a5b1c7;--blue:#83adff;--green:#54d49b;--amber:#f3bd61;--red:#ff8088}*{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at 15% 0%,#1a2c4c,transparent 42%),var(--bg);color:var(--text);font:15px/1.5 system-ui,"Segoe UI",sans-serif}main{max-width:1160px;margin:34px auto;padding:0 18px 48px}header{display:flex;justify-content:space-between;gap:20px;align-items:start;margin-bottom:22px}h1{font-size:clamp(27px,4vw,42px);line-height:1.1;letter-spacing:-.03em;margin:0 0 8px}h2{font-size:18px;margin:0 0 14px}.muted,.sub{color:var(--muted)}.live{color:var(--green);padding:6px 12px;border:1px solid #28634e;background:#123126;border-radius:99px;white-space:nowrap;font-weight:700}.top{display:grid;grid-template-columns:minmax(230px,.8fr) 2fr;gap:14px}.panel,.card{background:linear-gradient(145deg,#172136,#101827);border:1px solid var(--line);border-radius:15px;padding:19px;box-shadow:0 12px 32px #0002}.big{font-size:56px;font-weight:800;line-height:1;margin:8px 0}.bar{height:9px;border-radius:99px;background:#29364c;overflow:hidden;margin:13px 0 8px}.bar i{display:block;height:100%;background:linear-gradient(90deg,#558dff,#55d49c);border-radius:99px}.summary{display:grid;grid-template-columns:repeat(4,1fr);gap:9px}.summary div{padding:12px;background:#0e1523;border:1px solid var(--line);border-radius:11px}.summary b{display:block;font-size:20px}.summary span{font-size:12px;color:var(--muted)}.section{margin-top:24px}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:11px}.card{padding:15px;min-height:142px}.cardtop{display:flex;align-items:start;justify-content:space-between;gap:9px;margin-bottom:9px}.card h3{font-size:14px;margin:0}.chip{font-size:10px;font-weight:750;border:1px solid var(--line);border-radius:99px;padding:3px 8px;color:var(--amber);background:#352914;white-space:nowrap}.chip.critical{color:var(--red);background:#351b24;border-color:#713b42}.chip.done{color:var(--green);background:#123126;border-color:#28634e}.chip.defer{color:#c1b2ff;background:#241d3b;border-color:#51447c}.detail{font-size:12px;color:#cad4e6;margin:5px 0}.activity{display:grid;grid-template-columns:minmax(260px,1fr) minmax(300px,1fr);gap:18px}.activity-current{font-size:20px;font-weight:700;margin:8px 0}.activity-feed{list-style:none;padding:0;max-height:210px;overflow:auto}.activity-feed li{border-left:2px solid #406397;padding:2px 0 8px 12px;margin:0 0 7px}.activity-feed strong{display:block;color:var(--text);font-size:13px}.activity-feed small{color:var(--muted)}.two{display:grid;grid-template-columns:1fr 1fr;gap:14px}ul{padding-left:18px;margin:0;color:#c8d2e5;font-size:13px}li+li{margin-top:8px}.links{display:flex;gap:16px;flex-wrap:wrap}a{color:var(--blue);text-decoration:none}a:hover{text-decoration:underline}footer{margin-top:23px;border-top:1px solid var(--line);padding-top:14px;font-size:12px;color:var(--muted)}@media(max-width:800px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.top{grid-template-columns:1fr}.summary{grid-template-columns:repeat(2,1fr)}.activity{grid-template-columns:1fr}}@media(max-width:540px){header{display:block}.live{display:inline-block;margin-top:12px}.grid,.two,.summary,.activity{grid-template-columns:1fr}main{margin-top:20px}}
.hero{position:relative;margin:4px 0 18px;padding:26px 26px 20px;border-radius:20px;border:1px solid #2c3d5e;background:radial-gradient(ellipse at 90% -20%,#2b3f7a55,transparent 60%),linear-gradient(145deg,#172239,#0d1424);box-shadow:0 20px 50px #0006,inset 0 1px 0 #ffffff10}.hero-row{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;flex-wrap:wrap}.pct{font-size:clamp(64px,12vw,112px);font-weight:850;line-height:.9;letter-spacing:-.05em;background:linear-gradient(90deg,#7fb0ff,#5ce0a8 60%,#b6ffda);-webkit-background-clip:text;background-clip:text;color:transparent;font-variant-numeric:tabular-nums}.pct em{font-style:normal;font-size:.42em;margin-left:4px}.hero-side{text-align:right}.big-count{font-size:30px;font-weight:800;font-variant-numeric:tabular-nums}.small{font-size:12px}.hbar{position:relative;display:flex;height:22px;margin:20px 0 12px;border-radius:99px;background:#0a101c;border:1px solid #2b3a57;overflow:hidden;box-shadow:inset 0 2px 6px #0008}.seg{display:block;height:100%;transition:width 1.2s cubic-bezier(.2,.8,.2,1)}.seg.pass{position:relative;background:linear-gradient(90deg,#3f74ff,#46c9ff 45%,#4fe0a0);box-shadow:0 0 18px #4fe0a088;overflow:hidden}.seg.pass:after{content:"";position:absolute;inset:0;background:linear-gradient(110deg,transparent 30%,#ffffff55 50%,transparent 70%);background-size:220% 100%;animation:shine 2.6s linear infinite}.seg.fail{background:repeating-linear-gradient(135deg,#ff5f6d,#ff5f6d 6px,#c33f4c 6px,#c33f4c 12px);box-shadow:0 0 12px #ff5f6d77}.ticks{position:absolute;inset:0;pointer-events:none;background:repeating-linear-gradient(90deg,transparent 0 calc(10% - 1px),#ffffff22 calc(10% - 1px) 10%)}@keyframes shine{from{background-position:120% 0}to{background-position:-120% 0}}@media(prefers-reduced-motion:reduce){.seg.pass:after{animation:none}.seg{transition:none}}.legend{display:flex;gap:16px;flex-wrap:wrap;font-size:13px;color:var(--muted)}.legend span b{color:var(--text);margin-right:4px}.legend i{display:inline-block;width:9px;height:9px;border-radius:3px;margin-right:6px}.now{margin:14px 0 0;font-size:13px;color:#cad4e6;display:flex;gap:6px;align-items:center}.pulse{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 0 0 #54d49b88;animation:pulse 1.8s infinite;flex:none}.pulse.stale{background:var(--amber);animation:none}@keyframes pulse{70%{box-shadow:0 0 0 8px #54d49b00}}details.panel{padding:0}details.panel>summary{cursor:pointer;list-style:none;padding:14px 19px;font-weight:700;display:flex;gap:10px;align-items:center}details.panel>summary::-webkit-details-marker{display:none}details.panel>summary:before{content:"▸";color:var(--muted);transition:transform .2s}details[open].panel>summary:before{transform:rotate(90deg)}details.panel>*:not(summary){margin:0 19px 16px}details.section{margin-top:10px}header h1{font-size:22px;margin:0}header{align-items:center}</style></head><body><main>
<header><h1>Melee Source Port</h1><div class="live" id="connection">● Connecting locally</div></header>
<section class="hero"><div class="hero-row"><div class="pct"><span id="pct">--.--</span><em>%</em></div><div class="hero-side"><div class="big-count" id="estimate">—</div><div class="muted small">acceptance checkpoints verified</div></div></div>
<div class="hbar" id="hbar"><i class="seg pass" id="bar" style="width:0"></i><i class="seg fail" id="bar-fail" style="width:0"></i><span class="ticks"></span></div>
<div class="legend" id="summary"></div><p class="now"><span class="pulse" id="activity-pulse"></span><b>Now:</b> <span id="activity-current">Loading active work…</span></p></section>
<section class="agents-panel" id="agents-panel" hidden><div class="agents-head"><b>Agents</b><span class="muted small" id="agents-sum"></span></div><div id="agents"></div></section>
<details class="section panel"><summary>Working on now <span class="chip" id="activity-phase">Starting</span></summary><div class="activity"><div><p class="muted" id="activity-elapsed"></p><p class="detail" id="activity-detail"></p><ul class="work-items" id="work-items"></ul><div class="activity-meta"><span id="activity-updated">Loading evidence feed…</span> · <span id="page-refresh">Waiting for local refresh…</span></div></div><div><div class="feed-head"><b>Recent results</b> <span class="feed-count muted" id="event-count"></span></div><ul class="activity-feed" id="activity-feed"></ul></div></div></details>
<details class="section panel"><summary>Milestones</summary><div class="controls"><label for="status-filter" class="muted">Filter</label><select id="status-filter"><option value="all">All statuses</option><option value="PASS">Passed</option><option value="IN PROGRESS">In progress</option><option value="FAILED">Failed</option><option value="OPEN">Open</option></select><span class="muted" id="focus"></span></div><div class="grid" id="milestones"></div></details>
<details class="section panel"><summary>Open work</summary><ul id="blockers"></ul></details>
<details class="section panel"><summary>Notes</summary><ul id="notes"></ul><p class="muted small" id="meter-label"></p><p class="muted small" id="updated"></p></details>
<details class="section panel"><summary>Project documents</summary><div class="links"><a href="/milestones.md" target="_blank">Milestone status</a><a href="/handoff.md" target="_blank">Engineering handoff</a><a href="/plan.md" target="_blank">Acceptance plan</a><a href="/about.md" target="_blank">Dashboard details</a></div></details>
<footer>Refreshes every second from this computer. On the same home network, open this PC's LAN address and port. Progress entries update when local evidence is recorded. No external requests, Codex usage, Jev usage, analytics, or third-party scripts.</footer>
</main><script>
const $=id=>document.getElementById(id),el=(tag,cls,text)=>{let x=document.createElement(tag);if(cls)x.className=cls;if(text!==undefined)x.textContent=text;return x};
function chip(s){let c='chip';if(s.toLowerCase().includes('critical')||s.toLowerCase().includes('failed'))c+=' critical';else if(s.toLowerCase().includes('done')||s.toLowerCase().includes('green'))c+=' done';else if(s.toLowerCase().includes('out of scope'))c+=' defer';return c}
function fillList(id,items){let x=$(id);x.replaceChildren();for(let t of items)x.append(el('li','',t))}
let latest=null;
function renderMilestones(d){let grid=$('milestones'),filter=$('status-filter').value;grid.replaceChildren();for(let m of d.milestones){let rows=m.checkpoints.items.filter(x=>filter==='all'||x.state===filter);if(filter!=='all'&&!rows.length)continue;let c=el('article','card'),top=el('div','cardtop');top.append(el('h3','',m.id+' - '+m.title),el('span',chip(m.status),m.status));c.append(top,el('p','detail',m.done));let bar=el('div','mini-bar'),fill=el('i');fill.style.width=m.checkpoints.total?Math.round(100*m.checkpoints.pass/m.checkpoints.total)+'%':'0%';bar.append(fill);c.append(bar,el('p','detail muted',`${m.checkpoints.pass}/${m.checkpoints.total} checkpoints passed; ${m.checkpoints.partial} in progress; ${m.checkpoints.failed} failed`));let list=el('ul','items');for(let x of rows){let li=el('li'),badge=el('span','state '+x.state.toLowerCase().replace(' ','-'),x.state);li.append(badge,document.createTextNode(' '+x.name));list.append(li)}c.append(list,el('p','detail muted','Remaining: '+m.remaining));grid.append(c)}}
function drawActivity(a){$('activity-phase').textContent=a.phase||'Active';$('activity-current').textContent=a.current||'No active task recorded';$('activity-detail').textContent=a.detail||'';let f=$('activity-feed'),events=a.events||[];f.replaceChildren();$('event-count').textContent=events.length+' updates';for(let e of [...events].sort((x,y)=>Date.parse(y.time)-Date.parse(x.time))){let li=el('li'),time=new Date(e.time),row=[el('small','',time.toLocaleTimeString()+' · '+(e.state||'UPDATE')),el('strong','',e.title||''),el('span','detail',e.detail||'')];if(e.evidence)row.push(el('small','', 'Evidence: '+e.evidence));li.append(...row);f.append(li)}let tasks=$('work-items');tasks.replaceChildren();for(let t of (a.work_items||[])){let li=el('li'),state=String(t.state||'UP NEXT').toLowerCase().replace(' ','-');li.append(el('span','work-state '+state,t.state||'UP NEXT'),el('span','',t.title+(t.evidence?' · '+t.evidence:'')));tasks.append(li)}window.activityStarted=a.started?Date.parse(a.started):0;window.activityUpdated=a.updated?Date.parse(a.updated):0;window.pagePolled=Date.now();tickElapsed()}
function tickElapsed(){let start=window.activityStarted||0;if(!start){$('activity-elapsed').textContent='Elapsed time starts when this task is recorded.'}else{let seconds=Math.max(0,Math.floor((Date.now()-start)/1000)),mins=Math.floor(seconds/60),hrs=Math.floor(mins/60);$('activity-elapsed').textContent='Task running '+(hrs?hrs+'h ':'')+(mins%60)+'m '+(seconds%60)+'s · local browser timer'}let age=window.activityUpdated?Math.max(0,Math.floor((Date.now()-window.activityUpdated)/1000)):null,pulse=$('activity-pulse');if(age===null){$('activity-updated').textContent='No evidence update recorded yet';pulse.classList.add('stale')}else{$('activity-updated').textContent='Evidence feed updated '+age+'s ago · '+new Date(window.activityUpdated).toLocaleTimeString();pulse.classList.toggle('stale',age>30)}$('page-refresh').textContent='Page checking localhost every 1s · last response '+(window.pagePolled?Math.max(0,Math.floor((Date.now()-window.pagePolled)/1000))+'s ago':'pending')}
function drawAgents(list){let p=$('agents-panel'),box=$('agents');if(!list||!list.length){p.hidden=true;return}p.hidden=false;box.replaceChildren();let run=0,done=0;for(let a of list){let pct=Math.max(0,Math.min(100,100*(a.done||0)/Math.max(1,a.total||1)));if(a.status==='running')run++;if(a.status==='done')done++;let row=el('div','agent '+(a.status||'running')),top=el('div','agent-top'),left=el('div');left.append(el('span','agent-name',a.title||a.name),el('span','agent-state',a.status||'running'));top.append(left,el('span','agent-pct',pct.toFixed(1)+'%  ('+(a.done||0)+'/'+(a.total||0)+')'));let bar=el('div','agent-bar'),fill=el('i');fill.style.width=pct+'%';bar.append(fill);let age=a.updated?Math.round((Date.now()-Date.parse(a.updated))/60000):null;row.append(top,bar,el('div','agent-note',(a.note||'')+(age!=null?'  ·  updated '+(age<1?'just now':age+' min ago'):'')));box.append(row)}$('agents-sum').textContent=run+' running, '+done+' done'}
function draw(d){latest=d;drawAgents(d.agents);$('connection').textContent='Live - local only';$('connection').style.color='var(--green)';let c=d.checkpoints,t=c.total||1,px=d.percent_exact!=null?d.percent_exact:100*c.pass/t;$('pct').textContent=px.toFixed(2);$('estimate').textContent=c.pass+' / '+c.total;$('meter-label').textContent=px.toFixed(2)+'% of checkpoints verified; failed and open checks do not count as passed';$('bar').style.width=px+'%';$('bar-fail').style.width=(100*c.failed/t)+'%';$('updated').textContent='Evidence refreshed '+new Date(d.updated).toLocaleString();$('focus').textContent=d.focus||'';let s=$('summary'),col={Passed:'#4fe0a0','In progress':'#83adff',Failed:'#ff5f6d',Open:'#5a6784'};s.replaceChildren();for(let a of d.summary){let b=el('span'),dot=el('i');dot.style.background=col[a.label]||'#888';b.append(dot,el('b','',String(a.count)),document.createTextNode(a.label));s.append(b)}drawActivity(d.activity||{});renderMilestones(d);fillList('blockers',d.blockers);fillList('notes',d.notes)}
document.getElementById('status-filter').addEventListener('change',()=>{if(latest)renderMilestones(latest)})
async function refresh(){try{let r=await fetch('/api/progress',{cache:'no-store'});if(!r.ok)throw Error();draw(await r.json())}catch(_){$('connection').textContent='● Reconnecting to local server';$('connection').style.color='var(--amber)'}}refresh();setInterval(refresh,1000);setInterval(tickElapsed,1000);
</script></body></html>'''
PAGE = PAGE.replace('.two{', '.mini-bar{height:6px;border-radius:99px;background:#29364c;overflow:hidden;margin:10px 0 5px}.mini-bar i{display:block;height:100%;background:linear-gradient(90deg,#558dff,#55d49c);border-radius:99px}.controls{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin:0 0 12px}.controls select{background:#101827;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px 10px}.items{padding-left:18px;margin:8px 0 0;color:#c8d2e5;font-size:12px}.state{display:inline-block;min-width:78px;font-size:10px;font-weight:750;border-radius:6px;padding:2px 6px;color:#f3bd61;background:#352914}.state.pass{color:#54d49b;background:#123126}.state.failed{color:#ff8088;background:#351b24}.state.in-progress{color:#83adff;background:#182849}.two{')
PAGE = PAGE.replace('</style>', '.activity{border-color:#3b5c87;background:linear-gradient(135deg,#192744,#111a2b)}.work-head,.feed-head{display:flex;align-items:center;gap:9px}.pulse{width:10px;height:10px;flex:none;border-radius:50%;background:var(--green);box-shadow:0 0 0 0 #54d49b99;animation:pulse 1.8s infinite}.pulse.stale{background:var(--amber);animation:none;box-shadow:none}@keyframes pulse{70%{box-shadow:0 0 0 8px #54d49b00}}.work-items{list-style:none;padding:0;margin:13px 0 0;display:grid;gap:7px}.work-items li{display:flex;align-items:start;gap:8px;background:#0d1524a8;border:1px solid #29364c;border-radius:8px;padding:7px 9px;font-size:12px}.work-state{min-width:74px;color:var(--muted);font-size:10px;font-weight:800;text-transform:uppercase}.work-state.done{color:var(--green)}.work-state.current{color:var(--blue)}.work-state.blocked{color:var(--red)}.feed-count{color:var(--muted);font-size:11px}.activity-meta{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;font-size:11px;color:var(--muted);margin-top:10px}.activity-feed{max-height:340px}.activity-feed li{padding-bottom:12px;margin-bottom:9px}.activity-feed li small:last-child{display:block;color:#83adff;font-size:10px}</style>')

def load_agents() -> list:
    try:
        data = json.loads(AGENTS.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []
    return [dict(name=k, **v) for k, v in data.get("agents", {}).items()]


def progress(markdown: str) -> dict:
    updated = re.search(r"\*\*Live project status.*?([0-9-]+)\*\*", markdown)
    focus_match = re.search(r"\*\*Current focus:\*\*\s*(.+)", markdown)
    milestones = []
    for line in markdown.splitlines():
        if not re.match(r"^\|\s*M\d+\s*\|", line):
            continue
        c = [x.strip() for x in line.strip().strip("|").split("|")]
        if len(c) >= 5:
            milestones.append(dict(id=c[0], title=c[1], status=c[2].replace("*", "").replace("`", ""), done=c[3], remaining=c[4]))
    checkpoint_rows = []
    for line in markdown.splitlines():
        if not re.match(r"^\|\s*M\d+\s*\|", line):
            continue
        c = [x.strip() for x in line.strip().strip("|").split("|")]
        if len(c) == 3:
            # Some evidence-bearing checkpoint rows append a note after the
            # state (for example, "PASS — packaged README ..."). Count the
            # leading state token so the live dashboard matches the ledger.
            state_match = re.match(r"^(PASS|IN PROGRESS|FAILED|OPEN)\b", c[2].replace("*", "").replace("`", ""), re.I)
            if state_match:
                checkpoint_rows.append({"id": c[0], "name": c[1], "state": state_match.group(1).upper()})
    for m in milestones:
        rows = [r for r in checkpoint_rows if r["id"] == m["id"]]
        m["checkpoints"] = {"total": len(rows), "pass": sum(r["state"] == "PASS" for r in rows),
                            "partial": sum(r["state"] == "IN PROGRESS" for r in rows),
                            "failed": sum(r["state"] == "FAILED" for r in rows), "items": rows}
    totals = {state.lower().replace(" ", "_"): sum(r["state"] == state for r in checkpoint_rows)
              for state in ("PASS", "IN PROGRESS", "FAILED", "OPEN")}
    count = len(checkpoint_rows)
    passed = totals.get("pass", 0)
    blockers = [m["id"]+" "+m["title"]+": "+m["remaining"] for m in milestones
                if "open" in m["status"].lower() or "mostly done" in m["status"].lower()]
    notes = ["M7 remains a critical simulation parity gate; no frame-shift workaround is accepted."]
    if "No push, tag or release publication has been made." in markdown:
        notes.append("Work remains local; nothing has been published.")
    status_mtime = datetime.fromtimestamp(STATUS.stat().st_mtime, timezone.utc).isoformat(timespec="seconds")
    try:
        activity = json.loads(ACTIVITY.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        activity = {"phase": "Awaiting checkpoint", "current": "Activity feed is not initialized", "events": []}
    return dict(checkpoints={"total": count, "pass": passed, "partial": totals.get("in_progress", 0),
                            "failed": totals.get("failed", 0), "open": totals.get("open", 0)},
                # Python's round() uses ties-to-even, which displays 62% for
                # 25/40 (62.5%). Use conventional half-up display rounding.
                percent=int(100 * passed / count + 0.5) if count else 0,
                percent_exact=round(100 * passed / count, 2) if count else 0.0,
                updated=status_mtime, focus=focus_match.group(1).strip() if focus_match else None,
                milestones=milestones, blockers=blockers,
                summary=[dict(label="Passed", count=passed), dict(label="In progress", count=totals.get("in_progress", 0)),
                         dict(label="Failed", count=totals.get("failed", 0)), dict(label="Open", count=totals.get("open", 0))],
                notes=notes, activity=activity, agents=load_agents(),
                now=datetime.now(timezone.utc).isoformat())

class Handler(BaseHTTPRequestHandler):
    def send_body(self, body: bytes, kind: str, status=200):
        self.send_response(status); self.send_header("Content-Type",kind)
        self.send_header("Content-Length",str(len(body))); self.send_header("Cache-Control","no-store")
        self.send_header("X-Content-Type-Options","nosniff")
        self.send_header("Content-Security-Policy","default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; img-src 'none'; base-uri 'none'; frame-ancestors 'none'")
        self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        if self.path == "/": self.send_body(PAGE.encode(),"text/html; charset=utf-8"); return
        if self.path == "/api/progress":
            self.send_body(json.dumps(progress(STATUS.read_text(encoding="utf-8")),ensure_ascii=False).encode(),"application/json; charset=utf-8"); return
        p=DOCS.get(self.path)
        if p:
            try: self.send_body(p.read_bytes(),"text/markdown; charset=utf-8")
            except OSError: self.send_error(404)
            return
        self.send_error(404)
    def log_message(self, fmt, *args): print("[progress-site] "+fmt%args)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host",default="127.0.0.1",help="Interface address to serve on (default: localhost only)")
    ap.add_argument("--port",type=int,default=8765)
    a=ap.parse_args()
    server=ThreadingHTTPServer((a.host,a.port),Handler)
    print(f"Melee progress site at http://{a.host}:{a.port}/ (no external APIs)")
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: server.server_close()

if __name__ == "__main__": main()
