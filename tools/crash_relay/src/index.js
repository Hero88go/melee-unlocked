// Melee Unlocked crash report relay (Cloudflare Worker).
// The launcher POSTs a zip (crash text, minidump, logs) to /report after the player clicks Send.
// The Discord webhook lives only in the Worker secret DISCORD_WEBHOOK_URL; the client never has it.
// Limits: zip only, 8 MB, one report per IP per 10 minutes (KV binding RATE), 50 per day in total.

const MAX_BYTES = 8 * 1024 * 1024;
const PER_IP_SECONDS = 600;
const DAILY_LIMIT = 50;

function plain(value, max) {
  return String(value || "").replace(/[\u0000-\u001f\u007f`@]/g, " ").slice(0, max);
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
    const day = new Date().toISOString().slice(0, 10);
    if (env.RATE) {
      if (await env.RATE.get("ip:" + ip)) return new Response("slow down", { status: 429 });
      const count = Number((await env.RATE.get("day:" + day)) || "0");
      if (count >= DAILY_LIMIT) return new Response("daily limit", { status: 429 });
      await env.RATE.put("ip:" + ip, "1", { expirationTtl: PER_IP_SECONDS });
      await env.RATE.put("day:" + day, String(count + 1), { expirationTtl: 172800 });
    }

    const body = await request.arrayBuffer();
    if (body.byteLength > MAX_BYTES) return new Response("too large", { status: 413 });
    const head = new Uint8Array(body.slice(0, 4));
    if (!(head[0] === 0x50 && head[1] === 0x4b && head[2] === 0x03 && head[3] === 0x04))
      return new Response("not a zip", { status: 415 });

    const version = plain(request.headers.get("x-mu-version"), 20);
    const engine = plain(request.headers.get("x-mu-engine"), 20);
    const where = plain(request.headers.get("x-mu-crash"), 300);
    const form = new FormData();
    form.append("payload_json", JSON.stringify({
      content: `Crash report: ${version || "?"} (${engine || "?"})\n${where}`,
      allowed_mentions: { parse: [] },
    }));
    form.append("files[0]", new Blob([body], { type: "application/zip" }), `crash-${Date.now()}.zip`);
    const sent = await fetch(env.DISCORD_WEBHOOK_URL, { method: "POST", body: form });
    if (!sent.ok) return new Response("relay failed", { status: 502 });
    return new Response("sent", { status: 200 });
  },
};
