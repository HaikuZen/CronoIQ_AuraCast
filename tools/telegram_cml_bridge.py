#!/usr/bin/env python3
"""Telegram -> HTTP/JSON bridge for the CML weather bot (@CML_robot).

Why this exists
----------------
CronoIQ_AuraCast's "telegram" weather provider wants hyperlocal current
conditions from @CML_robot. That bot only replies to a real Telegram *user*
account messaging it privately with "/realtime" followed by the locality as
a separate follow-up message (many bots ignore messages from other bots).
Speaking Telegram's real client protocol (MTProto -- the thing api_id/
api_hash from https://my.telegram.org are for) isn't something ESP32
firmware can realistically do: it's an encrypted binary protocol with its
own handshake and serialization format, not a simple REST API.

So instead, this small script runs on a normal always-on computer (a
Raspberry Pi, a spare Linux box, a cheap VPS -- anything that can stay
online on your network), logs in ONCE as your own Telegram account using
Telethon (a mature, actively-maintained MTProto client library), and
exposes a tiny local HTTP endpoint. The ESP32 firmware just does a plain
HTTP GET against that endpoint -- exactly like every other weather provider
in this project.

NOT independently tested against the real @CML_robot / Telegram servers in
this repo (no Telegram login is possible from the environment this was
written in) -- the request/response shape and the "/realtime" + locality
two-message flow match what was observed manually, but timing / exact
message matching may need a small tweak once you run it for real. Watch the
script's own stdout and the ESP32's serial log (`wx.telegram_cml` /
`app_weather` tags) when testing.

Setup
-----
1.  python3 -m venv venv && source venv/bin/activate   (optional but tidy)
    pip install -r requirements.txt

2.  Get api_id / api_hash: https://my.telegram.org -> "API development
    tools" -> create an app. These identify the *application*, not you.

3.  Run once interactively to log in as your own account (phone number,
    then the login code Telegram sends you, then your 2FA password if you
    have one set):

        python3 telegram_cml_bridge.py --api-id 123456 --api-hash abcdef0123456789abcdef0123456789

    This creates cml_bridge.session next to the script. Future runs reuse
    it silently -- no more prompts. Treat that .session file like a
    password: anyone with it can act as your Telegram account. Don't commit
    it, don't share it.

4.  (Recommended) Protect the HTTP endpoint with a shared secret so nothing
    else on your network can make your account message @CML_robot:

        BRIDGE_TOKEN=some-long-random-string python3 telegram_cml_bridge.py --api-id ... --api-hash ...

    and put the same value in config.json's telegram.bridge_token.

5.  Point the ESP32 at this host in /sdcard/config.json:

        "telegram": {
          "enabled": true,
          "bridge_url": "http://192.168.1.50:8765",
          "bridge_token": "some-long-random-string",
          "locality": "Milano Arbe"
        }

   Run this script as a systemd service / cron @reboot / tmux session so it
   survives reboots -- the firmware calls it every 30 minutes.

HTTP API
--------
GET /realtime?locality=<name>[&token=<bridge_token>]
    -> 200 {"ok": true, "text": "<raw @CML_robot reply, as-is>"}
    -> 4xx/5xx {"ok": false, "error": "<reason>"}

GET /
    -> 200 {"ok": true, "service": "telegram_cml_bridge"}   (health check)
"""
from __future__ import annotations

import argparse
import asyncio
import os
import time

from aiohttp import web
from telethon import TelegramClient

CML_BOT = "CML_robot"
SEND_GAP_S = 0.8          # pause between "/realtime" and the locality message
REPLY_TIMEOUT_S = 12      # must stay comfortably under weather_http_get()'s 15s
POLL_INTERVAL_S = 1.5
SESSION_NAME = "cml_bridge"

client: "TelegramClient | None" = None
bridge_token = os.environ.get("BRIDGE_TOKEN", "")


async def fetch_realtime(locality: str) -> str:
    assert client is not None
    entity = await client.get_entity(CML_BOT)

    since = time.time()
    await client.send_message(entity, "/realtime")
    await asyncio.sleep(SEND_GAP_S)
    await client.send_message(entity, locality)

    deadline = time.time() + REPLY_TIMEOUT_S
    while time.time() < deadline:
        async for msg in client.iter_messages(entity, limit=10):
            if msg.out:
                continue  # our own two messages
            if msg.date.timestamp() < since - 1:
                break  # scrolled past anything from before we asked
            if msg.text and "Temperatura" in msg.text:
                return msg.text
        await asyncio.sleep(POLL_INTERVAL_S)
    raise TimeoutError(f"no reply from @{CML_BOT} within {REPLY_TIMEOUT_S}s")


async def handle_realtime(request: web.Request) -> web.Response:
    if bridge_token and request.query.get("token") != bridge_token:
        return web.json_response({"ok": False, "error": "bad token"}, status=403)

    locality = request.query.get("locality", "").strip()
    if not locality:
        return web.json_response({"ok": False, "error": "missing ?locality="}, status=400)

    try:
        text = await fetch_realtime(locality)
        return web.json_response({"ok": True, "text": text})
    except Exception as exc:  # noqa: BLE001 - report any failure to the caller
        print(f"[telegram_cml_bridge] /realtime?locality={locality!r} failed: {exc}")
        return web.json_response({"ok": False, "error": str(exc)}, status=504)


async def handle_health(request: web.Request) -> web.Response:
    return web.json_response({"ok": True, "service": "telegram_cml_bridge"})


async def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--api-id", type=int, required=True, help="from https://my.telegram.org")
    ap.add_argument("--api-hash", required=True, help="from https://my.telegram.org")
    ap.add_argument("--host", default="0.0.0.0", help="bind address (default: all interfaces)")
    ap.add_argument("--port", type=int, default=8765)
    args = ap.parse_args()

    global client
    client = TelegramClient(SESSION_NAME, args.api_id, args.api_hash)
    await client.start()  # prompts for phone/code/2FA only when there's no session yet
    me = await client.get_me()
    print(f"[telegram_cml_bridge] logged in as {me.first_name} (id {me.id})")
    if not bridge_token:
        print("[telegram_cml_bridge] WARNING: no BRIDGE_TOKEN set -- the HTTP endpoint "
              "is unauthenticated. Fine on a trusted LAN only.")

    app = web.Application()
    app.router.add_get("/realtime", handle_realtime)
    app.router.add_get("/", handle_health)

    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, args.host, args.port)
    await site.start()
    print(f"[telegram_cml_bridge] listening on http://{args.host}:{args.port}")

    await client.run_until_disconnected()


if __name__ == "__main__":
    asyncio.run(main())
