// Read USB enumeration only; never open the Stick's serial port.
import { execFile } from "node:child_process";
import { readFile, readdir } from "node:fs/promises";
import { join } from "node:path";
import process from "node:process";
import { promisify } from "node:util";

const exec = promisify(execFile);
const VID = 0x303a;
const PID = 0x1001;

/** @param {string} actual @param {string} wanted */
function matchesSerial(actual, wanted) {
  return !wanted || actual.trim().toLowerCase() === wanted.trim().toLowerCase();
}

/** @param {string} output @param {string} wanted */
export function macPresence(output, wanted) {
  const devices = output.split(/^[ |+\-]*o .+$/m).slice(1);
  return devices.some((device) => Number(device.match(/"idVendor" = (\d+)/)?.[1]) === VID &&
    Number(device.match(/"idProduct" = (\d+)/)?.[1]) === PID &&
    matchesSerial(device.match(/"USB Serial Number" = "([^"\n]*)"/)?.[1] ?? "", wanted));
}

/** @param {string} path @param {string} wanted */
export async function linuxPresence(path, wanted) {
  const names = await readdir(path);
  for (const name of names) {
    const base = join(path, name);
    const get = (/** @type {string} */ name) => readFile(join(base, name), "utf8").catch(() => "");
    if (Number.parseInt(await get("idVendor"), 16) !== VID) continue;
    if (Number.parseInt(await get("idProduct"), 16) !== PID) continue;
    if (!wanted || matchesSerial(await get("serial"), wanted)) return true;
  }
  return false;
}

/** @param {string} wanted @returns {Promise<boolean | null>} null = probe could not tell */
export async function stickPresent(wanted) {
  try {
    if (process.platform === "darwin") {
      const { stdout } = await exec("/usr/sbin/ioreg", ["-p", "IOUSB", "-l", "-w", "0"], { timeout: 800 });
      return macPresence(stdout, wanted);
    }
    if (process.platform === "linux") return await linuxPresence("/sys/bus/usb/devices", wanted);
  } catch { /* Leave the last known state alone. */ }
  return null;
}
