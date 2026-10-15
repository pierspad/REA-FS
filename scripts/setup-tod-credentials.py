#!/usr/bin/env python3
"""Export existing private 06cb:00dc session material; does not pair a new device."""
import argparse
import os
from pathlib import Path
import sys


def bounded_read(path, expected=None):
    with path.open('rb') as stream:
        data = stream.read(1024 * 1024 + 1)
    if not data or len(data) > 1024 * 1024:
        raise ValueError(f'invalid file size: {path.name}')
    if expected is not None and len(data) != expected:
        raise ValueError(f'unexpected size: {path.name} ({len(data)}, expected {expected})')
    return data


def extract_credentials(session_dir: Path, out_dir: Path):
    created = []
    made_directory = False
    try:
        if out_dir.exists() or out_dir.is_symlink():
            raise ValueError('destination already exists; use a new private directory')
        pairing = session_dir/'pairing-candidate'
        artifacts = session_dir/'artifacts'
        key = bounded_read(pairing/'host-key.pem')
        if b'PRIVATE KEY-----' not in key:
            raise ValueError('host key has no PEM private-key marker')
        reply_path = pairing/'ownership-atomic-oem-reply.bin'
        if not reply_path.exists(): reply_path = pairing/'ownership-reply.bin'
        reply = bounded_read(reply_path)
        if len(reply) < 190: raise ValueError('ownership reply too short')
        material = bounded_read(pairing/'public-material-after-ownership.bin', 926)
        point = b'\x04' + material[534:566][::-1] + material[602:634][::-1]
        files = {
            'host_key.pem': key,
            'host_cert.bin': reply[6:190],
            'server_ecdh_pub.bin': point,
            'capture_init.bin': bounded_read(artifacts/'acer-capture-init-rom6-20-group2.bin', 4676),
            'capture_packet.bin': bounded_read(artifacts/'type2449-raw-capture-request.bin', 1917),
        }
        # Validate all sources before creating the destination. Never replace credentials.
        out_dir.mkdir(mode=0o700)
        made_directory = True
        for name, data in files.items():
            target = out_dir/name
            fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            created.append(target)
            with os.fdopen(fd, 'wb') as stream: stream.write(data)
        print(f'Exported five private files to {out_dir}; key validity and sensor identity must be checked separately.')
        return True
    except (OSError, ValueError) as exc:
        for target in created: target.unlink(missing_ok=True)
        if made_directory: out_dir.rmdir()
        print(f'Error: {exc}', file=sys.stderr)
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session-dir', type=Path, required=True)
    parser.add_argument('--out-dir', type=Path, required=True)
    args = parser.parse_args()
    return 0 if extract_credentials(args.session_dir, args.out_dir) else 1

if __name__ == '__main__':
    sys.exit(main())
