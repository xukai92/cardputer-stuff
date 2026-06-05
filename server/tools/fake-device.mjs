#!/usr/bin/env node
/*
 * fake-device — a terminal stand-in for the Cardputer.
 *
 * Speaks the EXACT wire protocol the firmware uses, so if this works against
 * your bridge (locally or through the Cloudflare tunnel), the real device will
 * too — isolating "server/tunnel problem" from "ESP32/WiFi problem".
 *
 * Usage:
 *   node tools/fake-device.mjs 'wss://cardputer.example.com/?token=SECRET'
 *   # or take the URL from the env var the firmware build uses:
 *   CARDPUTER_CLAUDE_WS_URI='wss://...' node tools/fake-device.mjs
 *
 * Type a line + enter to send. Ctrl-C to quit.
 */
import { WebSocket } from "ws";
import readline from "node:readline";

const uri = process.argv[2] || process.env.CARDPUTER_CLAUDE_WS_URI;
if (!uri) {
  console.error("usage: node tools/fake-device.mjs '<ws(s)://host/?token=...>'");
  console.error("   or: CARDPUTER_CLAUDE_WS_URI=... node tools/fake-device.mjs");
  process.exit(2);
}

const redact = (u) => u.replace(/token=[^&]*/, "token=***");
console.error(`connecting to ${redact(uri)} ...`);

const ws = new WebSocket(uri);

ws.on("open", () => console.error("● connected (type a message, enter to send; Ctrl-C to quit)\n"));

ws.on("message", (data) => {
  const raw = data.toString();
  let msg;
  try {
    msg = JSON.parse(raw);
  } catch {
    console.log("raw <", raw);
    return;
  }
  if (msg.type === "reply") console.log("claude <", msg.text);
  else if (msg.type === "status") console.log("[status]", msg.text);
  else console.log("?", raw);
});

ws.on("close", (code, reason) => {
  console.error(`\n○ closed (code=${code}${reason?.length ? `, ${reason}` : ""})`);
  process.exit(0);
});

ws.on("error", (err) => {
  // 401 here = bad/missing token; ENOTFOUND/ECONNREFUSED = wrong host/port/tunnel down.
  console.error("✗ error:", err.message);
});

const rl = readline.createInterface({ input: process.stdin });
rl.on("line", (line) => {
  const text = line.trim();
  if (!text) return;
  if (ws.readyState !== WebSocket.OPEN) {
    console.error("(not connected — message dropped)");
    return;
  }
  ws.send(JSON.stringify({ type: "msg", text }));
  console.log("you >", text);
});
rl.on("close", () => ws.close());
