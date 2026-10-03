// Melee Unlocked crash report relay (Cloudflare Worker).
// The launcher POSTs a ZIP after the player clicks Send. Older clients may include raw dumps/logs;
// this relay forwards only a newly built ZIP and Markdown containing scrubbed log text (privacy.js).
// The Discord webhook lives only in the Worker secret DISCORD_WEBHOOK_URL; the client never has it.
// Limits: zip only, 8 MB, one report per IP per 10 minutes, 50 per day in total (Durable Object QUOTA).
import { InvalidZip, sanitizedReport } from "./crash_markdown.js";

const MAX_BYTES = 8 * 1024 * 1024;
const PER_IP_SECONDS = 600;
const DAILY_LIMIT = 50;

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

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname !== "/report") return new Response("not found", { status: 404 });
    if (request.method !== "POST") return new Response("POST only", { status: 405 });
    const type = request.headers.get("content-type") || "";
    if (!type.startsWith("application/zip")) return new Response("zip only", { status: 415 });
    const length = Number(request.headers.get("content-length") || "0");
    if (!length || length > MAX_BYTES) return new Response("too large", { status: 413 });

    const ip = request.headers.get("cf-connecting-ip") || "unknown";
    // One Durable Object holds the counters: it checks and reserves in one step, so reports sent at
    // the same moment cannot all pass the limits (KV reads and writes are not atomic). No quota
    // binding at all is a configuration error, and the relay refuses rather than running unlimited.
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

    let report;
    try {
      report = sanitizedReport(body, request.headers.get("x-mu-version"),
        request.headers.get("x-mu-engine"), request.headers.get("x-mu-crash") || "");
    } catch (error) {
      if (error instanceof InvalidZip) return new Response("unsupported diagnostic zip", { status: 415 });
      throw error;
    }
    const form = new FormData();
    form.append("payload_json", JSON.stringify({
      content: `Crash report: ${report.version} (${report.engine})\n${report.where}`,
      allowed_mentions: { parse: [] },
    }));
    const reportId = Date.now();
    form.append("files[0]", new Blob([report.zip], { type: "application/zip" }), `crash-${reportId}.zip`);
    form.append("files[1]", new Blob([report.markdown],
      { type: "text/markdown; charset=utf-8" }), `crash-${reportId}.md`);
    const sent = await fetch(env.DISCORD_WEBHOOK_URL, { method: "POST", body: form });
    if (!sent.ok) return new Response("relay failed", { status: 502 });
    return new Response("sent", { status: 200 });
  },
};
