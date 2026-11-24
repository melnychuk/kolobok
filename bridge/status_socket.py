#!/usr/bin/env python3
"""Publish the bridge's status to local apps such as BAA over a Unix socket.

Memory only: binding creates one socket inode in the per-user temp directory
when the bridge starts; every update after that stays in the kernel, so there
are no per-poll file writes. Newline-delimited JSON, version 1:

  bridge -> app: {"v": 1, "type": "status", "online": bool, "source": str,
                  "active": n, "blocked": n, "done": n,
                  "agents": [{"key", "agent", "title", "status", "unread"}]}
  app -> bridge: {"id": n, "focus": key}
  bridge -> app: {"v": 1, "type": "focus", "id": n, "ok": bool, "error": "busy"|"stale"|"failed"|"invalid"}

Only processes running as the same user can reach the socket (0700 directory,
0600 socket). Agent titles go to them, never to logs.
"""
import errno
import json
import os
import socket
import stat
import tempfile
import time
from pathlib import Path

DARWIN_USER_TEMP_DIR = 65537  # _CS_DARWIN_USER_TEMP_DIR; Python's os.confstr has no name for it.
MAX_CLIENTS = 8
MAX_REQUEST = 512            # Bytes per request line.
MAX_PENDING = 256 * 1024     # Unsent bytes per client before it's dropped as stalled.
HEARTBEAT = 5                # Seconds between repeats of an unchanged status.


def default_path():
    """Same path for a LaunchAgent and a GUI app: derived from the user, not $TMPDIR."""
    try:
        base = os.confstr(DARWIN_USER_TEMP_DIR)
    except (ValueError, OSError):
        base = None
    return Path(base or tempfile.gettempdir()) / "stick-s3" / "bridge.sock"


class Unavailable(RuntimeError):
    pass


def parse_request(line):
    """(request id, agent key) from one request line, or None if it isn't one."""
    try:
        request = json.loads(line)
        request_id, key = request["id"], request["focus"]
    except (ValueError, KeyError, TypeError):
        return None
    if (isinstance(request_id, int) and not isinstance(request_id, bool) and 0 <= request_id < 2 ** 31
            and isinstance(key, str) and 0 < len(key) <= 200):
        return request_id, key
    return None


def _private_dir(path):
    path.mkdir(mode=0o700, parents=False, exist_ok=True)
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise Unavailable("Status socket directory is not private")


def _clear_stale(path):
    """Unlink a leftover socket only when it's ours and nobody is listening."""
    try:
        info = path.lstat()
    except FileNotFoundError:
        return
    if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid():
        raise Unavailable("Status socket path is taken")
    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        probe.connect(str(path))
    except ConnectionRefusedError:
        path.unlink()
        return
    except OSError as error:
        raise Unavailable("Status socket path is unusable") from error
    finally:
        probe.close()
    raise Unavailable("Another bridge is already publishing status")


class Client:
    def __init__(self, sock):
        self.sock = sock
        self.inbox = b""
        self.outbox = bytearray()


class StatusServer:
    def __init__(self, path=None, on_focus=None, clock=time.monotonic):
        self.path = Path(path) if path else default_path()
        self.on_focus = on_focus  # (client, request_id, key) -> None; reply later with reply().
        self.clock = clock
        self.clients = []
        self.last_line = None
        self.last_sent = 0.0
        _private_dir(self.path.parent)
        _clear_stale(self.path)
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            listener.bind(str(self.path))
            os.chmod(self.path, 0o600)
            listener.listen(MAX_CLIENTS)
            listener.setblocking(False)
        except OSError:
            listener.close()
            raise
        self.listener = listener
        self.inode = self.path.lstat().st_ino

    # --- Publishing ---------------------------------------------------------

    def publish(self, message):
        """Send a status to every client when it changes, or as a heartbeat."""
        line = json.dumps(message, separators=(",", ":")).encode() + b"\n"
        now = self.clock()
        if line == self.last_line and now - self.last_sent < HEARTBEAT:
            return
        self.last_line, self.last_sent = line, now
        for client in list(self.clients):
            self._queue(client, line)

    def reply(self, client, payload):
        if client in self.clients:
            self._queue(client, json.dumps({"v": 1, "type": "focus", **payload}).encode() + b"\n")

    def _queue(self, client, data):
        client.outbox += data
        if len(client.outbox) > MAX_PENDING:
            self._drop(client)  # A stalled reader must not grow the bridge's memory.

    # --- Event loop ---------------------------------------------------------

    def poll(self):
        """Accept, read and write without blocking; call from the bridge's loop."""
        self._accept()
        for client in list(self.clients):
            self._read(client)
        for client in list(self.clients):
            self._write(client)

    def _accept(self):
        while True:
            try:
                sock, _ = self.listener.accept()
            except (BlockingIOError, InterruptedError):
                return
            except OSError:
                return
            if len(self.clients) >= MAX_CLIENTS:
                sock.close()
                continue
            sock.setblocking(False)
            client = Client(sock)
            self.clients.append(client)
            if self.last_line:
                self._queue(client, self.last_line)  # Newcomers get the current status at once.

    def _read(self, client):
        try:
            chunk = client.sock.recv(4096)
        except (BlockingIOError, InterruptedError):
            return
        except OSError:
            self._drop(client)
            return
        if not chunk:
            self._drop(client)
            return
        client.inbox += chunk
        while b"\n" in client.inbox:
            line, client.inbox = client.inbox.split(b"\n", 1)
            self._request(client, line)
            if client not in self.clients:
                return
        if len(client.inbox) > MAX_REQUEST:
            self._drop(client)

    def _request(self, client, line):
        parsed = parse_request(line)
        if parsed is None:
            self._drop(client)  # Not a BAA; don't keep talking to it.
            return
        request_id, key = parsed
        if self.on_focus is None:
            self.reply(client, {"id": request_id, "ok": False, "error": "failed"})
        else:
            self.on_focus(client, request_id, key)

    def _write(self, client):
        while client.outbox:
            try:
                sent = client.sock.send(client.outbox)
            except (BlockingIOError, InterruptedError):
                return
            except OSError:
                self._drop(client)
                return
            del client.outbox[:sent]

    def _drop(self, client):
        if client in self.clients:
            self.clients.remove(client)
        client.sock.close()

    def close(self):
        for client in list(self.clients):
            self._drop(client)
        self.listener.close()
        try:
            # Only remove our own socket, not one a newer bridge has bound since.
            if self.path.lstat().st_ino == self.inode:
                self.path.unlink()
        except OSError as error:
            if error.errno != errno.ENOENT:
                raise
