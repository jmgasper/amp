#!/usr/bin/env python3
"""A tiny stand-in for a Music Assistant server, enough to exercise Amp's integration:
login, library commands, playlists, player queue commands and a Sendspin /sendspin proxy that
streams a synthesised tone as PCM chunks in the unencrypted transition-mode protocol that
aiosendspin 9.x accepts. Not a faithful reimplementation; only the shapes Amp relies on.

    python3 tools/fake-ma-server.py [port]      (default 8096; user: demo, password: demo)
"""
import base64
import hashlib
import json
import math
import socket
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8096
TOKEN = "fake-token-123"
USER = {"user_id": "u1", "username": "demo", "role": "admin"}

ARTISTS = [("Aurora Fields", "1"), ("Bay Street Quartet", "2")]
ALBUMS = [("Northern Lights", "Aurora Fields", "1", 2016), ("Late Ferry", "Bay Street Quartet", "2", 2020)]
TRACKS = []
for album_index, (album, artist, artist_id, year) in enumerate(ALBUMS):
    for n in range(1, 5):
        item_id = str(album_index * 10 + n)
        TRACKS.append({
            "item_id": item_id, "provider": "library", "name": "%s %d" % (album.split()[0], n),
            "uri": "library://track/" + item_id, "media_type": "track", "duration": 12 + n,
            "track_number": n, "disc_number": 1,
            "artists": [{"item_id": artist_id, "provider": "library", "name": artist, "uri": "library://artist/" + artist_id}],
            "album": {"item_id": str(album_index + 1), "provider": "library", "name": album, "uri": "library://album/%d" % (album_index + 1),
                      "year": year, "artists": [{"name": artist, "uri": "library://artist/" + artist_id}]},
            "metadata": {"images": [{"type": "thumb", "path": "cover%d" % (album_index + 1), "provider": "library",
                                     "remotely_accessible": False, "proxy_id": "%064x" % (album_index + 1)}], "genres": ["Demo"]},
        })
PLAYLISTS = {"7": {"item_id": "7", "provider": "library", "name": "Server Favourites", "uri": "library://playlist/7",
                   "media_type": "playlist", "is_editable": True, "tracks": ["1", "12"]}}
NEXT_PLAYLIST = [8]
QUEUES = {}      # player_id -> {"state", "uri", "elapsed", "started"}
PLAYERS = {}     # client_id -> websocket sender
LOG = []


def log(text):
    print(time.strftime("%H:%M:%S"), text, flush=True)
    LOG.append(text)


def png_cover(index):
    # 2x2 PNG with a distinct colour so the image proxy returns a decodable picture
    import zlib
    colour = [(200, 60, 60), (60, 120, 200)][index % 2]
    raw = b"".join(b"\x00" + bytes(colour) * 2 for _ in range(2))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, fmt, *args):
        pass

    def send_json(self, status, payload):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def authed(self):
        return self.headers.get("Authorization", "") == "Bearer " + TOKEN

    def do_GET(self):
        if self.path == "/info":
            return self.send_json(200, {"server_id": "fake", "server_version": "2.10.2-fake", "schema_version": 65, "base_url": "http://localhost:%d" % PORT})
        if self.path.startswith("/imageproxy/"):
            if not self.authed():
                log("imageproxy without token (allowed)")
            index = int(self.path.split("/")[2].split("?")[0], 16)
            body = png_cover(index)
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path == "/sendspin":
            return SendspinSession(self).run()
        self.send_json(404, {"error": "not found"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        if self.path == "/auth/login":
            creds = body.get("credentials", {})
            log("login %s" % creds.get("username"))
            if creds.get("username") == "demo" and creds.get("password") == "demo":
                return self.send_json(200, {"success": True, "token": TOKEN, "user": USER})
            return self.send_json(200, {"success": False, "error": "Invalid username or password"})
        if self.path == "/api":
            if not self.authed():
                self.send_response(401)
                self.end_headers()
                self.wfile.write(b"Authentication required")
                return
            command = body.get("command")
            args = body.get("args", {}) or {}
            log("api %s %s" % (command, json.dumps(args)[:120]))
            return self.send_json(200, self.run_command(command, args))
        self.send_json(404, {"error": "not found"})

    def run_command(self, command, args):
        offset = int(args.get("offset", 0))
        limit = int(args.get("limit", 500))
        if command == "auth/me":
            return USER
        if command == "music/artists/library_items":
            items = [{"item_id": i, "provider": "library", "name": n, "uri": "library://artist/" + i, "media_type": "artist",
                      "metadata": {"images": []}} for n, i in ARTISTS]
            return items[offset:offset + limit]
        if command == "music/albums/library_items":
            items = [{"item_id": str(k + 1), "provider": "library", "name": a, "uri": "library://album/%d" % (k + 1), "year": y,
                      "media_type": "album", "artists": [{"name": ar, "uri": "library://artist/" + ai}],
                      "metadata": {"images": [{"type": "thumb", "path": "cover", "provider": "library", "remotely_accessible": False,
                                               "proxy_id": "%064x" % (k + 1)}]}} for k, (a, ar, ai, y) in enumerate(ALBUMS)]
            return items[offset:offset + limit]
        if command == "music/tracks/library_items":
            return TRACKS[offset:offset + limit]
        if command == "music/playlists/library_items":
            items = [{k: v for k, v in p.items() if k != "tracks"} for p in PLAYLISTS.values()]
            return items[offset:offset + limit]
        if command == "music/playlists/playlist_tracks":
            playlist = PLAYLISTS.get(str(args.get("item_id")))
            if not playlist:
                return []
            return [t for t in TRACKS if t["item_id"] in playlist["tracks"]]
        if command == "music/playlists/create_playlist":
            item_id = str(NEXT_PLAYLIST[0])
            NEXT_PLAYLIST[0] += 1
            PLAYLISTS[item_id] = {"item_id": item_id, "provider": "library", "name": args.get("name", "New"), "uri": "library://playlist/" + item_id,
                                  "media_type": "playlist", "is_editable": True, "tracks": []}
            return {k: v for k, v in PLAYLISTS[item_id].items() if k != "tracks"}
        if command == "music/playlists/add_playlist_tracks":
            playlist = PLAYLISTS.get(str(args.get("db_playlist_id")))
            if playlist:
                for uri in args.get("uris", []):
                    playlist["tracks"].append(uri.rsplit("/", 1)[-1])
            return {"name": "add", "status": "finished"}
        if command == "music/playlists/remove_playlist_tracks":
            playlist = PLAYLISTS.get(str(args.get("db_playlist_id")))
            if playlist:
                for position in sorted(args.get("positions_to_remove", []), reverse=True):
                    if 0 <= position < len(playlist["tracks"]):
                        del playlist["tracks"][position]
            return {"name": "remove", "status": "finished"}
        if command == "music/library/remove_item":
            PLAYLISTS.pop(str(args.get("library_item_id")), None)
            return None
        if command == "players/get":
            player_id = args.get("player_id")
            if player_id in PLAYERS:
                return {"player_id": player_id, "name": "Amp", "available": True, "playback_state": QUEUES.get(player_id, {}).get("state", "idle")}
            return None
        if command == "player_queues/play_media":
            player_id = args.get("queue_id")
            media = args.get("media", [])
            uri = media[0] if media else ""
            track = next((t for t in TRACKS if t["uri"] == uri), None)
            sender = PLAYERS.get(player_id)
            if not sender or not track:
                return {"error": "unknown player or track"}
            QUEUES[player_id] = {"state": "playing", "uri": uri, "elapsed": 0.0, "started": time.time(), "duration": track["duration"], "name": track["name"]}
            sender.start_stream(track["duration"], 220.0 + 40 * int(track["item_id"]))
            return None
        if command.startswith("players/cmd/"):
            player_id = args.get("player_id")
            queue = QUEUES.get(player_id)
            sender = PLAYERS.get(player_id)
            action = command.split("/")[-1]
            if queue and sender:
                if action == "pause":
                    queue["elapsed"] += time.time() - queue["started"]
                    queue["state"] = "paused"
                    sender.pause_stream()
                elif action == "play":
                    queue["started"] = time.time()
                    queue["state"] = "playing"
                    sender.resume_stream()
                elif action == "stop":
                    queue["state"] = "idle"
                    sender.stop_stream()
                elif action == "seek":
                    queue["elapsed"] = float(args.get("position", 0))
                    queue["started"] = time.time()
                    sender.seek_stream(queue["elapsed"])
            return None
        if command == "player_queues/get":
            queue = QUEUES.get(args.get("queue_id"))
            if not queue:
                return None
            elapsed = queue["elapsed"] + (time.time() - queue["started"] if queue["state"] == "playing" else 0)
            return {"queue_id": args.get("queue_id"), "state": queue["state"], "elapsed_time": elapsed,
                    "current_item": {"name": queue["name"], "duration": queue["duration"], "media_item": {"uri": queue["uri"]}}}
        return {"error": "unknown command " + str(command)}


class SendspinSession:
    """Serves one /sendspin WebSocket: proxy auth, legacy hello, time sync and PCM streaming."""

    def __init__(self, handler):
        self.handler = handler
        self.sock = handler.connection
        self.lock = threading.Lock()
        self.client_id = None
        self.streaming = False
        self.paused = False
        self.stop_event = threading.Event()

    def send_frame(self, opcode, payload):
        header = bytes([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header += bytes([n])
        elif n < 65536:
            header += bytes([126]) + struct.pack(">H", n)
        else:
            header += bytes([127]) + struct.pack(">Q", n)
        with self.lock:
            self.sock.sendall(header + payload)

    def send_json(self, payload):
        self.send_frame(0x1, json.dumps(payload).encode())

    def read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("closed")
            data += chunk
        return data

    def read_frame(self):
        b0, b1 = self.read_exact(2)
        opcode = b0 & 0x0F
        masked = b1 & 0x80
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack(">H", self.read_exact(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", self.read_exact(8))[0]
        mask = self.read_exact(4) if masked else b"\0\0\0\0"
        payload = bytearray(self.read_exact(n))
        for i in range(n):
            payload[i] ^= mask[i & 3]
        return opcode, bytes(payload)

    def run(self):
        h = self.handler
        key = h.headers.get("Sec-WebSocket-Key", "")
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        h.send_response(101, "Switching Protocols")
        h.send_header("Upgrade", "websocket")
        h.send_header("Connection", "Upgrade")
        h.send_header("Sec-WebSocket-Accept", accept)
        h.end_headers()
        h.close_connection = True
        try:
            opcode, payload = self.read_frame()
            auth = json.loads(payload)
            if auth.get("type") != "auth" or auth.get("token") != TOKEN:
                log("sendspin auth rejected: %s" % payload[:80])
                self.send_frame(0x8, struct.pack(">H", 4001) + b"Invalid or expired token")
                return
            self.send_json({"type": "auth_ok"})
            opcode, payload = self.read_frame()
            hello = json.loads(payload)
            if hello.get("type") != "client/hello":
                log("expected client/hello, got %s" % hello.get("type"))
                return
            info = hello["payload"]
            self.client_id = info.get("client_id")
            formats = info["player@v1_support"]["supported_formats"]
            self.format = formats[0]
            log("sendspin hello from %s (%s) formats=%s buffer=%s" % (self.client_id, info.get("name"),
                ["%s/%s" % (f["codec"], f["sample_rate"]) for f in formats], info["player@v1_support"]["buffer_capacity"]))
            self.send_json({"type": "server/hello", "payload": {"server_id": "fake-server", "name": "Fake MA", "version": 1,
                                                                 "connection_reason": "playback", "active_roles": ["player@v1"]}})
            self.send_json({"type": "group/update", "payload": {"playback_state": "stopped", "group_id": self.client_id, "group_name": "Amp"}})
            PLAYERS[self.client_id] = self
            while True:
                opcode, payload = self.read_frame()
                if opcode == 0x8:
                    break
                if opcode == 0x9:
                    self.send_frame(0xA, payload)
                    continue
                if opcode != 0x1:
                    continue
                message = json.loads(payload)
                kind = message.get("type")
                if kind == "client/time":
                    now = int(time.monotonic() * 1e6)
                    self.send_json({"type": "server/time", "payload": {"client_transmitted": message["payload"]["client_transmitted"],
                                                                         "server_received": now, "server_transmitted": now + 5}})
                elif kind == "client/state":
                    log("client/state %s" % json.dumps(message["payload"])[:160])
                else:
                    log("client message %s" % kind)
        except (ConnectionError, OSError, ValueError) as exc:
            log("sendspin session ended: %s" % exc)
        finally:
            self.stop_event.set()
            PLAYERS.pop(self.client_id, None)

    # ---- streaming ---------------------------------------------------------
    def start_stream(self, seconds, frequency):
        self.stop_stream()
        self.stop_event = threading.Event()
        self.paused = False
        self.position = 0.0
        self.total = seconds
        self.frequency = frequency
        self.send_json({"type": "stream/start", "payload": {"server_transmitted": int(time.monotonic() * 1e6),
                        "player": {"codec": "pcm", "sample_rate": self.format["sample_rate"], "channels": 2, "bit_depth": 16}}})
        threading.Thread(target=self.stream_loop, daemon=True).start()

    def pause_stream(self):
        self.paused = True

    def resume_stream(self):
        self.paused = False
        self.send_json({"type": "stream/clear", "payload": {"server_transmitted": int(time.monotonic() * 1e6), "roles": ["player"]}})

    def seek_stream(self, position):
        self.position = position
        self.send_json({"type": "stream/clear", "payload": {"server_transmitted": int(time.monotonic() * 1e6), "roles": ["player"]}})

    def stop_stream(self):
        if self.streaming:
            self.stop_event.set()
            self.streaming = False
            self.send_json({"type": "stream/end", "payload": {"server_transmitted": int(time.monotonic() * 1e6), "roles": ["player"]}})

    def stream_loop(self):
        self.streaming = True
        stop_event = self.stop_event  # this stream's own event; a newer stream replaces the attribute
        rate = self.format["sample_rate"]
        chunk_ms = 25
        frames = rate * chunk_ms // 1000
        lead = 0.6  # seconds of send-ahead
        next_ts = time.monotonic() + lead
        sample = 0
        while not stop_event.is_set() and self.position < self.total:
            if self.paused:
                time.sleep(0.05)
                next_ts = time.monotonic() + lead
                continue
            pcm = bytearray()
            for i in range(frames):
                value = int(8000 * math.sin(2 * math.pi * self.frequency * (sample + i) / rate))
                pcm += struct.pack("<hh", value, value)
            sample += frames
            header = struct.pack(">Bq", 4, int(next_ts * 1e6))
            try:
                self.send_frame(0x2, bytes(header) + bytes(pcm))
            except OSError:
                break
            next_ts += chunk_ms / 1000.0
            self.position += chunk_ms / 1000.0
            # pace the sender so it stays roughly `lead` seconds ahead of real time
            ahead = next_ts - time.monotonic()
            if ahead > lead:
                time.sleep(ahead - lead)
        if not stop_event.is_set():
            time.sleep(lead)
            if stop_event.is_set():
                return
            self.streaming = False
            queue = QUEUES.get(self.client_id)
            if queue:
                queue["state"] = "idle"
            try:
                self.send_json({"type": "stream/end", "payload": {"server_transmitted": int(time.monotonic() * 1e6), "roles": ["player"]}})
            except OSError:
                pass
            log("stream finished")


if __name__ == "__main__":
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    log("fake Music Assistant listening on %d" % PORT)
    server.serve_forever()
