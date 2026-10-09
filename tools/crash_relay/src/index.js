// Melee Unlocked crash report relay (Cloudflare Worker).
// The launcher POSTs a ZIP after the player clicks Send. Older clients may include raw dumps/logs;
// this relay forwards only a newly built ZIP and Markdown containing scrubbed log text (privacy.js).
// Reports are stored in KV (REPORTS) and pulled with tools/pull_reports.py through /admin/*, which
// needs the Worker secret ADMIN_TOKEN. Each report is also posted to the private Discord channel when FORWARD_DISCORD is "1".
// Limits: zip only, 8 MB, one report per IP per 2 minutes, 20 per day in total (Durable Object QUOTA).
import { InvalidZip, gameVersionLabel, sanitizedReport } from "./crash_markdown.js";

const MAX_BYTES = 8 * 1024 * 1024;
const PER_IP_SECONDS = 120;
const DAILY_LIMIT = 20;

// The limits' counters. A Durable Object handles one request at a time while it waits on its own
// storage, so a check and the reservation after it cannot interleave with another report.
export async function reserve(storage, ip, now) {
  const day = new Date(now).toISOString().slice(0, 10);
  const until = (await storage.get("ip:" + ip)) || 0;
  if (until > now) return { ok: false, reason: "slow down" };
  const count = (await storage.get("day:" + day)) || 0;
  if (count >= DAILY_LIMIT) return { ok: false, reason: "daily limit" };
  const stale = [];
  for (const [key, value] of await storage.list()) {
    if ((key.startsWith("ip:") && value <= now) || (key.startsWith("day:") && key !== "day:" + day)) stale.push(key);
  }
  if (stale.length) await storage.delete(stale);
  await storage.put({ ["ip:" + ip]: now + PER_IP_SECONDS * 1000, ["day:" + day]: count + 1 });
  return { ok: true };
}

export class QuotaGate {
  constructor(state) { this.storage = state.storage; }
  async fetch(request) {
    const { ip, now } = await request.json();
    return Response.json(await reserve(this.storage, String(ip), Number(now)));
  }
}

const KEEP_SECONDS = 90 * 24 * 3600;   // reports expire after 90 days

function authorized(request, env) {
  const want = env.ADMIN_TOKEN || "";
  const got = (request.headers.get("authorization") || "").replace(/^Bearer\s+/i, "");
  if (!want || got.length !== want.length) return false;
  let diff = 0;
  for (let i = 0; i < want.length; ++i) diff |= want.charCodeAt(i) ^ got.charCodeAt(i);
  return diff === 0;
}

async function admin(request, env, url) {
  if (!authorized(request, env)) return new Response("unauthorized", { status: 401 });
  if (url.pathname === "/admin/list") {
    const after = url.searchParams.get("after") || "";
    const items = [];
    let cursor;
    do {
      const page = await env.REPORTS.list({ prefix: "r:", cursor });
      for (const k of page.keys) if (!k.name.endsWith("/trace") && k.name > after) items.push({ key: k.name, meta: k.metadata || {} });
      cursor = page.list_complete ? undefined : page.cursor;
    } while (cursor && items.length < 500);
    items.sort((a, b) => a.key < b.key ? -1 : 1);
    return Response.json({ items: items.slice(0, 200) });
  }
  const key = url.searchParams.get("key") || "";
  if (!key.startsWith("r:")) return new Response("bad key", { status: 400 });
  if (url.pathname === "/admin/get") {
    const md = await env.REPORTS.get(key);
    return md === null ? new Response("missing", { status: 404 }) : new Response(md, { headers: { "content-type": "text/markdown; charset=utf-8" } });
  }
  if (url.pathname === "/admin/trace") {
    const tr = await env.REPORTS.get(key + "/trace", "arrayBuffer");
    return tr === null ? new Response("missing", { status: 404 }) : new Response(tr, { headers: { "content-type": "text/plain" } });
  }
  if (url.pathname === "/admin/delete" && request.method === "POST") {
    await env.REPORTS.delete(key); await env.REPORTS.delete(key + "/trace");
    return new Response("deleted");
  }
  return new Response("not found", { status: 404 });
}

function decodeNote(value) {
  try { return decodeURIComponent(value || "").slice(0, 2000); } catch { return ""; }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname.startsWith("/admin/")) return admin(request, env, url);
    if (url.pathname !== "/report") return new Response("not found", { status: 404 });
    if (request.method !== "POST") return new Response("POST only", { status: 405 });
    const type = request.headers.get("content-type") || "";
    if (!type.startsWith("application/zip")) return new Response("zip only", { status: 415 });
    const length = Number(request.headers.get("content-length") || "0");
    if (!length || length > MAX_BYTES) return new Response("too large", { status: 413 });
    if (!env.REPORTS) return new Response("relay not configured", { status: 503 });

    const ip = request.headers.get("cf-connecting-ip") || "unknown";
    // One Durable Object holds the counters: it checks and reserves in one step, so reports sent at
    // the same moment cannot all pass the limits. No quota binding is a configuration error.
    if (!env.QUOTA) return new Response("relay not configured", { status: 503 });
    const gate = env.QUOTA.get(env.QUOTA.idFromName("quota"));
    const verdict = await (await gate.fetch("https://quota/reserve", {
      method: "POST", body: JSON.stringify({ ip, now: Date.now() }),
    })).json();
    if (!verdict.ok) return new Response(verdict.reason, { status: 429 });

    const body = await request.arrayBuffer();
    if (body.byteLength > MAX_BYTES) return new Response("too large", { status: 413 });
    const head = new Uint8Array(body.slice(0, 4));
    if (!(head[0] === 0x50 && head[1] === 0x4b && head[2] === 0x03 && head[3] === 0x04))
      return new Response("not a zip", { status: 415 });

    const kind = request.headers.get("x-mu-kind") === "logs" ? "logs" : "crash";
    let report;
    try {
      report = sanitizedReport(body, request.headers.get("x-mu-version"),
        request.headers.get("x-mu-engine"), request.headers.get("x-mu-crash") || "",
        decodeNote(request.headers.get("x-mu-note")), kind);
    } catch (error) {
      if (error instanceof InvalidZip) return new Response("unsupported diagnostic zip", { status: 415 });
      throw error;
    }
    const now = new Date();
    const id = now.toISOString().replace(/[:.]/g, "-") + "-" + crypto.randomUUID().slice(0, 8);
    const key = "r:" + id;
    const meta = { kind, version: report.version, engine: report.engine, where: report.where.slice(0, 200),
                   note: report.note.slice(0, 300), trace: !!report.trace };
    await env.REPORTS.put(key, report.markdown, { metadata: meta, expirationTtl: KEEP_SECONDS });
    if (report.trace) {
      // The trace stays a plain CSV for tools/net_trace readers: drop the privacy header line.
      const text = new TextDecoder().decode(report.trace).replace(/^\[Privacy:[^\n]*\n/, "");
      await env.REPORTS.put(key + "/trace", text, { expirationTtl: KEEP_SECONDS });
    }

    if (env.FORWARD_DISCORD === "1" && env.DISCORD_WEBHOOK_URL) {
      const form = new FormData();
      form.append("payload_json", JSON.stringify({
        // The header's version is the launcher's; the game that crashed names its own at the end of
        // the crash line, and a player can run an older game from the launcher's Versions list.
        content: `${kind === "logs" ? "Logs" : "Crash report"}: ${gameVersionLabel(report.where, report.version)} (${report.engine})\n${report.where}`,
        allowed_mentions: { parse: [] },
      }));
      const reportId = now.getTime();
      form.append("files[0]", new Blob([report.zip], { type: "application/zip" }), `crash-${reportId}.zip`);
      form.append("files[1]", new Blob([report.markdown], { type: "text/markdown; charset=utf-8" }), `crash-${reportId}.md`);
      // The report is already stored, so a Discord outage does not fail the player's send.
      try { await fetch(env.DISCORD_WEBHOOK_URL, { method: "POST", body: form }); } catch {}
    }
    return new Response("sent " + id, { status: 200 });
  },
};
