#!/usr/bin/env python3
"""Submodule & driver version synchronization and verification tool."""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional, Tuple

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SYNA_DIR = ROOT / "drivers" / "syna_tod"
DEFAULT_PROTOTYPE_DIR = ROOT / "workspace" / "libfprint" / "libfprint" / "drivers" / "syna_00dc"
DEFAULT_UPSTREAM_REPO = ROOT.parent / "synaptics-00dc"


def extract_meson_version(content: str) -> Optional[str]:
    m = re.search(r"version:\s*'([^']+)'", content)
    return m.group(1).strip() if m else None


def extract_bootstrap_version(content: str) -> Optional[str]:
    m = re.search(r"^VERSION=v?([0-9a-zA-Z.-]+)", content, re.MULTILINE)
    return m.group(1).strip() if m else None


def verify_versions_match(driver_dir: Path) -> Tuple[bool, str]:
    meson_file = driver_dir / "meson.build"
    bootstrap_file = driver_dir / "bootstrap.sh"
    if not meson_file.is_file():
        return False, f"Missing {meson_file}"
    if not bootstrap_file.is_file():
        return False, f"Missing {bootstrap_file}"

    meson_ver = extract_meson_version(meson_file.read_text(encoding="utf-8"))
    bootstrap_ver = extract_bootstrap_version(bootstrap_file.read_text(encoding="utf-8"))

    if not meson_ver:
        return False, f"Could not parse version from {meson_file}"
    if not bootstrap_ver:
        return False, f"Could not parse VERSION from {bootstrap_file}"
    if meson_ver != bootstrap_ver:
        return False, f"Version mismatch: meson.build has '{meson_ver}' but bootstrap.sh has '{bootstrap_ver}'"
    return True, f"Versions match: {meson_ver}"


def check_working_tree_clean(repo_dir: Path) -> Tuple[bool, str]:
    res = subprocess.run(
        ["git", "-C", str(repo_dir), "status", "--porcelain"],
        capture_output=True, text=True
    )
    if res.returncode != 0:
        return False, f"git status failed in {repo_dir}: {res.stderr.strip()}"
    dirty = res.stdout.strip()
    if dirty:
        return False, f"Working tree at {repo_dir} is dirty:\n{dirty}"
    return True, f"Working tree at {repo_dir} is clean."


def check_submodule_reachable_on_remote(submodule_dir: Path, remote_branch: str = "origin/main") -> Tuple[bool, str]:
    res = subprocess.run(
        ["git", "-C", str(submodule_dir), "rev-parse", "HEAD"],
        capture_output=True, text=True
    )
    if res.returncode != 0:
        return False, f"Could not determine HEAD in {submodule_dir}: {res.stderr.strip()}"
    head_sha = res.stdout.strip()

    # Check if HEAD is an ancestor of remote branch
    res_anc = subprocess.run(
        ["git", "-C", str(submodule_dir), "merge-base", "--is-ancestor", "HEAD", remote_branch],
        capture_output=True, text=True
    )
    if res_anc.returncode == 0:
        return True, f"Submodule HEAD ({head_sha[:8]}) is reachable on {remote_branch}"

    # Try fetching remote branch in case tracking ref is stale
    try:
        remote, branch = remote_branch.split("/", 1) if "/" in remote_branch else ("origin", "main")
        subprocess.run(
            ["git", "-C", str(submodule_dir), "fetch", remote, branch],
            capture_output=True, text=True, timeout=10
        )
        res_anc = subprocess.run(
            ["git", "-C", str(submodule_dir), "merge-base", "--is-ancestor", "HEAD", remote_branch],
            capture_output=True, text=True
        )
        if res_anc.returncode == 0:
            return True, f"Submodule HEAD ({head_sha[:8]}) is reachable on {remote_branch}"
    except Exception:
        pass

    return False, f"Submodule HEAD ({head_sha[:8]}) is NOT reachable on {remote_branch}. Commit must be pushed upstream before updating submodule pin."


def rewrite_native_prototype_id(content: str) -> str:
    """Rewrites dev_class->id = \"syna_00dc_tod\"; to dev_class->id = \"syna_00dc\";"""
    return re.sub(
        r'dev_class->id\s*=\s*"syna_00dc_tod";',
        'dev_class->id = "syna_00dc";',
        content
    )


def check_driver(root: Path = ROOT) -> int:
    syna_dir = root / "drivers" / "syna_tod"
    print(f"Checking driver submodule at {syna_dir}...")
    if not syna_dir.is_dir():
        print(f"Error: {syna_dir} does not exist.", file=sys.stderr)
        return 1

    clean, msg = check_working_tree_clean(syna_dir)
    if not clean:
        print(f"Check failed: {msg}", file=sys.stderr)
        return 1
    print(f"✓ {msg}")

    ver_ok, ver_msg = verify_versions_match(syna_dir)
    if not ver_ok:
        print(f"Check failed: {ver_msg}", file=sys.stderr)
        return 1
    print(f"✓ {ver_msg}")

    reach_ok, reach_msg = check_submodule_reachable_on_remote(syna_dir)
    if not reach_ok:
        print(f"Check failed: {reach_msg}", file=sys.stderr)
        return 1
    print(f"✓ {reach_msg}")

    print("All driver checks passed successfully.")
    return 0


def sync_driver(root: Path = ROOT, synaptics_repo: Optional[Path] = None) -> int:
    syna_dir = root / "drivers" / "syna_tod"
    prototype_dir = root / "workspace" / "libfprint" / "libfprint" / "drivers" / "syna_00dc"
    if synaptics_repo is None:
        synaptics_repo = DEFAULT_UPSTREAM_REPO

    if synaptics_repo.is_dir():
        print(f"Checking upstream synaptics-00dc repository at {synaptics_repo}...")
        clean, msg = check_working_tree_clean(synaptics_repo)
        if not clean:
            print(f"Error: upstream synaptics-00dc has uncommitted changes:\n{msg}", file=sys.stderr)
            return 1

        res = subprocess.run(
            ["git", "-C", str(synaptics_repo), "rev-list", "origin/main..HEAD"],
            capture_output=True, text=True
        )
        if res.returncode == 0 and res.stdout.strip():
            print("Error: synaptics-00dc has unpushed commits ahead of origin/main. Push upstream first!", file=sys.stderr)
            return 1

    print(f"Fetching and fast-forwarding {syna_dir} from origin/main...")
    res = subprocess.run(
        ["git", "-C", str(syna_dir), "fetch", "origin", "main"],
        capture_output=True, text=True
    )
    if res.returncode != 0:
        print(f"Error fetching origin/main for {syna_dir}: {res.stderr.strip()}", file=sys.stderr)
        return 1

    res = subprocess.run(
        ["git", "-C", str(syna_dir), "checkout", "origin/main"],
        capture_output=True, text=True
    )
    if res.returncode != 0:
        print(f"Error checking out origin/main in {syna_dir}: {res.stderr.strip()}", file=sys.stderr)
        return 1

    if prototype_dir.is_dir():
        print(f"Synchronizing driver sources into native prototype at {prototype_dir}...")
        src_dir = syna_dir / "src"
        for p in src_dir.glob("*.[ch]"):
            dest = prototype_dir / p.name
            content = p.read_text(encoding="utf-8")
            if p.name == "syna_device.c":
                content = rewrite_native_prototype_id(content)
            dest.write_text(content, encoding="utf-8")
            print(f"  Updated {dest.name}")

    print("Driver sync completed successfully.")
    return 0


def get_git_hooks_dir(root: Path) -> Path:
    git_path = root / ".git"
    if git_path.is_file():
        line = git_path.read_text(encoding="utf-8").strip()
        if line.startswith("gitdir:"):
            gitdir = Path(line.split(":", 1)[1].strip())
            if not gitdir.is_absolute():
                gitdir = root / gitdir
            return gitdir / "hooks"
    return git_path / "hooks"


def install_hooks(root: Path = ROOT) -> int:
    hooks_dir = get_git_hooks_dir(root)
    hooks_dir.mkdir(parents=True, exist_ok=True)

    pre_commit = hooks_dir / "pre-commit"
    pre_push = hooks_dir / "pre-push"

    pre_commit_content = """#!/bin/sh
set -e
# If drivers/syna_tod is modified or staged, run sync check
if git diff --cached --name-only | grep -q "^drivers/syna_tod"; then
    echo "Running driver sync check on staged drivers/syna_tod..."
    python3 scripts/sync-driver.py --check
fi
"""

    pre_push_content = """#!/bin/sh
set -e
# If any commit being pushed touches drivers/syna_tod, verify reachability and sync
touched=0
while read local_ref local_oid remote_ref remote_oid; do
    if [ "$local_oid" = "0000000000000000000000000000000000000000" ]; then
        continue
    fi
    if [ "$remote_oid" = "0000000000000000000000000000000000000000" ]; then
        range="$local_oid"
    else
        range="$remote_oid..$local_oid"
    fi
    if git diff --name-only "$range" 2>/dev/null | grep -q "^drivers/syna_tod"; then
        touched=1
        break
    fi
done

if [ "$touched" -eq 1 ]; then
    echo "Running driver sync check before pushing drivers/syna_tod..."
    python3 scripts/sync-driver.py --check
fi
"""

    pre_commit.write_text(pre_commit_content, encoding="utf-8")
    pre_commit.chmod(0o755)
    print(f"Installed {pre_commit}")

    pre_push.write_text(pre_push_content, encoding="utf-8")
    pre_push.chmod(0o755)
    print(f"Installed {pre_push}")

    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true", help="Verify driver submodule cleanliness, version match, and remote reachability")
    group.add_argument("--sync", action="store_true", help="Sync drivers/syna_tod and workspace/libfprint from upstream origin/main")
    group.add_argument("--install-hooks", action="store_true", help="Install git pre-commit and pre-push hooks")

    args = parser.parse_args(argv)

    if args.check:
        return check_driver(ROOT)
    elif args.sync:
        return sync_driver(ROOT)
    elif args.install_hooks:
        return install_hooks(ROOT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
