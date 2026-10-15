#!/usr/bin/env python3
"""
setup-tod-credentials.py

Extracts pairing credentials and OEM initialization blobs for Synaptics 06cb:00dc
and installs them into /var/lib/fprint/syna_00dc/ (or a custom directory) with secure permissions.
"""

import os
import sys
import argparse
import shutil
from pathlib import Path

def extract_credentials(session_dir: Path, out_dir: Path):
    pairing_dir = session_dir / "pairing-candidate"
    artifacts_dir = session_dir / "artifacts"

    if not pairing_dir.is_dir() or not artifacts_dir.is_dir():
        print(f"Error: Could not find required session subdirectories in {session_dir}", file=sys.stderr)
        return False

    out_dir.mkdir(parents=True, exist_ok=True)
    os.chmod(out_dir, 0o700)

    # 1. host_key.pem
    host_key_path = pairing_dir / "host-key.pem"
    if not host_key_path.exists():
        print(f"Error: {host_key_path} not found", file=sys.stderr)
        return False
    dst_key = out_dir / "host_key.pem"
    shutil.copyfile(host_key_path, dst_key)
    dst_key.chmod(0o600)
    print(f"  [+] Installed {dst_key.name} ({dst_key.stat().st_size} bytes)")

    # 2. host_cert.bin (184 bytes at offset 6 in ownership-atomic-oem-reply.bin)
    reply_path = pairing_dir / "ownership-atomic-oem-reply.bin"
    if not reply_path.exists():
        reply_path = pairing_dir / "ownership-reply.bin"
    if not reply_path.exists():
        print(f"Error: ownership reply not found in {pairing_dir}", file=sys.stderr)
        return False
    
    reply_data = reply_path.read_bytes()
    if len(reply_data) < 190:
        print(f"Error: ownership reply too short ({len(reply_data)} bytes)", file=sys.stderr)
        return False
    cert_data = reply_data[6:190]
    dst_cert = out_dir / "host_cert.bin"
    dst_cert.write_bytes(cert_data)
    dst_cert.chmod(0o600)
    print(f"  [+] Installed {dst_cert.name} ({len(cert_data)} bytes)")

    # 3. server_ecdh_pub.bin (65-byte uncompressed SEC1 key from public-material-after-ownership.bin)
    mat_path = pairing_dir / "public-material-after-ownership.bin"
    if not mat_path.exists():
        print(f"Error: {mat_path} not found", file=sys.stderr)
        return False
    mat_data = mat_path.read_bytes()
    if len(mat_data) != 926:
        print(f"Error: Unexpected length for public material ({len(mat_data)} bytes)", file=sys.stderr)
        return False
    ecdh_offset = 526
    x_le = mat_data[ecdh_offset + 8 : ecdh_offset + 40]
    y_le = mat_data[ecdh_offset + 76 : ecdh_offset + 108]
    sec1_pub = b"\x04" + bytes(reversed(x_le)) + bytes(reversed(y_le))
    dst_ecdh = out_dir / "server_ecdh_pub.bin"
    dst_ecdh.write_bytes(sec1_pub)
    dst_ecdh.chmod(0o600)
    print(f"  [+] Installed {dst_ecdh.name} ({len(sec1_pub)} bytes)")

    # 4. capture_init.bin
    init_blob_path = artifacts_dir / "acer-capture-init-rom6-20-group2.bin"
    if not init_blob_path.exists():
        print(f"Error: {init_blob_path} not found", file=sys.stderr)
        return False
    dst_init = out_dir / "capture_init.bin"
    shutil.copyfile(init_blob_path, dst_init)
    dst_init.chmod(0o600)
    print(f"  [+] Installed {dst_init.name} ({dst_init.stat().st_size} bytes)")

    # 5. capture_packet.bin
    pkt_path = artifacts_dir / "type2449-raw-capture-request.bin"
    if not pkt_path.exists():
        print(f"Error: {pkt_path} not found", file=sys.stderr)
        return False
    dst_pkt = out_dir / "capture_packet.bin"
    shutil.copyfile(pkt_path, dst_pkt)
    dst_pkt.chmod(0o600)
    print(f"  [+] Installed {dst_pkt.name} ({dst_pkt.stat().st_size} bytes)")

    print(f"Successfully configured Synaptics TOD credentials in {out_dir}")
    return True

def main():
    parser = argparse.ArgumentParser(description="Install Synaptics 06cb:00dc pairing credentials for libfprint-tod")
    parser.add_argument("--session-dir", type=Path, default=Path(__file__).resolve().parent.parent / "sessions/2026-10-15-synaptics-06cb-00dc",
                        help="Session directory containing pairing candidate and artifacts")
    parser.add_argument("--out-dir", type=Path, default=Path("/var/lib/fprint/syna_00dc"),
                        help="Target directory (default: /var/lib/fprint/syna_00dc)")
    args = parser.parse_args()

    print(f"Configuring credentials from {args.session_dir} to {args.out_dir}...")
    success = extract_credentials(args.session_dir, args.out_dir)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
