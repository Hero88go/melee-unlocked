// Local synthetic archives and a mock webhook only. No network requests or report uploads.
// SPDX-License-Identifier: GPL-2.0-or-later
import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import { fileURLToPath } from "node:url";
import { crashMarkdown, gameVersionLabel, logGameVersion, sanitizedReport, storedTextFiles, MAX_MARKDOWN_BYTES } from "../src/crash_markdown.js";
import { reportNames, scrubLine } from "../src/privacy.js";
import relay from "../src/index.js";

// A quota that always lets the report through (the limits have their own tests in quota.test.js).
const openQuota = { idFromName: () => "quota", get: () => ({ fetch: async () => Response.json({ ok: true }) }) };

function crc32(bytes) {
  let crc = 0xFFFFFFFF;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}
function zip(entries) {
  const local = [], central = [];
  let offset = 0;
  for (const [name, content] of entries) {
    const nameBytes = Buffer.from(name), data = Buffer.from(content), crc = crc32(data);
    const header = Buffer.alloc(30);
    header.writeUInt32LE(0x04034B50); header.writeUInt16LE(20, 4);
    header.writeUInt32LE(crc, 14); header.writeUInt32LE(data.length, 18);
    header.writeUInt32LE(data.length, 22); header.writeUInt16LE(nameBytes.length, 26);
    local.push(header, nameBytes, data);
    const directory = Buffer.alloc(46);
    directory.writeUInt32LE(0x02014B50); directory.writeUInt16LE(20, 4); directory.writeUInt16LE(20, 6);
    directory.writeUInt32LE(crc, 16); directory.writeUInt32LE(data.length, 20);
    directory.writeUInt32LE(data.length, 24); directory.writeUInt16LE(nameBytes.length, 28);
    directory.writeUInt32LE(offset, 42); central.push(directory, nameBytes);
    offset += header.length + nameBytes.length + data.length;
  }
  const directory = Buffer.concat(central), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054B50); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(directory.length, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...local, directory, end]);
}
function locations(bytes) {
  const end = bytes.length - 22;
  return { end, central: bytes.readUInt32LE(end + 16) };
}
function rejected(bytes, match) {
  assert.throws(() => storedTextFiles(bytes), match);
  assert.throws(() => crashMarkdown(bytes, "0.8.5", "Static Recomp"), match);
}

test("shared privacy vectors: every line scrubs to the expected text or is omitted", () => {
  const vectors = JSON.parse(fs.readFileSync(new URL("./privacy_vectors.json", import.meta.url), "utf8"));
  assert.ok(vectors.cases.length >= 30);
  for (const item of vectors.cases)
    assert.equal(scrubLine(item.in, reportNames(item.in, vectors.names)), item.out, item.in.slice(0, 120));
});

test("old launcher ZIP receives scrubbed logs and drops dump, lobby and unknown payloads", () => {
  const bytes = zip([["melee_port_crash.txt", "FATAL: missing itPublicData, version 0.8.5"], ["melee_port_crash.dmp", "BINARY_SECRET"],
    ["melee_port.log", "game log by PlayerOne"], ["lobby.log", "lobby log"], ["settings.ini", "ACCOUNT_SECRET"]]);
  const report = sanitizedReport(bytes, "0.8.5", "Static Recomp", "allocator failed in C:\\Users\\PlayerOne\\src\\heap.c:12");
  assert.match(report.markdown, /Version: 0.8.5/); assert.match(report.markdown, /itPublicData/);
  assert.match(report.markdown, /game log by \[user\]/); assert.equal(report.where, "allocator failed in heap.c:12");
  assert.doesNotMatch(report.markdown, /BINARY_SECRET|ACCOUNT_SECRET|settings\.ini|lobby log|PlayerOne/);
  assert.notDeepEqual(Buffer.from(report.zip), bytes);
  const texts = storedTextFiles(report.zip);
  assert.deepEqual([...texts.keys()], ["melee_port_crash.txt", "melee_port.log", "lobby.log"]);
  assert.doesNotMatch(Buffer.from(report.zip).toString("utf8"), /BINARY_SECRET|ACCOUNT_SECRET|melee_port_crash\.dmp/);
});

test("CR runs, terminal controls and fence injection are normalized", () => {
  const report = crashMarkdown(zip([["melee_port.log", "FATAL: missing itPublicData\r\r\nscene: major 01 minor 02 (frame 94)\r\0\x1b\x7f\u0085```\nattack\n````\n"]]));
  assert.ok(report.includes("FATAL: missing itPublicData\nscene: major 01 minor 02 (frame 94)\n"));
  assert.doesNotMatch(report, /[\r\u0000\u001b\u007f\u0085]/);
  assert.match(report, /attack/);
  const content = report.split("## melee_port.log\n\n```text\n")[1].split("\n```\n")[0];
  assert.doesNotMatch(content, /```/);
});

test("UTF-8 report caps include normalization and fence expansion and retain newest input bytes", () => {
  const report = crashMarkdown(zip([
    ["melee_port_crash.txt", Buffer.alloc(300 * 1024, 0xFF)],
    ["melee_port.log", "EARLY_SECRET" + "```".repeat(240 * 1024) + "\nFATAL: missing itPublicData"],
    ["lobby.log", "日".repeat(180 * 1024)],
  ]));
  assert.ok(Buffer.byteLength(report, "utf8") <= MAX_MARKDOWN_BYTES);
  assert.match(report, /Earlier input bytes omitted/); assert.doesNotMatch(report, /EARLY_SECRET/);
  assert.match(report, /itPublicData/);
});

test("central and local sizes and offsets are bounded", () => {
  const good = zip([["melee_port_crash.txt", "TEXT_PAYLOAD"]]), { end, central } = locations(good);
  for (const [at, value] of [[end + 12, 0xFFFFFFFE], [end + 16, good.length],
    [central + 42, central], [central + 20, 0xFFFFFFFE], [18, 999999]]) {
    const bytes = Buffer.from(good); bytes.writeUInt32LE(value, at); rejected(bytes, /bounded|size|header/i);
  }
  rejected(good.subarray(0, good.length - 1), /missing|truncated/);
});

test("entry count mismatch and local filename disagreement are rejected", () => {
  const bytes = zip([["melee_port_crash.txt", "TEXT_PAYLOAD"]]);
  const count = Buffer.from(bytes); count.writeUInt16LE(0, count.length - 12); count.writeUInt16LE(0, count.length - 14);
  rejected(count, /entry count/);
  const name = Buffer.from(bytes); name[30] = "M".charCodeAt(0); rejected(name, /filenames disagree/);
});

test("duplicate names and case aliases are rejected without partial extraction", () => {
  for (const name of ["melee_port_crash.txt", "MELEE_PORT_CRASH.TXT"])
    rejected(zip([["melee_port_crash.txt", "TEXT_PAYLOAD"], [name, "second"]]), /Duplicate|ambiguous/);
});

test("paths and control-bearing filenames are rejected", () => {
  for (const name of ["../melee_port.log", "dir\\melee_port.log", "C:melee_port.log", "lobby.log\0", ".."])
    rejected(zip([["melee_port_crash.txt", "TEXT_PAYLOAD"], [name, "second"]]), /paths|basename/i);
});

test("encrypted, compressed and descriptor entries return explanatory Markdown", () => {
  for (const [kind, value, match] of [["flags", 1, /Encrypted/], ["flags", 8, /descriptors/], ["method", 8, /Compressed/]]) {
    const bytes = zip([["melee_port_crash.txt", "TEXT_PAYLOAD"]]), { central } = locations(bytes);
    bytes.writeUInt16LE(value, kind === "flags" ? 6 : 8);
    bytes.writeUInt16LE(value, central + (kind === "flags" ? 8 : 10)); rejected(bytes, match);
  }
});

test("ZIP64, split archives, overlapping local regions and bad text checksums fail closed", () => {
  const good = zip([["melee_port_crash.txt", "TEXT_PAYLOAD"]]), { end, central } = locations(good);
  const large = Buffer.from(good); large.writeUInt32LE(0xFFFFFFFF, central + 20); rejected(large, /ZIP64/);
  const split = Buffer.from(good); split.writeUInt16LE(1, end + 4); rejected(split, /Split/);
  const checksum = Buffer.from(good); checksum[30 + "melee_port_crash.txt".length] ^= 1; rejected(checksum, /checksum/);
  const overlap = zip([["melee_port_crash.txt", "TEXT_PAYLOAD"], ["melee_port.log", "second"]]);
  const second = locations(overlap).central + 46 + "melee_port_crash.txt".length;
  overlap.writeUInt32LE(0, second + 42); rejected(overlap, /header|filename|overlap/);
});

test("otherwise valid nested local file regions are rejected before extracting text", () => {
  const bytes = zip([["ignored.bin", Buffer.alloc(512)], ["melee_port_crash.txt", "TEXT_PAYLOAD"]]);
  const second = locations(bytes).central + 46 + "ignored.bin".length;
  const original = bytes.readUInt32LE(second + 42);
  const nested = 30 + "ignored.bin".length + 8;
  const localSize = 30 + "melee_port_crash.txt".length + "TEXT_PAYLOAD".length;
  bytes.copy(bytes, nested, original, original + localSize);
  bytes.writeUInt32LE(nested, second + 42);
  rejected(bytes, /overlap/);
});

test("no allowed texts rejects the archive instead of forwarding dump bytes", () => {
  assert.throws(() => crashMarkdown(zip([["melee_port_crash.dmp", "BINARY_SECRET"]])), /No supported/);
});

test("current crash fixture is packaged in memory and its text is readable", t => {
  const folder = new URL("../../../run-source/crash-1790846146111/", import.meta.url);
  const textPath = fileURLToPath(new URL("melee_port_crash.txt", folder));
  if (!fs.existsSync(textPath)) { t.skip("local crash fixture is not in this checkout"); return; }
  const text = fs.readFileSync(textPath), entries = [["melee_port_crash.txt", text]];
  const dumpPath = fileURLToPath(new URL("melee_port_crash.dmp", folder));
  if (fs.existsSync(dumpPath)) entries.push(["melee_port_crash.dmp", fs.readFileSync(dumpPath)]);
  const report = crashMarkdown(zip(entries), "0.8.63", "Static Recomp");
  assert.match(report, /## melee_port_crash\.txt/); assert.doesNotMatch(report, /Readable report unavailable/);
  const raw = text.toString("utf8"), names = reportNames(raw);
  const expected = raw.split(/[\r\n]/).map(line => scrubLine(line, names)).find(Boolean);
  assert.ok(expected && report.includes(expected));
  assert.doesNotMatch(report, /[A-Za-z]:[\\/]|Users[\\/]|\/home\/|AppData/i);
  for (const name of names) if (name.length >= 3) assert.ok(!report.toLowerCase().includes(name), "user folder name");
});

test("fence escapes cannot push a section past its cap, and the newest lines stay", () => {
  const ticks = ("```".repeat(1000) + "\n").repeat(170);
  const report = crashMarkdown(zip([["melee_port.log", ticks + "FATAL: newest line"]]));
  assert.ok(Buffer.byteLength(report, "utf8") <= MAX_MARKDOWN_BYTES);
  assert.match(report, /FATAL: newest line/);
  assert.doesNotMatch(report.split("## melee_port.log\n\n```text\n")[1].split("\n```\n")[0], /```/);
});

test("relay mock sends ZIP and Markdown under one id without upload protocol changes", async () => {
  const bytes = zip([["melee_port_crash.txt", "FATAL: missing itPublicData"], ["melee_port_crash.dmp", "BINARY_SECRET"]]);
  const originalFetch = globalThis.fetch;
  let captured;
  globalThis.fetch = async (url, options) => {
    assert.equal(url, "https://webhook.invalid/local-mock"); captured = options.body;
    return new Response("", { status: 200 });
  };
  try {
    const request = new Request("https://relay.invalid/report", { method: "POST", body: bytes,
      headers: { "content-type": "application/zip", "content-length": String(bytes.length),
        "x-mu-version": "0.8.5", "x-mu-engine": "Static Recomp" } });
    const response = await relay.fetch(request, { QUOTA: openQuota, REPORTS: memKV(), FORWARD_DISCORD: "1", DISCORD_WEBHOOK_URL: "https://webhook.invalid/local-mock" });
    assert.equal(response.status, 200);
    const zipped = captured.get("files[0]"), markdown = captured.get("files[1]");
    assert.match(zipped.name, /^crash-\d+\.zip$/);
    assert.equal(markdown.name, zipped.name.replace(/\.zip$/, ".md"));
    const forwarded = Buffer.from(await zipped.arrayBuffer());
    assert.notDeepEqual(forwarded, bytes); assert.equal(storedTextFiles(forwarded).size, 1);
    assert.doesNotMatch(forwarded.toString("utf8"), /BINARY_SECRET|melee_port_crash\.dmp/);
    assert.match(await markdown.text(), /itPublicData/); assert.doesNotMatch(await markdown.text(), /BINARY_SECRET/);
    assert.deepEqual(JSON.parse(captured.get("payload_json")).allowed_mentions, { parse: [] });
  } finally { globalThis.fetch = originalFetch; }
});

test("scrub and keep removes identities and file locations but keeps the diagnostic lines", () => {
  const privateLines = [
    "slippi: logged in as PlayerOne (PONE#123)",
    "slippi: using login C:\\Users\\PlayerOne\\AppData\\profile.json",
    "lobby: PlayerTwo endpoint https://server.private.test/join match_id MATCH_PRIVATE",
    "mods: PlayerOne Training Build from /home/PlayerTwo/custom/PersonalMod.iso",
    "connect code PONE#123 peer 192.0.2.42 port 51413 user=PlayerTwo",
    "displayName PlayerOne token SECRET_TOKEN_USER email one@private.test",
    "mail one@private.test from PlayerTwo",
  ].join("\n");
  const text = [
    "CRASH: exception C0000005 at 0000000012345678 (C:\\Users\\PlayerOne\\melee_game.dll+0x21B2C3), version 0.8.63",
    "last guest function 8006B7F8 ftCo_800693AC, lr 8006B80C",
    "recent guest functions (oldest first):",
    "  800693AC HSD_Index2TexCoord",
    "[game] gamepanic /home/PlayerTwo/private/source.c:94 personal=PlayerOne",
    "[game] Cannot find symbol itPublicData from C:\\Users\\PlayerOne\\PersonalMod.dat",
    "assertion \"0\" failed in C:\\Users\\PlayerOne\\private\\source.c on line 94",
    "FATAL: guest fault: call to unmapped guest address (0000D899) in ftCo_800C0658 (800C0658); lr=8035E3F8 r1=804EE740, version 0.8.63",
    "mods: on:  Mods\\Discs\\Some Pack.iso | Detected: Some Pack",
    privateLines,
  ].join("\n");
  const report = sanitizedReport(zip([["melee_port_crash.txt", text], ["melee_port.log", privateLines
    + "\nscene: major 01 minor 02 (frame 94) username PlayerOne"],
    ["lobby.log", text], ["melee_port_crash.dmp", "MINIDUMP_SECRET PlayerOne /home/PlayerTwo"],
    ["PlayerOne-assets.iso", "ASSET_SECRET"]]), "0.8.63", "Static Recomp", privateLines);
  const joined = report.markdown + Buffer.from(report.zip).toString("utf8") + report.where;
  assert.doesNotMatch(joined, /PlayerOne|PlayerTwo|PONE#123|MATCH_PRIVATE|private\.test|192\.0\.2\.42|SECRET_TOKEN|MINIDUMP_SECRET|ASSET_SECRET|C:\\|\/home\/|AppData|Users/);
  for (const expected of ["CRASH: exception C0000005 at 0000000012345678 (melee_game.dll+0x21B2C3), version 0.8.63",
    "last guest function 8006B7F8 ftCo_800693AC, lr 8006B80C", "recent guest functions (oldest first):\n  800693AC HSD_Index2TexCoord",
    "[game] Cannot find symbol itPublicData from PersonalMod.dat", "assertion \"0\" failed in source.c on line 94",
    "FATAL: guest fault: call to unmapped guest address (0000D899) in ftCo_800C0658 (800C0658); lr=8035E3F8 r1=804EE740, version 0.8.63",
    "mods: on:  Mods\\Discs\\Some Pack.iso | Detected: Some Pack", "slippi: using login profile.json",
    "connect code [code] peer [ip] port 51413 user=[user]", "mail [email] from [user]",
    "scene: major 01 minor 02 (frame 94) username [user]", "Version: 0.8.63", "Static Recomp"])
    assert.ok(report.markdown.includes(expected), expected);
  assert.equal(report.where, "");
  const crash = new TextDecoder().decode(storedTextFiles(report.zip).get("melee_port_crash.txt"));
  assert.match(crash, /^\[Privacy: 5 private lines omitted\]\n/);
  const lobby = new TextDecoder().decode(storedTextFiles(report.zip).get("lobby.log"));
  assert.match(lobby, /^\[Privacy: \d+ private lines omitted\]\n\n$/);
  assert.notEqual(report.zip[14], zip([["melee_port_crash.txt", text]])[14]);
});

test("personal metadata headers cannot leak through payload JSON or either attachment", async () => {
  const originalFetch = globalThis.fetch;
  let sent;
  globalThis.fetch = async (url, options) => { sent = options.body; return new Response("", { status: 200 }); };
  try {
    const bytes = zip([["melee_port_crash.txt", "FATAL: missing itPublicData for AlicePrivate\nold log /home/AlicePrivate"],
      ["melee_port_crash.dmp", "BINARY_SECRET BobPrivate"]]);
    const response = await relay.fetch(new Request("https://relay.invalid/report", { method: "POST", body: bytes,
      headers: { "content-type": "application/zip", "content-length": String(bytes.length),
        "x-mu-version": "0.8.63 AlicePrivate", "x-mu-engine": "Static Recomp BobPrivate",
        "x-mu-crash": "FATAL: missing itPublicData C:\\Users\\AlicePrivate\\build\\source.c:94 connect ALICE#123 192.0.2.42",
        "cf-connecting-ip": "192.0.2.42" } }), { QUOTA: openQuota, REPORTS: memKV(), FORWARD_DISCORD: "1", DISCORD_WEBHOOK_URL: "https://webhook.invalid/mock" });
    assert.equal(response.status, 200);
    const payload = sent.get("payload_json"), markdown = await sent.get("files[1]").text();
    const outgoing = Buffer.from(await sent.get("files[0]").arrayBuffer()).toString("utf8");
    assert.doesNotMatch(payload + markdown + outgoing, /AlicePrivate|BobPrivate|ALICE#123|192\.0\.2\.42|C:\\|\/home\/|BINARY_SECRET/);
    assert.match(payload, /unknown/); assert.match(markdown, /FATAL: missing itPublicData for \[user\]\n/);
    assert.equal(JSON.parse(payload).content.split("\n")[1], "FATAL: missing itPublicData source.c:94 connect [code] [ip]");
  } finally { globalThis.fetch = originalFetch; }
});

test("unsupported ZIPs return 415 without calling a webhook", async () => {
  const originalFetch = globalThis.fetch;
  let calls = 0;
  globalThis.fetch = async () => { ++calls; throw new Error("must not send"); };
  try {
    const examples = [zip([["melee_port_crash.dmp", "BINARY_SECRET"]]),
      zip([["melee_port_crash.txt", "FATAL: missing itPublicData"], ["../AlicePrivate", "private"]]),
      zip([["melee_port_crash.txt", "FATAL: missing itPublicData"], ["melee_port_crash.txt", "duplicate"]])];
    const compressed = zip([["melee_port_crash.txt", "FATAL: missing itPublicData"]]);
    compressed.writeUInt16LE(8, 8); compressed.writeUInt16LE(8, locations(compressed).central + 10); examples.push(compressed);
    for (const bytes of examples) {
      const response = await relay.fetch(new Request("https://relay.invalid/report", { method: "POST", body: bytes,
        headers: { "content-type": "application/zip", "content-length": String(bytes.length) } }),
      { QUOTA: openQuota, REPORTS: memKV(), FORWARD_DISCORD: "1", DISCORD_WEBHOOK_URL: "https://webhook.invalid/mock" });
      assert.equal(response.status, 415);
    }
    assert.equal(calls, 0);
  } finally { globalThis.fetch = originalFetch; }
});

test("oversized lines are omitted before any scrubbing", () => {
  const unknown = "PlayerOne" + "x".repeat(512 * 1024);
  const fatal = "FATAL: C:\\Users\\PlayerOne\\" + "x".repeat(512 * 1024) + "\\source.c:94 itPublicData";
  assert.equal(scrubLine(unknown), null); assert.equal(scrubLine(fatal), null);
  assert.equal(scrubLine("x".repeat(4097)), null); assert.equal(scrubLine("x".repeat(4096)), "x".repeat(4096));
  const report = sanitizedReport(zip([["melee_port_crash.txt", fatal],
    ["melee_port.log", unknown + "\nscene: major 01 minor 02 (frame 94)"]]), "0.8.63", "Static Recomp", fatal);
  assert.doesNotMatch(report.markdown, /PlayerOne|itPublicData|source\.c:94/);
  assert.match(report.markdown, /major 01 minor 02 \(frame 94\)/);
  assert.equal(report.where, "");
});

test("a user folder name is replaced everywhere, including a source file named after it", () => {
  const report = sanitizedReport(zip([["melee_port_crash.txt",
    "FATAL: game panic at C:\\Users\\HiddenUser\\HiddenUser.cpp:94, version 0.8.63\n"
    + "FATAL: game panic at C:\\Users\\HiddenUser\\src\\lbarchive.c:94, version 0.8.63\n"
    + "FATAL: game panic at /home/HiddenUser/lbarchive.c:95, version 0.8.63\n"
    + "  8038FD54 fn_HiddenUserHandler"]]),
  "0.8.63", "Static Recomp", "FATAL: game panic at D:\\build\\hiddenuser\\HiddenUser.h:233");
  const output = report.markdown + Buffer.from(report.zip).toString("utf8") + report.where;
  assert.doesNotMatch(output, /HiddenUser[^H]|hiddenuser|C:\\|D:\\|\/home\//);
  assert.match(output, /game panic at \[user\]\.cpp:94, version/); assert.match(output, /game panic at lbarchive\.c:94, version/);
  assert.doesNotMatch(output, /lbarchive\.c:95/); assert.match(output, /fn_HiddenUserHandler/);
  assert.equal(report.where, "FATAL: game panic at [user].h:233");
  const second = sanitizedReport(report.zip, report.version, report.engine, report.where);
  assert.match(second.markdown, /\[user\]\.cpp:94/); assert.equal(second.where, report.where);
});

test("new launcher scrubbed text survives the relay's second privacy pass unchanged", () => {
  const text = "CRASH: exception C0000005 at 0000000012345678 (melee_game.dll+0x21B2C3), version 0.8.63\n"
    + "last guest function 8006B7F8 ftCo_800693AC, lr 8006B80C\n"
    + "FATAL: game stopped at lbarchive.c:94: , version 0.8.63\n[game] Cannot find symbol itPublicData.\n"
    + "native practice: opponent code [code] at [ip], host [user], mail [email]\n[12 lines omitted for privacy]\n";
  const report = sanitizedReport(zip([["melee_port_crash.txt", text]]), "0.8.63", "Source Port", "CRASH: exception C0000005 at 0000000012345678 (melee_game.dll+0x21B2C3), version 0.8.63");
  assert.ok(report.markdown.includes("[Privacy: 0 private lines omitted]\n" + text));
  assert.match(report.where, /C0000005/);
});

test("a cut game log keeps its session start lines, scrubbed, before the newest lines", () => {
  const start = "Melee Unlocked 0.8.69, melee_port.exe\n" +
                "gecko: no user codes (C:\\Users\\Somebody\\Games\\Melee\\GeckoCodes.ini not found)\n" +
                "settings: D3D12, frame cap 240, sub-frame off\n" +
                "cosmetics: 3 disc files overridden\n";
  const middle = "sim frame 1 took 20.0 ms\n".repeat(40 * 1024);
  const report = crashMarkdown(zip([["melee_port.log", start + middle + "FATAL: OSPanic at pobj.c:1896"]]));
  assert.ok(Buffer.byteLength(report, "utf8") <= MAX_MARKDOWN_BYTES);
  assert.match(report, /Melee Unlocked 0\.8\.69, melee_port\.exe/);
  assert.match(report, /cosmetics: 3 disc files overridden/);
  assert.doesNotMatch(report, /Somebody/);
  assert.match(report, /Earlier input bytes omitted/);
  assert.match(report, /pobj\.c:1896/);
  assert.ok(report.indexOf("cosmetics: 3 disc files") < report.indexOf("Earlier input bytes omitted"));
});

function memKV() {
  const data = new Map();
  return {
    data,
    async put(key, value, options = {}) { data.set(key, { value, metadata: options.metadata }); },
    async get(key, type) {
      const e = data.get(key); if (!e) return null;
      if (type === "arrayBuffer") return typeof e.value === "string" ? new TextEncoder().encode(e.value).buffer : e.value.buffer ?? e.value;
      return typeof e.value === "string" ? e.value : new TextDecoder().decode(e.value);
    },
    async delete(key) { data.delete(key); },
    async list({ prefix = "" } = {}) {
      return { keys: [...data.keys()].filter(k => k.startsWith(prefix)).map(name => ({ name, metadata: data.get(name).metadata })), list_complete: true };
    },
  };
}

test("reports are stored with the player's note and trace, and only the admin token reads them", async () => {
  const kv = memKV();
  const bytes = zip([["melee_port.log", "scene: major 02 minor 02 (frame 94)\nslippi: logged in as AlicePrivate"],
    ["session.trace", "frame,inputs,checksum\n1,0000,abcd\n"]]);
  const env = { QUOTA: openQuota, REPORTS: kv, ADMIN_TOKEN: "t0ken-long-enough" };
  const response = await relay.fetch(new Request("https://relay.invalid/report", { method: "POST", body: bytes,
    headers: { "content-type": "application/zip", "content-length": String(bytes.length), "x-mu-kind": "logs",
      "x-mu-version": "0.8.83", "x-mu-engine": "Static Recomp",
      "x-mu-note": encodeURIComponent("Desync at frame 94 vs AlicePrivate, C:\\Users\\AlicePrivate\\x") } }), env);
  assert.equal(response.status, 200);
  const list = await relay.fetch(new Request("https://relay.invalid/admin/list", { headers: { authorization: "Bearer t0ken-long-enough" } }), env);
  const { items } = await list.json();
  assert.equal(items.length, 1); assert.equal(items[0].meta.kind, "logs"); assert.equal(items[0].meta.trace, true);
  const md = await (await relay.fetch(new Request("https://relay.invalid/admin/get?key=" + encodeURIComponent(items[0].key),
    { headers: { authorization: "Bearer t0ken-long-enough" } }), env)).text();
  assert.match(md, /What the player says happened/); assert.match(md, /Desync at frame 94/);
  assert.doesNotMatch(md, /C:\\|logged in as/); // a name the player typed in the note stays; paths do not
  const trace = await (await relay.fetch(new Request("https://relay.invalid/admin/trace?key=" + encodeURIComponent(items[0].key),
    { headers: { authorization: "Bearer t0ken-long-enough" } }), env)).text();
  assert.match(trace, /^frame,inputs,checksum\n1,0000,abcd/);
  const denied = await relay.fetch(new Request("https://relay.invalid/admin/list", { headers: { authorization: "Bearer wrong-token-xxxxx" } }), env);
  assert.equal(denied.status, 401);
});

test("the header names the crashed game's version when it differs from the launcher's", () => {
  assert.equal(gameVersionLabel("CRASH: exception C00000FD at 00007FF6F63752F7 (melee_port.exe+0x1DF52F7), version 0.8.77", "0.8.82"),
               "game 0.8.77 (launcher 0.8.82)");
  assert.equal(gameVersionLabel("CRASH: exception C0000005 at 1 (melee_game.dll+0x1), version 0.8.82", "0.8.82"), "0.8.82");
  assert.equal(gameVersionLabel("", "0.8.82"), "0.8.82");
});

test("a logs report names the game version from the game's own log", () => {
  assert.equal(logGameVersion("[Privacy: 0 private lines omitted]\nMelee Unlocked 0.8.79, melee_port.exe\ngecko: x"), "0.8.79");
  assert.equal(logGameVersion("no version line"), "");
  assert.equal(gameVersionLabel("version " + logGameVersion("Melee Unlocked 0.8.79, melee_port.exe"), "0.8.84"),
               "game 0.8.79 (launcher 0.8.84)");
});
