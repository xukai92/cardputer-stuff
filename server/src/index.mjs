#!/usr/bin/env node
/*
 * cardputer-claude-bridge
 *
 * An MCP server that bridges a Cardputer (handheld, over WebSocket) to a real
 * Claude Code session using the Anthropic channel protocol.
 *
 * Modeled on the `zulip` plugin from meowkey-dev/machine-plugins, but the chat
 * surface is a WebSocket to the device instead of Zulip.
 *
 *   Cardputer  --WebSocket-->  THIS SERVER  --stdio (MCP)-->  claude (TUI)
 *    (chat app) <-----------                <-------------    (full agent)
 *
 * Flow:
 *   - Device sends a chat message over WS  -> we emit a `notifications/claude/channel`
 *     notification carrying a <channel> tag, which the running Claude session reads
 *     mid-conversation (no overlay; the real TUI session handles it).
 *   - Claude calls the `cardputer_reply` tool to answer -> we forward the text to the
 *     device over WS.
 *
 * Transport to Claude: stdio (Claude Code spawns this as a plugin / MCP server).
 * IMPORTANT: in stdio mode, stdout is the MCP channel — never write logs there.
 * All logging goes to stderr.
 */

import crypto from "node:crypto";
import { WebSocketServer } from "ws";
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { CallToolRequestSchema, ListToolsRequestSchema } from "@modelcontextprotocol/sdk/types.js";

const WS_PORT = parseInt(process.env.CARDPUTER_WS_PORT ?? "8787", 10);

// Shared secret. Connections must present it as ?token=... or an
// `x-cardputer-token` header. THIS CHANNEL CAN RUN SHELL COMMANDS VIA CLAUDE —
// always set a token when the bridge is reachable beyond localhost.
const TOKEN = process.env.CARDPUTER_TOKEN ?? "";

function log(...args) {
  // stderr only — stdout is reserved for the MCP stdio protocol.
  process.stderr.write(`[cardputer-bridge] ${args.join(" ")}\n`);
}

function extractToken(req) {
  const header = req.headers["x-cardputer-token"];
  if (header) return Array.isArray(header) ? header[0] : header;
  try {
    return new URL(req.url, "http://localhost").searchParams.get("token") ?? "";
  } catch {
    return "";
  }
}

function tokenOk(provided) {
  if (!TOKEN) return true; // no token configured -> allow (see startup warning)
  if (!provided) return false;
  const a = Buffer.from(provided);
  const b = Buffer.from(TOKEN);
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

function escapeAttr(s) {
  return String(s).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
}

// Build a <channel ...>body</channel> tag, matching the zulip plugin's format.
function formatChannelTag(attrs, body) {
  const attrStr = Object.entries(attrs)
    .map(([k, v]) => `${k}="${escapeAttr(v)}"`)
    .join(" ");
  return `<channel ${attrStr}>\n${body}\n</channel>`;
}

// ---------------------------------------------------------------------------
// MCP server (talks to Claude over stdio)
// ---------------------------------------------------------------------------

const mcp = new Server(
  { name: "cardputer-claude-bridge", version: "0.1.0" },
  {
    capabilities: {
      tools: {},
      experimental: { "claude/channel": {} },
    },
    instructions:
      "Messages tagged <channel source=\"cardputer\"> come from the user's handheld " +
      "Cardputer device. Treat them as direct requests from the user. To answer the " +
      "person on the Cardputer, call the `cardputer_reply` tool with your reply text. " +
      "Keep replies short — the device screen is tiny (~30 columns).",
  }
);

// Tool list: a single `cardputer_reply` tool Claude uses to answer the device.
mcp.setRequestHandler(ListToolsRequestSchema, async () => ({
  tools: [
    {
      name: "cardputer_reply",
      description:
        "Send a text reply to the user's Cardputer handheld. Use this to answer a " +
        "message that arrived as a <channel source=\"cardputer\"> tag. Keep it short " +
        "(the screen is ~30 columns wide).",
      inputSchema: {
        type: "object",
        properties: {
          text: { type: "string", description: "The reply text to show on the Cardputer." },
        },
        required: ["text"],
      },
    },
  ],
}));

mcp.setRequestHandler(CallToolRequestSchema, async (req) => {
  if (req.params.name === "cardputer_reply") {
    const text = String(req.params.arguments?.text ?? "");
    const n = broadcast({ type: "reply", text });
    log(`reply -> ${n} client(s):`, JSON.stringify(text.slice(0, 60)));
    return {
      content: [
        {
          type: "text",
          text:
            n > 0
              ? `Sent to ${n} Cardputer client(s).`
              : "No Cardputer connected — reply was not delivered.",
        },
      ],
    };
  }
  return {
    content: [{ type: "text", text: `Unknown tool "${req.params.name}".` }],
    isError: true,
  };
});

// Push an inbound device message into the Claude session as a channel notification.
async function deliverToClaude(text, sender = "user") {
  const timestamp = new Date().toISOString();
  const content = formatChannelTag({ source: "cardputer", sender, timestamp }, text);
  try {
    await mcp.notification({
      method: "notifications/claude/channel",
      params: { content, meta: { source: "cardputer", sender, timestamp } },
    });
    log("delivered message to Claude:", JSON.stringify(text.slice(0, 60)));
  } catch (err) {
    log("failed to deliver to Claude:", String(err));
  }
}

// ---------------------------------------------------------------------------
// WebSocket server (talks to the Cardputer)
// ---------------------------------------------------------------------------

let wss = null;

function broadcast(obj) {
  if (!wss) return 0;
  const payload = JSON.stringify(obj);
  let n = 0;
  for (const client of wss.clients) {
    if (client.readyState === 1 /* OPEN */) {
      client.send(payload);
      n++;
    }
  }
  return n;
}

function startWebSocketServer() {
  wss = new WebSocketServer({
    port: WS_PORT,
    verifyClient: (info, cb) => {
      if (tokenOk(extractToken(info.req))) return cb(true);
      log(`rejected unauthorized connection from ${info.req.socket.remoteAddress}`);
      cb(false, 401, "Unauthorized");
    },
  });

  if (!TOKEN) {
    log("WARNING: CARDPUTER_TOKEN is not set — connections are UNAUTHENTICATED.");
    log("         Set CARDPUTER_TOKEN before exposing the bridge beyond localhost.");
  }
  wss.on("listening", () => log(`WebSocket server listening on ws://0.0.0.0:${WS_PORT}`));
  wss.on("error", (err) => log("WebSocket server error:", String(err)));

  wss.on("connection", (ws, req) => {
    const peer = req.socket.remoteAddress;
    log(`Cardputer connected from ${peer} (${wss.clients.size} total)`);
    ws.send(JSON.stringify({ type: "status", text: "connected" }));

    ws.on("message", (data) => {
      const raw = data.toString().trim();
      if (!raw) return;

      // Accept either a JSON frame {type:"msg", text:"..."} or plain text.
      let text = raw;
      try {
        const parsed = JSON.parse(raw);
        if (parsed && typeof parsed.text === "string") text = parsed.text;
      } catch {
        // not JSON — treat the whole frame as the message text
      }
      if (!text) return;

      deliverToClaude(text);
    });

    ws.on("close", () => log(`Cardputer disconnected (${wss.clients.size} remaining)`));
    ws.on("error", (err) => log("client socket error:", String(err)));
  });
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

async function main() {
  startWebSocketServer();
  const transport = new StdioServerTransport();
  await mcp.connect(transport);
  log("MCP server connected over stdio; channel capability active.");
}

main().catch((err) => {
  log("fatal:", String(err));
  process.exit(1);
});
