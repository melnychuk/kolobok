import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INSTALL = ROOT / "scripts/install-herdr-avatar.py"
MOD = "stick-s3-avatar"
AGENTS = {"type": "block", "id": "agents", "content": {"mod": "agents"}}


@unittest.skipUnless(shutil.which("node"), "Node.js is required for the Herdr avatar")
class HerdrAvatarInstallTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.home = Path(temp.name)
        self.layouts = self.home / "layouts"
        self.layouts.mkdir()
        self.original = self.layouts / "main.json"
        self.layout = {"type": "row", "theme": "moss", "children": [
            {"type": "column", "id": "sidebar", "background": "chrome", "children": [
                {"type": "column", "id": "lists", "children": [
                    {"type": "block", "id": "spaces"}, AGENTS,
                ]},
            ]},
            {"type": "block", "id": "work"},
        ]}
        self.original.write_text(json.dumps(self.layout), encoding="utf-8")

    def install(self, *args):
        return subprocess.run([sys.executable, INSTALL, *args], capture_output=True, text=True,
                              env={**os.environ, "HERDR_APP_HOME": str(self.home)}, check=False)

    def test_installs_above_agents_without_touching_original(self):
        original = self.original.read_bytes()
        result = self.install()
        self.assertEqual(result.returncode, 0, result.stderr)
        target = self.layouts / "stick-s3.json"
        data = json.loads(target.read_text(encoding="utf-8"))
        lists = data["children"][0]["children"][0]["children"]
        self.assertEqual(lists[1]["content"], {"mod": MOD})
        self.assertEqual(lists[1]["size"], {"px": 76})
        del lists[1]
        self.assertEqual(data, self.layout)
        self.assertEqual(self.original.read_bytes(), original)
        for path in (ROOT / "herdr" / MOD).rglob("*"):
            if path.is_file():
                dest = self.home / "mods" / MOD / path.relative_to(ROOT / "herdr" / MOD)
                if path.name.endswith(".d.ts") or path.name in {"jsconfig.json", ".DS_Store"}:
                    self.assertFalse(dest.exists())
                else:
                    self.assertEqual(dest.read_bytes(), path.read_bytes())

    def test_refuses_to_overwrite_an_existing_layout_or_mod(self):
        self.assertEqual(self.install().returncode, 0)
        target = self.layouts / "stick-s3.json"
        target.write_text("user layout", encoding="utf-8")
        self.assertNotEqual(self.install().returncode, 0)
        self.assertEqual(target.read_text(encoding="utf-8"), "user layout")
        mod = self.home / "mods" / MOD / "main.js"
        mod.write_text("user mod", encoding="utf-8")
        self.assertNotEqual(self.install("--name", "another").returncode, 0)
        self.assertEqual(mod.read_text(encoding="utf-8"), "user mod")
        self.assertFalse((self.layouts / "another.json").exists())

    def test_incompatible_layout_does_not_install_mod(self):
        self.original.write_text('{"type":"block","id":"root"}', encoding="utf-8")
        result = self.install()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected one Agents block", result.stderr)
        self.assertFalse((self.home / "mods").exists())

    def test_uses_exact_site_wasm(self):
        result = self.install("--usb-serial", "AA:11")
        self.assertEqual(result.returncode, 0, result.stderr)
        mod = self.home / "mods" / MOD
        self.assertEqual((mod / "avatar.wasm").read_bytes(), (ROOT / "site/avatar.wasm").read_bytes())
        self.assertTrue((mod / "renderer.mjs").is_file())
        self.assertTrue((mod / "presence.mjs").is_file())
        self.assertEqual(json.loads((mod / "runtime.json").read_text(encoding="utf-8"))["usbSerial"], "AA:11")

    def test_upgrade_preserves_layout_and_backs_up_prior_widget(self):
        self.assertEqual(self.install().returncode, 0)
        target = self.layouts / "stick-s3.json"
        old = json.loads(target.read_text(encoding="utf-8"))
        old["theme"] = "ink"
        block = old["children"][0]["children"][0]["children"][1]
        block.update(size={"px": 108}, min=108, max=108)
        target.write_text(json.dumps(old), encoding="utf-8")
        destination = self.home / "mods" / MOD
        (destination / "legacy.txt").write_text("old widget", encoding="utf-8")
        result = self.install("--upgrade")
        self.assertEqual(result.returncode, 0, result.stderr)
        backup, = self.home.glob("stick-s3-avatar-backup-*")
        self.assertEqual(json.loads((backup / target.name).read_text(encoding="utf-8")), old)
        self.assertTrue((backup / MOD / "legacy.txt").exists())
        self.assertFalse((destination / "legacy.txt").exists())
        block.update(size={"px": 76}, min=0, max=76)
        self.assertEqual(json.loads(target.read_text(encoding="utf-8")), old)
