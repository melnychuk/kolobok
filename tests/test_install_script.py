"""site/install.sh in a sandbox: fake HOME, systemctl, launchctl, herdr and
Python environments; firmware comes from a file:// fixture. No network,
real service, serial device or flashing is touched."""
import hashlib
import json
import plistlib
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "site/install.sh"

FAKE_PYTHON = r"""#!/bin/bash
# Fake python: "-m venv DIR" creates a fake env; "-c" version checks pass.
if [[ $1 == -c ]]; then exit 0; fi
if [[ $1 == -m && $2 == venv ]]; then
  mkdir -p "$3/bin"
  cp "$0" "$3/bin/python"
  exit 0
fi
printf '%s\n' "$*" >> "$TEST_LOG/python.log"
if [[ $1 == -m && $2 == pip ]]; then exit 0; fi
if [[ $1 == -m && $2 == esptool ]]; then exit "${TEST_ESPTOOL_EXIT:-0}"; fi
if [[ $1 == - ]]; then exec "$TEST_REAL_PYTHON" "$@"; fi
# bridge scripts: the --once preflight
exit "${TEST_ONCE_EXIT:-0}"
"""


def write(path, text, mode=0o755):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(mode)


class InstallerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.home = base / "home"
        self.home.mkdir()
        self.logs = base / "logs"
        self.logs.mkdir()
        self.bin = base / "bin"
        write(self.bin / "python3", FAKE_PYTHON)
        for tool in ("launchctl", "herdr", "udevadm"):
            write(self.bin / tool, f'#!/bin/sh\nprintf "%s\\n" "$*" >> "$TEST_LOG/{tool}.log"\n')
        # is-active reports inactive unless a test sets TEST_ACTIVE.
        write(self.bin / "systemctl", '#!/bin/sh\nprintf "%s\\n" "$*" >> "$TEST_LOG/systemctl.log"\n'
              'case "$*" in *is-active*) [ -n "$TEST_ACTIVE" ] || exit 3;; esac\n')
        self.serial = base / "by-id"
        self.serial.mkdir()
        self.source = base / "checkout with spaces"
        (self.source / "bridge").mkdir(parents=True)
        for name in ("herdr_bridge.py", "combined_herdr.py", "macos_herdr_bridge.py"):
            (self.source / "bridge" / name).touch()
        self.helper = self.home / ".config/omarchy/plugins/acme.agents/herdr_tasks.py"
        write(self.helper, "", 0o644)
        self.env = {"HOME": str(self.home), "PATH": f"{self.bin}:/usr/bin:/bin",
                    "TEST_LOG": str(self.logs), "TEST_REAL_PYTHON": shutil.which("python3"),
                    "STICK_S3_OS": "Linux", "STICK_S3_SERIAL_DIR": str(self.serial)}

    def run_installer(self, *args, **env):
        return subprocess.run(["bash", str(INSTALLER), "--source", str(self.source), *args],
                              env={**self.env, **env}, capture_output=True, text=True, timeout=30)

    def log(self, tool):
        path = self.logs / f"{tool}.log"
        return path.read_text().splitlines() if path.exists() else []

    @property
    def unit(self):
        return self.home / ".config/systemd/user/stick-s3-herdr.service"

    def test_linux_detects_helper_and_installs_unit(self):
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        unit = self.unit.read_text()
        self.assertIn(f'Environment="STICK_S3_HERDR_HELPER={self.helper}"', unit)
        self.assertIn('Environment="STICK_S3_AGENTS_PLUGIN=acme.agents"', unit)
        self.assertIn(f'ExecStart="{self.source}/.venv/bin/python" "{self.source}/bridge/herdr_bridge.py"\n', unit)
        self.assertIn("WantedBy=default.target", unit)
        self.assertEqual(self.log("systemctl"), [
            "--user daemon-reload", "--user enable stick-s3-herdr.service",
            "--user restart stick-s3-herdr.service"])
        pip = [line for line in self.log("python") if line.startswith("-m pip")]
        self.assertEqual(len(pip), 1)
        self.assertIn("numpy>=1.24", pip[0])
        self.assertNotIn("sherpa", pip[0])
        self.assertIn("Plug in the Stick", result.stdout)

    def test_combined_config_and_unit_escaping(self):
        (self.home / ".config/stick-s3").mkdir(parents=True)
        (self.home / ".config/stick-s3/herdr.json").write_text("{}")
        odd = self.home / "plugins 100% $odd"
        write(odd / "herdr_tasks.py", "", 0o644)
        result = self.run_installer("--helper", str(odd / "herdr_tasks.py"), "--port", "/dev/null")
        self.assertEqual(result.returncode, 0, result.stderr)
        unit = self.unit.read_text()
        self.assertIn("100%% $$odd/herdr_tasks.py", unit)
        self.assertIn('"--helper" "', unit)
        self.assertIn('/bridge/combined_herdr.py" "--port" "/dev/null"', unit)

    def test_missing_helper_fails_before_anything(self):
        self.helper.unlink()
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No Herdr task helper found", result.stderr)
        self.assertFalse(self.unit.exists())
        self.assertEqual(self.log("systemctl"), [])

    def test_preflight_failure_still_installs_with_warning(self):
        result = self.run_installer(TEST_ONCE_EXIT="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("status check failed", result.stderr)
        self.assertTrue(self.unit.exists())

    def test_existing_unit_is_backed_up(self):
        self.unit.parent.mkdir(parents=True)
        self.unit.write_text("old\n")
        self.assertEqual(self.run_installer().returncode, 0)
        backups = list(self.unit.parent.glob("stick-s3-herdr.service.bak.*"))
        self.assertEqual([b.read_text() for b in backups], ["old\n"])

    def test_uninstall(self):
        self.assertEqual(self.run_installer().returncode, 0)
        result = self.run_installer("--uninstall")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.unit.exists())
        self.assertIn("--user disable --now stick-s3-herdr.service", self.log("systemctl"))

    def test_macos_launch_agent(self):
        result = self.run_installer("--session", "work", STICK_S3_OS="Darwin")
        self.assertEqual(result.returncode, 0, result.stderr)
        plist = self.home / "Library/LaunchAgents/io.github.melnychuk.stick-s3.plist"

        config = plistlib.loads(plist.read_bytes())
        self.assertEqual(config["ProgramArguments"][1:], [
            f"{self.source}/bridge/macos_herdr_bridge.py", "--herdr", str(self.bin / "herdr"),
            "--session", "work"])
        self.assertTrue(config["KeepAlive"])
        self.assertTrue(any(line.startswith("bootstrap gui/") for line in self.log("launchctl")))
        self.assertFalse(self.unit.exists())

    def firmware_fixture(self, corrupt=False):
        folder = Path(self.temp.name) / "site/firmware"
        folder.mkdir(parents=True)
        files = []
        for offset, name in (("0x0", "bootloader.bin"), ("0x8000", "partitions.bin"),
                             ("0xe000", "boot_app0.bin"), ("0x10000", "firmware.bin")):
            data = name.encode() * 10
            (folder / name).write_bytes(data)
            digest = hashlib.sha256(b"x" if corrupt and name == "firmware.bin" else data).hexdigest()
            files.append({"offset": offset, "name": name, "sha256": digest})
        (folder / "manifest.json").write_text(json.dumps(
            {"chip": "esp32s3", "commit": "abc1234", "files": files}))
        return f"file://{folder.parent}"

    def test_flash_only_verifies_and_flashes_with_confirmation_flag(self):
        port = Path(self.temp.name) / "ttyFAKE"
        port.touch()
        result = self.run_installer("--flash-only", "--yes", "--port", str(port),
                                    STICK_S3_BASE_URL=self.firmware_fixture())
        self.assertEqual(result.returncode, 0, result.stderr)
        esptool = [line for line in self.log("python") if line.startswith("-m esptool")]
        self.assertEqual(len(esptool), 1)
        args = esptool[0].split()
        self.assertIn(str(port), args)
        self.assertIn("write-flash", args)
        self.assertEqual([args[i] for i in range(len(args)) if args[i].startswith("0x")],
                         ["0x0", "0x8000", "0xe000", "0x10000"])
        self.assertTrue(any("esptool==" in line for line in self.log("python")))
        self.assertFalse(self.unit.exists())  # --flash-only installs no bridge
        # The bridge was not running, so it is neither stopped nor started.
        self.assertNotIn("--user stop stick-s3-herdr.service", self.log("systemctl"))

    def test_flash_pauses_and_resumes_running_bridge(self):
        port = Path(self.temp.name) / "ttyFAKE"
        port.touch()
        result = self.run_installer("--flash-only", "--yes", "--port", str(port),
                                    STICK_S3_BASE_URL=self.firmware_fixture(), TEST_ACTIVE="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.log("systemctl")
        self.assertLess(calls.index("--user stop stick-s3-herdr.service"),
                        calls.index("--user start stick-s3-herdr.service"))
        # Failed flashing still restarts the bridge.
        (self.logs / "systemctl.log").unlink()
        result = self.run_installer("--flash-only", "--yes", "--port", str(port),
                                    STICK_S3_BASE_URL=self.firmware_fixture_again(),
                                    TEST_ACTIVE="1", TEST_ESPTOOL_EXIT="2")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--user start stick-s3-herdr.service", self.log("systemctl"))

    def firmware_fixture_again(self):
        return f"file://{Path(self.temp.name) / 'site'}"

    def test_flash_rejects_bad_checksum(self):
        port = Path(self.temp.name) / "ttyFAKE"
        port.touch()
        result = self.run_installer("--flash-only", "--yes", "--port", str(port),
                                    STICK_S3_BASE_URL=self.firmware_fixture(corrupt=True))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checksum mismatch", result.stderr)
        self.assertFalse(any(line.startswith("-m esptool") for line in self.log("python")))

    def test_flash_needs_confirmation_without_tty(self):
        port = Path(self.temp.name) / "ttyFAKE"
        port.touch()
        result = subprocess.run(["setsid", "bash", str(INSTALLER), "--source", str(self.source),
                                 "--flash-only", "--port", str(port)],
                                env={**self.env, "STICK_S3_BASE_URL": self.firmware_fixture()},
                                capture_output=True, text=True, timeout=30, stdin=subprocess.DEVNULL)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--yes", result.stderr)
        self.assertFalse(any(line.startswith("-m esptool") for line in self.log("python")))

    def test_help_and_unknown_option(self):
        result = self.run_installer("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("--flash", result.stdout)
        result = self.run_installer("--bogus")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unknown option", result.stderr)


class AgentGuideTest(unittest.TestCase):
    """site/install.md must only tell agents to use options the script has."""

    def test_guide_options_exist(self):
        guide = (ROOT / "site/install.md").read_text()
        usage = subprocess.run(["bash", str(INSTALLER), "--help"], capture_output=True,
                               text=True, timeout=10).stdout
        options = set(re.findall(r"(?<![\w-])--[a-z][a-z-]+", guide))
        known_other = {"--version", "--user", "--no-pager", "--json"}  # other tools' flags
        missing = sorted(o for o in options - known_other if o not in usage)
        self.assertEqual(missing, [])
        for required in ("--flash", "--yes", "--dictation", "--uninstall"):
            self.assertIn(required, guide)

    def test_site_prompt_points_at_the_guide(self):
        page = (ROOT / "site/index.html").read_text()
        self.assertGreaterEqual(page.count("https://melnychuk.github.io/kolobok/install.md"), 2)


if __name__ == "__main__":
    unittest.main()
