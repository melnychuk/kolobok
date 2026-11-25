# Install Kolobok — instructions for a coding agent

You are helping a person set up **Kolobok** on an M5Stack StickS3. It reads
status from Herdr, so there’s no requirement to use a specific coding agent or
model provider. The Stick shows when agents are working (blue), finished (green),
or need you (yellow), opens the next task on a key press and, on Linux, takes
push-to-talk dictation with local CPU speech recognition.

The heavy lifting is done by a tested installer script. Your job is to check
the machine, explain the choices, get the person's consent where needed, run
the script with the right options, and verify the result with them.

- Project: <https://github.com/melnychuk/kolobok> (MIT)
- Installer: <https://melnychuk.github.io/kolobok/install.sh>
- Site with a live demo: <https://melnychuk.github.io/kolobok/>

## Ground rules

- **Ask before flashing firmware.** It replaces whatever is on the Stick. Only
  pass `--yes` after the person has explicitly agreed in this conversation.
- **Never run `sudo` yourself.** If USB permissions need a udev rule, show the
  commands the installer printed and ask the person to run them.
- **Ask before enabling dictation** (`--dictation`): it downloads ~632 MiB and
  uses the Stick's microphone while the front key is held. Audio stays on the
  machine; never add cloud speech services.
- Do not stop, restart or reconfigure Herdr, its sessions or running agents.
  The only service you may stop/start is the Stick bridge
  (`stick-s3-herdr.service` on Linux, `io.github.melnychuk.stick-s3` on macOS).
- Do not guess a serial port when several devices are connected; ask, then pass
  `--port`.
- Do not read agent transcripts, prompts or other private files to "check"
  anything. The installer's read-only status check is enough.

## 1. Check the machine

Run these and summarize what you found for the person:

```sh
uname -s                      # Linux or Darwin (macOS); others are unsupported
python3 --version             # needs 3.10+
git --version && curl --version | head -1
herdr --version               # Herdr must be installed (and running for the check)
```

**Linux** also needs:

```sh
systemctl --user --version | head -1            # systemd user services
ls ~/.config/omarchy/plugins/*/herdr_tasks.py    # Herdr task helper (Agents panel plugin)
ls ~/.config/stick-s3/herdr.json 2>/dev/null     # optional: combined CLI + native Herdr config
ls -l /dev/serial/by-id/ 2>/dev/null | grep -i espressif   # is the Stick plugged in?
```

The bridge reads tasks through `herdr_tasks.py`, shipped by the Omarchy Agents
panel plugin. With exactly one match the installer uses it automatically; with
several, ask which one and pass `--helper PATH`. With none, the person can
still register native Herdr instances in `~/.config/stick-s3/herdr.json`
(example: `docs/herdr-instances.example.json` in the repo); otherwise stop and
explain that the plugin is needed.

**macOS** also needs:

```sh
herdr session list --json     # the session attached in Ghostty's quick terminal
ls /dev/cu.usbmodem* 2>/dev/null
```

Ask which session to use if it is not `default` (pass `--session NAME`).
Dictation is Linux-only; on macOS the Stick shows status and the key opens tasks.

If no Stick is found, ask them to connect it with a **data** USB-C cable
(charge-only cables are common) and check again.

## 2. Decide the options with the person

| Question | Option |
|---|---|
| Is the Stick new, or not yet running this firmware? (A factory Stick shows M5Stack's demo, not a black face with eyes.) Flash it? | `--flash` (bridge + firmware) or `--flash-only` |
| Linux: enable hold-to-talk dictation? | `--dictation` |
| More than one serial device? | `--port PATH` |
| Several `herdr_tasks.py` helpers? | `--helper PATH` |
| Dictation popup plugin differs from the helper's plugin folder name? | `--agents-plugin ID` |
| macOS: Herdr session other than `default`? | `--session NAME` |

The prebuilt firmware is built by CI from the latest source and its files are
checksum-verified before flashing. If the person agreed to flash, add `--yes`
(your shell usually has no terminal for the script's own prompt).

## 3. Run the installer

Download it first so you can show what will run, then execute it with the
chosen options, for example:

```sh
curl -fsSL https://melnychuk.github.io/kolobok/install.sh -o /tmp/stick-s3-install.sh
bash /tmp/stick-s3-install.sh --help
bash /tmp/stick-s3-install.sh --dictation          # Linux, bridge + dictation
bash /tmp/stick-s3-install.sh --flash --yes        # after consent: bridge + firmware
```

What it does: clones the project to `~/.local/share/stick-s3/app` (or updates
it), creates a Python virtual environment there, runs a read-only Herdr status
check, then installs and starts the bridge as a user service. Re-running it is
safe and updates the install. It never uses sudo.

While flashing, the Stick reboots; tell the person not to unplug it. If
flashing cannot start, ask them to hold the **side power key ~2 seconds** until
the green LED blinks, then run the same command again.

## 4. USB permissions (Linux)

If the installer printed "You cannot open /dev/ttyACM…", show the person the
three commands it printed (a udev rule for **this** Stick's serial number, a
udev reload, and a bridge restart) and ask them to run them in their own
terminal. Do not make the port world-writable.

## 5. Verify with the person

```sh
# Linux
systemctl --user status stick-s3-herdr.service --no-pager
journalctl --user -u stick-s3-herdr.service -n 20 --no-pager   # expect "Stick connected"
# macOS
tail -n 20 ~/Library/Logs/stick-s3/bridge-error.log ~/Library/Logs/stick-s3/bridge.log
```

Then ask them to look at the Stick:

1. The face is awake; with work running, blue numerals (Roman, e.g. `II`) appear
   on the left. Finished-but-unread tasks show green on the right.
2. A short press of the front key opens the next finished or waiting task.
3. Linux with dictation: holding the key opens the Agents dictation popup and
   the eyes turn into a soundwave; releasing leaves editable text. Nothing is
   submitted until they press Enter.

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| "No StickS3 found" | Charge-only cable or wrong port; reconnect with a data cable, or pass `--port`. |
| "Several serial devices found" | Ask which one is the Stick; pass `--port`. |
| "No Herdr task helper found" | Install the Omarchy Agents panel plugin, or pass `--helper`, or configure `~/.config/stick-s3/herdr.json`. |
| Status check failed | Herdr is not running (or the macOS session name is wrong). Start Herdr; re-run. |
| Face shows offline | Bridge not running or no permission; check the service log and step 4. |
| Hold-to-talk does nothing | Dictation not enabled (`--dictation`), or the plugin id is wrong (`--agents-plugin`). |
| Flashing won't start | Hold the side power key ~2s until the green LED blinks, then retry. |

To remove the bridge service: `bash /tmp/stick-s3-install.sh --uninstall`
(downloads in `~/.local/share/stick-s3` are kept; delete that folder to remove
everything).

For manual setup instead, see `docs/linux-herdr.md` and `docs/macos-herdr.md`
in the repository.
