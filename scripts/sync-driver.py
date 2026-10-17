#!/usr/bin/env python3
"""CLI wrapper for driver synchronization and verification."""
import sys
from pathlib import Path

# Add project root to path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.driver_sync import main

if __name__ == "__main__":
    sys.exit(main())
