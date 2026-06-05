#!/usr/bin/env node
/*
 * End-to-end smoke test for the bridge — no Cardputer, no real Claude needed.
 *
 * Plays BOTH sides:
 *   - the MCP stdio client (stands in for Claude Code), spawning the server
 *   - the WebSocket client (stands in for the Cardputer)
 *
 * Verifies:
 *   1. device -> server: a WS message arrives at Claude as a
 *      `notifications/claude/channel` with a <channel> tag.
 *   2. Claude -> device: calling the `cardputer_reply` tool pushes a
 *      {type:"reply"} frame to the WS client.
 */
import { WebSocket } from "ws";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const PORT = "8799";
const fail = (msg) => {
  console.error("FAIL:", msg);
  process.exit(1);
};
const withTimeout = (p, ms, label) =>
  Promise.race([
    p,
    new Promise((_, rej) => setTimeout(() => rej(new Error(`timeout waiting for ${label}`)), ms)),
  ]);

const transport = new StdioClientTransport({
  command: "node",
  args: ["src/index.mjs"],
  env: { ...process.env, CARDPUTER_WS_PORT: PORT },
});

const client = new Client(
  { name: "smoke-test", version: "0.0.0" },
  { capabilities: { experimental: { "claude/channel": {} } } }
);

// Catch the custom channel notification the server sends.
let channelResolve;
const channelGot = new Promise((r) => (channelResolve = r));
client.fallbackNotificationHandler = async (n) => {
  if (n.method === "notifications/claude/channel") channelResolve(n);
};

await client.connect(transport);
console.error("• MCP client connected (stands in for Claude)");

// Connect the WS client (stands in for the Cardputer).
const ws = new WebSocket(`ws://127.0.0.1:${PORT}`);
const replyGot = new Promise((resolve) => {
  ws.on("message", (data) => {
    const msg = JSON.parse(data.toString());
    if (msg.type === "reply") resolve(msg);
  });
});
await withTimeout(new Promise((r) => ws.on("open", r)), 4000, "ws open");
console.error("• WS client connected (stands in for Cardputer)");

// 1. device -> Claude
ws.send(JSON.stringify({ type: "msg", text: "hello from the cardputer" }));
const note = await withTimeout(channelGot, 4000, "channel notification");
if (!note.params?.content?.includes("hello from the cardputer"))
  fail(`channel notification missing text: ${JSON.stringify(note.params)}`);
if (!note.params.content.includes('source="cardputer"'))
  fail(`channel tag missing source attr: ${note.params.content}`);
console.error("✓ device -> Claude: got notifications/claude/channel with <channel> tag");

// 2. Claude -> device
const toolRes = await withTimeout(
  client.callTool({ name: "cardputer_reply", arguments: { text: "hi cardputer!" } }),
  4000,
  "tool result"
);
const reply = await withTimeout(replyGot, 4000, "ws reply frame");
if (reply.text !== "hi cardputer!") fail(`unexpected reply: ${JSON.stringify(reply)}`);
console.error("✓ Claude -> device: cardputer_reply pushed reply frame to WS client");
console.error("  tool result:", JSON.stringify(toolRes.content?.[0]?.text));

ws.close();
await client.close();

// --- token enforcement -----------------------------------------------------
// Spawn a standalone server WITH a token and verify the gate.
const { spawn } = await import("node:child_process");
const TOK = "s3cret-test-token";
const TPORT = "8801";
const child = spawn("node", ["src/index.mjs"], {
  env: { ...process.env, CARDPUTER_WS_PORT: TPORT, CARDPUTER_TOKEN: TOK },
  stdio: ["ignore", "ignore", "pipe"],
});
await withTimeout(
  new Promise((res) => child.stderr.on("data", (d) => String(d).includes("listening") && res())),
  4000,
  "token-server listening"
);

const connects = (uri) =>
  new Promise((resolve) => {
    const w = new WebSocket(uri);
    w.on("open", () => {
      w.close();
      resolve(true);
    });
    w.on("error", () => resolve(false));
  });

if (await connects(`ws://127.0.0.1:${TPORT}`)) fail("connection WITHOUT token was accepted");
console.error("✓ auth: connection without token rejected");
if (await connects(`ws://127.0.0.1:${TPORT}/?token=wrong`)) fail("connection with WRONG token accepted");
console.error("✓ auth: wrong token rejected");
if (!(await connects(`ws://127.0.0.1:${TPORT}/?token=${TOK}`))) fail("connection with correct token rejected");
console.error("✓ auth: correct token accepted");
child.kill();

console.error("\nPASS: full device <-> Claude loop + token auth work");
process.exit(0);
