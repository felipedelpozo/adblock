#!/usr/bin/env python3
"""Serve the firmware dashboard with synthetic documentation data on localhost.

This preview never contacts a device. Its read-only API is for screenshots,
not firmware/OTA testing; all modifying requests are rejected.
"""
import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]
CLIENTS = [
    {"ip": "192.168.1.20", "mac": "02:00:00:00:00:20", "blocked": 2184,
     "allowed": 8736, "banned": False},
    {"ip": "192.168.1.30", "mac": "02:00:00:00:00:30", "blocked": 936,
     "allowed": 3744, "banned": False},
]
STATS = {
    "ip": "192.168.1.50", "blocked": 3120, "allowed": 12480, "domains": 227420,
    "wifiSsid": "Example Wi-Fi", "wifiSetupAp": "C3-AdBlock-1234", "language": "es",
    "rssi": -58, "temp": 42.7, "heap": 219136, "uptime": "0d 2h 14m",
    "upurl": "", "upiv": 24, "upstat": "Nunca comprobado", "blocking": True,
    "fwVersion": "0.2.4", "fwProfile": "jc3636w518c",
    # The local feature build is ahead of the public release. Keep the
    # screenshot honest: there is no synthetic newer release to install.
    "githubStatus": "Build local instalada: 0.2.4 · última release pública: 0.2.1", "githubVersion": "",
    "githubBusy": False, "githubCanInstall": False, "githubProgress": 0,
    "githubNonce": "documentation-preview-not-a-device-token", "resumeIn": 0,
    "clients": CLIENTS, "custom": [],
}
LISTS = {
    "language": "es",
    "selectedProfile": "balanced", "appliedProfile": "balanced", "busy": False,
    "status": "Lista verificada", "progress": 100,
    "nonce": "documentation-preview-not-a-device-token",
    "allowed": ["updates.example.com", "telemetry.example.net"], "domains": 227420,
}
ENTRIES = [
    {"domain": domain, "client": CLIENTS[index % 2]["ip"], "type": 1,
     "reason": "blocklist", "ageSeconds": 4 + index * 12}
    for index, domain in enumerate([
        "doubleclick.net", "googleadservices.com", "ads.example.com",
        "analytics.example.com", "test.doubleclick.net", "tracking.example.com",
    ])
]


class PreviewHandler(BaseHTTPRequestHandler):
    def reply(self, data, content_type="application/json", code=200):
        payload = data.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type + "; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path == "/":
            source = (ROOT / "src/page.h").read_text()
            page = source.split('R"HTML(', 1)[1].split(')HTML";', 1)[0]
            self.reply(page, "text/html")
        elif url.path == "/stats.json":
            self.reply(json.dumps(STATS))
        elif url.path == "/lists.json":
            self.reply(json.dumps(LISTS))
        elif url.path == "/blocked.json":
            query = parse_qs(url.query)
            try:
                offset = max(0, int(query.get("offset", ["0"])[0]))
                limit = max(1, min(16, int(query.get("limit", ["16"])[0])))
            except ValueError:
                self.reply("Invalid pagination", "text/plain", 400)
                return
            term = query.get("q", [""])[0].lower()[:63]
            filtered = [entry for entry in ENTRIES
                        if term in entry["domain"].lower() or term in entry["client"]]
            self.reply(json.dumps({
                "entries": filtered[offset:offset + limit], "count": len(filtered),
                "total": len(ENTRIES), "offset": offset,
                "more": offset + limit < len(filtered),
            }))
        else:
            self.reply("Read-only documentation preview", "text/plain", 404)

    def do_POST(self):
        self.reply("Documentation preview: device actions are unavailable", "text/plain", 405)

    def log_message(self, *_args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=39417)
    parser.add_argument("--language", choices=("es", "en"), default="es")
    args = parser.parse_args()
    STATS["language"] = LISTS["language"] = args.language
    if args.language == "en":
        STATS["githubStatus"] = "Installed local build: 0.2.4 · latest public release: 0.2.1"
        STATS["upstat"] = "Never checked"
        LISTS["status"] = "List verified"
    server = ThreadingHTTPServer(("127.0.0.1", args.port), PreviewHandler)
    print(f"Read-only dashboard preview: http://127.0.0.1:{server.server_port}/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
