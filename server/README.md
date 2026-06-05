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

## Test it WITHOUT the Cardputer (recommended first step)

Use `websocat` (or `wscat`) on your laptop to stand in for the device. Include
the token:

```bash
# install: brew install websocat   (or: cargo install websocat)
websocat 'ws://localhost:8787/?token=YOUR_TOKEN'
# or, through the tunnel:
websocat 'wss://cardputer.example.com/?token=YOUR_TOKEN'
```

Type a line and press enter — it should appear in your Claude TUI as a
`<channel>` message, and Claude's `cardputer_reply` calls come back to your
websocat terminal as JSON like `{"type":"reply","text":"…"}`.

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
