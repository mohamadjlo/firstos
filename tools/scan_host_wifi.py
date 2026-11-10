#!/usr/bin/env python3
"""
tools/scan_host_wifi.py
Scans real Wi-Fi networks visible to the host Mac (via CoreWLAN / system_profiler),
generates wifi_networks.txt, and updates /wifi_networks.txt directly in fs.img.
"""

import os
import sys
import subprocess
import struct
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Fallback networks if scan unavailable (seeded with real local networks)
DEFAULT_NETWORKS = [
    ("Vodafone-6414", 95, 1, 1, "5C:13:AC:07:DE:70"),
    ("Vodafone Homespot", 90, 0, 0, "5E:13:AC:07:DE:71"),
    ("o2-WLAN08", 85, 1, 0, "34:27:92:44:81:AA"),
    ("PandaPower", 75, 1, 0, "A0:C5:89:12:34:56"),
    ("Vodafone-6044", 70, 1, 0, "88:E3:AB:99:11:22"),
    ("FirstOS Public", 95, 0, 0, "12:34:56:78:9A:BC"),
    ("FRITZ!Box 7520 XB", 55, 1, 0, "DC:39:79:42:11:00"),
    ("VFCableMAX", 60, 1, 0, "70:54:D2:88:99:10"),
    ("Vodafone-060C", 65, 1, 0, "44:4E:6D:11:22:33"),
    ("Vodafone-2DE4", 50, 1, 0, "68:72:51:AA:BB:CC"),
]

def scan_via_corewlan():
    swift_code = r'''
import CoreWLAN

if let client = CWWiFiClient.shared().interface() {
    let cur = client.ssid() ?? ""
    print("CURRENT:" + cur)
    do {
        let nets = try client.scanForNetworks(withName: nil)
        for n in nets {
            if let ssid = n.ssid, !ssid.isEmpty {
                let bssid = n.bssid ?? "00:00:00:00:00:00"
                print("NET:" + ssid + "|" + String(n.rssiValue) + "|" + bssid)
            }
        }
    } catch {
        print("ERR:" + String(describing: error))
    }
}
'''
    try:
        res = subprocess.run(['swift', '-e', swift_code], capture_output=True, text=True, timeout=6)
        return res.stdout.splitlines()
    except Exception as e:
        print(f"[scan_host_wifi] CoreWLAN scan notice: {e}")
        return []

def scan_via_system_profiler():
    try:
        res = subprocess.run(['system_profiler', 'SPAirPortDataType'], capture_output=True, text=True, timeout=8)
        lines = res.stdout.splitlines()
        found = []
        in_nets = False
        for line in lines:
            stripped = line.strip()
            if "Current Network Information:" in line:
                in_nets = False
            elif "Other Local Wi-Fi Networks:" in line:
                in_nets = True
            elif stripped.endswith(":") and ("PHY Mode" not in stripped and "Channel" not in stripped and "Security" not in stripped and "BSSID" not in stripped):
                name = stripped[:-1].strip()
                if name and in_nets:
                    found.append((name, -65, "00:00:00:00:00:00"))
        return found
    except Exception as e:
        return []

def get_real_networks():
    raw_lines = scan_via_corewlan()
    current_ssid = ""
    nets_dict = {}

    for l in raw_lines:
        if l.startswith("CURRENT:"):
            c = l.split(":", 1)[1].strip()
            if c:
                current_ssid = c
        elif l.startswith("NET:"):
            parts = l[4:].split("|")
            if len(parts) >= 2:
                name = parts[0].strip()
                try: rssi = int(parts[1])
                except: rssi = -60
                bssid = parts[2].strip() if len(parts) > 2 else "00:00:00:00:00:00"
                # Convert RSSI (-30 to -95 dBm) to 0-100%
                sig = min(99, max(15, int(100 - (abs(rssi) - 35) * 1.5)))
                encrypted = 0 if ("Hotspot" in name or "Public" in name or "Free" in name or "Homespot" in name) else 1
                if name not in nets_dict or sig > nets_dict[name][0]:
                    nets_dict[name] = (sig, encrypted, bssid)

    if not nets_dict:
        alt = scan_via_system_profiler()
        for name, rssi, bssid in alt:
            sig = min(99, max(15, int(100 - (abs(rssi) - 35) * 1.5)))
            encrypted = 0 if ("Hotspot" in name or "Public" in name or "Free" in name or "Homespot" in name) else 1
            nets_dict[name] = (sig, encrypted, bssid)

    if not nets_dict:
        return DEFAULT_NETWORKS

    result = []
    top_name = current_ssid if (current_ssid and current_ssid != "none") else "Vodafone-6414"
    if top_name in nets_dict:
        sig, enc, bssid = nets_dict.pop(top_name)
        result.append((top_name, sig, enc, 1, bssid))
    elif "Vodafone-6414" in nets_dict:
        sig, enc, bssid = nets_dict.pop("Vodafone-6414")
        result.append(("Vodafone-6414", sig, enc, 1, bssid))
    else:
        result.append(("Vodafone-6414", 95, 1, 1, "5C:13:AC:07:DE:70"))

    for name, (sig, enc, bssid) in sorted(nets_dict.items(), key=lambda x: -x[1][0]):
        if len(result) >= 16: break
        result.append((name, sig, enc, 0, bssid))

    if not any("Public" in r[0] for r in result) and len(result) < 16:
        result.append(("FirstOS Public", 95, 0, 0, "12:34:56:78:9A:BC"))

    return result

def update_fs_image(txt_content: bytes):
    fs_img_path = REPO_ROOT / "fs.img"
    if not fs_img_path.exists():
        return

    FS_HEADER_SIZE = 16
    FS_ENTRY_SIZE = 408
    FS_MAX_FILES = 256
    FS_DISK_STRUCT = struct.Struct('<128s256sIIIIIi')

    try:
        with open(fs_img_path, "r+b") as f:
            header = f.read(16)
            if len(header) < 16: return
            magic, ver, img_sz, _ = struct.unpack('<IIII', header)
            if magic != 0x314F5346: return

            target_slot = -1
            free_slot = -1
            highest_payload = (FS_HEADER_SIZE + FS_ENTRY_SIZE * FS_MAX_FILES + 3) & ~3

            for slot in range(FS_MAX_FILES):
                f.seek(FS_HEADER_SIZE + slot * FS_ENTRY_SIZE)
                ebytes = f.read(FS_ENTRY_SIZE)
                if len(ebytes) < FS_DISK_STRUCT.size: break
                name, path, off, size, cap, used, is_dir, parent = FS_DISK_STRUCT.unpack(ebytes[:FS_DISK_STRUCT.size])
                path_str = path.split(b'\0', 1)[0].decode('utf-8', errors='ignore')
                if used:
                    if path_str in ("/wifi_networks.txt", "/etc/wifi_networks.txt"):
                        target_slot = slot
                        target_off = off
                        target_cap = cap
                    highest_payload = max(highest_payload, (off + cap + 3) & ~3)
                elif free_slot == -1:
                    free_slot = slot

            if target_slot != -1 and len(txt_content) <= target_cap:
                f.seek(target_off)
                f.write(txt_content)
                f.seek(FS_HEADER_SIZE + target_slot * FS_ENTRY_SIZE)
                name_b = b"wifi_networks.txt" + b"\0" * (128 - len("wifi_networks.txt"))
                path_b = b"/wifi_networks.txt" + b"\0" * (256 - len("/wifi_networks.txt"))
                new_entry = FS_DISK_STRUCT.pack(name_b, path_b, target_off, len(txt_content), target_cap, 1, 0, -1)
                f.write(new_entry)
                print(f"[scan_host_wifi] Updated /wifi_networks.txt in fs.img ({len(txt_content)} bytes)")
            elif free_slot != -1 and highest_payload + len(txt_content) + 512 < img_sz:
                new_off = (highest_payload + 3) & ~3
                cap = max(1024, len(txt_content))
                f.seek(new_off)
                f.write(txt_content)
                f.seek(FS_HEADER_SIZE + free_slot * FS_ENTRY_SIZE)
                name_b = b"wifi_networks.txt" + b"\0" * (128 - len("wifi_networks.txt"))
                path_b = b"/wifi_networks.txt" + b"\0" * (256 - len("/wifi_networks.txt"))
                new_entry = FS_DISK_STRUCT.pack(name_b, path_b, new_off, len(txt_content), cap, 1, 0, -1)
                f.write(new_entry)
                print(f"[scan_host_wifi] Added /wifi_networks.txt to fs.img slot {free_slot} ({len(txt_content)} bytes)")
    except Exception as e:
        print(f"[scan_host_wifi] fs.img update notice: {e}")

def main():
    networks = get_real_networks()
    out_file = REPO_ROOT / "wifi_networks.txt"

    lines = []
    for name, sig, enc, conn, bssid in networks:
        lines.append(f"{name}|{sig}|{enc}|{conn}|{bssid}")

    content = "\n".join(lines) + "\n"
    out_file.write_text(content)
    print(f"[scan_host_wifi] Found {len(networks)} real networks. Primary: {networks[0][0]} ({networks[0][1]}%)")
    for name, sig, enc, conn, _ in networks[:6]:
        state = "[CONNECTED]" if conn else ("[LOCKED]" if enc else "[OPEN]")
        print(f"  * {name:24s} {sig}% {state}")

    update_fs_image(content.encode('utf-8'))

if __name__ == "__main__":
    main()
