import { test } from "node:test";
import assert from "node:assert/strict";
import { reserve } from "../src/index.js";

// Storage that hands out requests one at a time, as a Durable Object does while it waits on its own
// storage (its input gate): each reserve() runs to the end before the next starts.
function gatedStorage() {
  const data = new Map();
  let queue = Promise.resolve();
  const storage = {
    async get(key) { return data.get(key); },
    async put(entries) { for (const [key, value] of Object.entries(entries)) data.set(key, value); },
    async delete(keys) { for (const key of keys) data.delete(key); },
    async list() { return new Map(data); },
  };
  const run = (ip, now) => (queue = queue.then(() => reserve(storage, ip, now)));
  return { data, run };
}

test("reports sent at the same moment from one address: one passes", async () => {
  const { run } = gatedStorage();
  const now = Date.parse("2026-10-02T12:00:00Z");
  const results = await Promise.all(Array.from({ length: 60 }, () => run("1.2.3.4", now)));
  assert.equal(results.filter((r) => r.ok).length, 1);
  assert.ok(results.filter((r) => !r.ok).every((r) => r.reason === "slow down"));
});

test("the daily limit holds for many addresses at once", async () => {
  const { run } = gatedStorage();
  const now = Date.parse("2026-10-02T12:00:00Z");
  const results = await Promise.all(Array.from({ length: 80 }, (_, i) => run("10.0.0." + i, now)));
  assert.equal(results.filter((r) => r.ok).length, 50);
  assert.ok(results.filter((r) => !r.ok).every((r) => r.reason === "daily limit"));
});

test("an address may send again after ten minutes, and a new day starts a new count", async () => {
  const { data, run } = gatedStorage();
  const now = Date.parse("2026-10-02T23:55:00Z");
  assert.equal((await run("1.2.3.4", now)).ok, true);
  assert.equal((await run("1.2.3.4", now + 9 * 60 * 1000)).ok, false);
  assert.equal((await run("1.2.3.4", now + 10 * 60 * 1000 + 1)).ok, true);
  assert.equal(data.get("day:2026-10-03"), 1);
  assert.equal(data.has("day:2026-10-02"), false, "the previous day's counter is dropped");
});
