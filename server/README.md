# cardputer-claude-bridge

Bridges a Cardputer (over WebSocket) to a **real Claude Code session** running in
TUI mode, using the Anthropic channel protocol (`claude/channel`).

```
Cardputer  --WebSocket-->  this server  --stdio (MCP)-->  claude (TUI, full agent)
 (chat app) <----------                 <-------------    on your remote dev box
```

- A message from the device is injected into the live Claude session as a
  `<channel source="cardputer">…</channel>` tag (read mid-conversation — no overlay).
- Claude answers by calling the `cardputer_reply` tool; the text is pushed back to
  the device over WebSocket.

Modeled on the `zulip` plugin in
[meowkey-dev/machine-plugins](https://github.com/meowkey-dev/machine-plugins).

## Install

```bash
cd server
npm install
```

## Wire it into Claude Code

This is an MCP server Claude spawns over stdio. Add it to your project's
`.mcp.json` (see `.mcp.json.example`):

```json
{
  "mcpServers": {
    "cardputer": {
      "command": "node",
      "args": ["/ABSOLUTE/PATH/TO/cardputer-stuff/server/src/index.mjs"],
      "env": {
        "CARDPUTER_WS_PORT": "8787",
        "CARDPUTER_TOKEN": "a-long-random-secret"
      }
    }
  }
}
```

Then start Claude Code normally (`claude`). When it loads, the bridge starts a
WebSocket server on `CARDPUTER_WS_PORT` (default **8787**) and registers the
channel + `cardputer_reply` tool with the session.

## Security — read this

This channel injects messages into a Claude session that **can run shell
commands and edit files** on this machine. Treat it like SSH:

- **Always set `CARDPUTER_TOKEN`** (a long random secret, e.g. `openssl rand -hex 32`).
  Clients must present it as a `?token=…` query param or an `x-cardputer-token`
  header. Without it, connections are rejected (`401`). If `CARDPUTER_TOKEN` is
  unset the server allows everyone and prints a warning — only acceptable on
  localhost.
- **Never expose `ws://` to the internet.** Put TLS in front (see Cloudflare
  Tunnel below) so the token and traffic are encrypted.

## Expose it with Cloudflare Tunnel (no open ports)

`cloudflared` makes an outbound connection to Cloudflare and maps a hostname to
your local port — no inbound firewall holes, free TLS.

```yaml
# ~/.cloudflared/config.yml
tunnel: <your-tunnel-id>
credentials-file: /home/you/.cloudflared/<your-tunnel-id>.json
ingress:
  - hostname: cardputer.example.com
    service: ws://localhost:8787      # WebSocket upgrade is proxied as-is
  - service: http_status:404
```

```bash
cloudflared tunnel route dns <tunnel> cardputer.example.com
cloudflared tunnel run <tunnel>
```

The device then connects to `wss://cardputer.example.com/?token=<TOKEN>`.

## Test it WITHOUT the Cardputer (do this before flashing)

Test in **layers** — each adds one piece, so a failure points at the right
place. If layer 3 works, the firmware will too (it speaks the same protocol).

**Layer 0 — protocol logic (no network, no Claude):**
```bash
npm test          # smoke test: channel round-trip + token auth enforcement
```

**Layer 1 — bridge locally, with a real Claude session:**
Wire up `.mcp.json`, run `claude`, then in another terminal use the faithful
stand-in (speaks the exact frames the firmware sends):
```bash
npm run fake-device 'ws://localhost:8787/?token=YOUR_TOKEN'
```
Type a line → it appears in the Claude TUI as a `<channel>` message; Claude's
`cardputer_reply` comes back as `claude < …`. This isolates "is my server ok"
from "is my tunnel ok".

**Layer 2 — through the Cloudflare tunnel (the real path the device uses):**
```bash
npm run fake-device 'wss://cardputer.example.com/?token=YOUR_TOKEN'
```
Now you've validated TLS + tunnel + token + the whole loop. Flash with
confidence.

`websocat` works too (it sends raw lines, which the server also accepts):
```bash
websocat 'wss://cardputer.example.com/?token=YOUR_TOKEN'
```

## Wire protocol (device <-> server)

JSON frames over WebSocket (plain-text frames from the device are also accepted
and treated as the message text):

- **device → server:** `{"type":"msg","text":"hello claude"}`
- **server → device:** `{"type":"reply","text":"hi!"}` and `{"type":"status","text":"connected"}`

## Config

| Env var | Default | Meaning |
| --- | --- | --- |
| `CARDPUTER_WS_PORT` | `8787` | Port the WebSocket server listens on |
| `CARDPUTER_TOKEN` | _(none)_ | Shared secret clients must present (`?token=` or `x-cardputer-token`). Unset = allow all (localhost only). |
