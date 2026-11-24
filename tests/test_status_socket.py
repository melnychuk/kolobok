import json
import os
import socket
import stat
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from bridge import macos_herdr_bridge as mac
from bridge import status_socket
from bridge.herdr_bridge import TaskState

REAL_SLEEP = time.sleep  # The loop's sleep is patched; this lets worker threads finish.


def read_messages(sock, count):
    buf, messages = b"", []
    while len(messages) < count:
        chunk = sock.recv(4096)
        if not chunk:
            break
        buf += chunk
        while b"\n" in buf and len(messages) < count:
            line, buf = buf.split(b"\n", 1)
            messages.append(json.loads(line))
    return messages


class StatusSocketTests(unittest.TestCase):
    def setUp(self):
        # AF_UNIX paths are short; keep the fixture close to the root of the temp dir.
        self.root = Path(tempfile.mkdtemp(prefix="ss", dir="/tmp"))
        self.path = self.root / "s" / "bridge.sock"
        self.clock = Mock(return_value=100.0)

    def tearDown(self):
        for path in sorted(self.root.rglob("*"), reverse=True):
            path.unlink() if not path.is_dir() else path.rmdir()
        self.root.rmdir()

    def server(self, **kwargs):
        server = status_socket.StatusServer(self.path, clock=self.clock, **kwargs)
        self.addCleanup(server.close)
        return server

    def client(self, server):
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(str(self.path))
        sock.settimeout(2)
        self.addCleanup(sock.close)
        server.poll()
        return sock

    def test_socket_and_directory_are_private_to_the_user(self):
        self.server()
        self.assertEqual(stat.S_IMODE(self.path.lstat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.path.parent.lstat().st_mode), 0o700)

    def test_refuses_a_shared_directory(self):
        self.path.parent.mkdir(mode=0o700)
        # Report the directory as group-readable without loosening a real one.
        shared = Mock(st_mode=stat.S_IFDIR | 0o750, st_uid=os.getuid())
        with patch.object(Path, "lstat", return_value=shared), \
                self.assertRaises(status_socket.Unavailable):
            status_socket.StatusServer(self.path)

    def test_stale_socket_is_replaced_but_a_live_one_is_not(self):
        leftover = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.path.parent.mkdir(mode=0o700)
        leftover.bind(str(self.path))
        leftover.close()  # Bound but nobody listening: stale.
        server = self.server()
        with self.assertRaises(status_socket.Unavailable):
            status_socket.StatusServer(self.path)  # The first one is live.
        server.close()
        self.assertFalse(self.path.exists())

    def test_refuses_to_replace_a_regular_file(self):
        self.path.parent.mkdir(mode=0o700)
        self.path.write_text("not a socket")
        with self.assertRaises(status_socket.Unavailable):
            status_socket.StatusServer(self.path)

    def test_newcomers_get_the_current_status_and_heartbeats_repeat_it(self):
        server = self.server()
        server.publish({"n": 1})
        sock = self.client(server)
        server.poll()
        self.assertEqual(read_messages(sock, 1), [{"n": 1}])
        server.publish({"n": 1})  # Unchanged within the heartbeat: not resent.
        self.clock.return_value = 106.0
        server.publish({"n": 1})  # Heartbeat.
        server.publish({"n": 2})  # Change: sent at once.
        server.poll()
        self.assertEqual(read_messages(sock, 2), [{"n": 1}, {"n": 2}])

    def test_focus_requests_reach_the_bridge_and_get_a_reply(self):
        calls = []
        server = self.server(on_focus=lambda client, request_id, key: calls.append((client, request_id, key)))
        sock = self.client(server)
        sock.sendall(b'{"id": 3, "focus": "desktop:t1"}\n')
        server.poll()
        self.assertEqual([(request_id, key) for _, request_id, key in calls], [(3, "desktop:t1")])
        server.reply(calls[0][0], {"id": 3, "ok": True})
        server.poll()
        self.assertEqual(read_messages(sock, 1), [{"v": 1, "type": "focus", "id": 3, "ok": True}])

    def test_malformed_or_oversized_requests_drop_the_client(self):
        for request in (b"not json\n", b'{"id": true, "focus": "k"}\n', b'{"id": 1, "focus": ""}\n',
                        b"x" * (status_socket.MAX_REQUEST + 1)):
            on_focus = Mock()
            server = status_socket.StatusServer(self.path, on_focus=on_focus, clock=self.clock)
            sock = self.client(server)
            sock.sendall(request)
            server.poll()
            self.assertEqual(server.clients, [], request[:20])
            on_focus.assert_not_called()
            sock.close()
            server.close()

    def test_client_count_and_backlog_are_bounded(self):
        server = self.server()
        socks = [self.client(server) for _ in range(status_socket.MAX_CLIENTS + 2)]
        self.assertEqual(len(server.clients), status_socket.MAX_CLIENTS)
        self.assertEqual(len(socks), status_socket.MAX_CLIENTS + 2)
        client = server.clients[0]
        server._queue(client, b"x" * (status_socket.MAX_PENDING + 1))
        self.assertNotIn(client, server.clients)

    def test_close_leaves_a_newer_bridges_socket_alone(self):
        server = status_socket.StatusServer(self.path, clock=self.clock)
        self.path.unlink()
        newer = self.server()
        server.close()
        self.assertTrue(self.path.exists())
        newer.close()

    def test_default_path_is_per_user_not_tmpdir(self):
        with patch.dict(os.environ, {"TMPDIR": "/somewhere/else"}):
            path = status_socket.default_path()
        self.assertEqual(path.name, "bridge.sock")
        self.assertNotIn("/somewhere/else", str(path))


def task(status, key, unread=None):
    t = {"key": key, "session": "desktop", "pane": key, "status": status, "completion": 0,
         "focused": False, "viewed": False, "agent": "pi", "title": "T"}
    if unread is not None:
        t["unread"] = unread
    return t


class StatusMessageTests(unittest.TestCase):
    def test_message_matches_what_the_stick_is_told(self):
        state = TaskState("desktop")
        tasks = [task("working", "a"), task("blocked", "b"), task("done", "c", True), task("idle", "d", False)]
        wire = state.update({"tasks": tasks})
        message = mac.status_message(state, True, "desktop")
        self.assertEqual(wire, b"C|2|1|1\n")
        self.assertEqual((message["active"], message["blocked"], message["done"]), (2, 1, 1))
        self.assertEqual([a["unread"] for a in message["agents"]], [False, False, True, False])
        self.assertEqual(set(message["agents"][0]), {"key", "agent", "title", "status", "unread"})

    def test_message_says_where_desktop_agents_are(self):
        state = TaskState("desktop")
        state.update({"tasks": [dict(task("working", "a"), space="website on devbox", tab="2", name="")]})
        agent = mac.status_message(state, True, "desktop")["agents"][0]
        self.assertEqual((agent["space"], agent["tab"], agent["name"]), ("website on devbox", "2", ""))

    def test_offline_message_has_no_agents_or_counts(self):
        state = TaskState("desktop")
        state.update({"tasks": [task("working", "a")]})
        message = mac.status_message(state, False, "desktop")
        self.assertEqual((message["online"], message["active"], message["agents"]), (False, 0, []))


class RunLoopTests(unittest.TestCase):
    def run_loop(self, helper, *, iterations, find_port, server=None, read=b"", select_ready=False):
        ticks = iter(range(iterations))

        def sleep(_):
            if next(ticks, None) is None:
                raise KeyboardInterrupt
            REAL_SLEEP(0.005)

        with patch.object(mac, "find_port", side_effect=find_port), \
                patch.object(mac, "open_serial", return_value=42), \
                patch.object(mac.select, "select", return_value=([42] if select_ready else [], [], [])), \
                patch.object(mac.os, "read", return_value=read), \
                patch.object(mac.os, "write", side_effect=lambda fd, data: len(data)), \
                patch.object(mac.os, "close"), \
                patch.object(mac.time, "sleep", side_effect=sleep), \
                self.assertRaises(KeyboardInterrupt):
            mac.run(helper, server=server)

    def test_ambiguous_ports_keep_publishing_status(self):
        helper = Mock(session="desktop", source="desktop")
        helper.snapshot.return_value = {"tasks": [task("working", "a")]}
        server = Mock()
        self.run_loop(helper, iterations=20, server=server,
                      find_port=RuntimeError("Multiple USB serial devices"))
        published = [c.args[0] for c in server.publish.call_args_list]
        self.assertTrue(any(m["online"] and m["active"] == 1 for m in published))

    def test_losing_the_stick_keeps_unread_state(self):
        helper = Mock(session="desktop", source="desktop")
        helper.snapshot.return_value = {"tasks": [task("done", "a", True)]}
        server = Mock()
        # The Stick disconnects (empty read) right after connecting.
        self.run_loop(helper, iterations=20, server=server, find_port=lambda port: "/dev/fixture",
                      select_ready=True, read=b"")
        last = server.publish.call_args_list[-1].args[0]
        self.assertEqual((last["online"], last["done"]), (True, 1))

    def test_app_focus_requests_share_one_action_with_the_button(self):
        helper = Mock(session="desktop", source="desktop")
        helper.snapshot.return_value = {"tasks": []}
        server = Mock()

        sent = []

        def poll():
            if not sent:
                sent.append(True)
                server.on_focus("c1", 1, "desktop:a")
                server.on_focus("c2", 2, "desktop:b")
        server.poll.side_effect = poll
        self.run_loop(helper, iterations=20, server=server, find_port=lambda port: None)
        helper.focus_key.assert_called_once_with("desktop:a")
        replies = {c.args[0]: c.args[1] for c in server.reply.call_args_list}
        self.assertEqual(replies["c2"], {"id": 2, "ok": False, "error": "busy"})
        self.assertEqual(replies["c1"], {"id": 1, "ok": True})


if __name__ == "__main__":
    unittest.main()
