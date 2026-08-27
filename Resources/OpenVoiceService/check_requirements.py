#!/usr/bin/env python3
"""
Requirements verification script for PlayVoice Unreal Engine Plugin.
Parses a requirements.txt file and checks whether required packages are installed.
"""

import sys
import os
import argparse

try:
    import importlib.metadata as meta
except ImportError:
    import importlib_metadata as meta


def check_requirements(requirements_file: str) -> bool:
    if not os.path.exists(requirements_file):
        print(f"Error: Requirements file not found at '{requirements_file}'")
        return False

    with open(requirements_file, 'r', encoding='utf-8') as f:
        lines = [l.strip() for l in f if l.strip() and not l.strip().startswith('#')]

    installed = set()
    for dist in meta.distributions():
        if dist.metadata:
            name = dist.metadata.get('Name')
            if name:
                installed.add(name.lower().replace('-', '_'))

    missing = []
    for req in lines:
        raw_pkg = req.split(';')[0].split('>=')[0].split('<=')[0].split('==')[0].split('~=')[0].split('!=')[0].strip().lower()
        norm_pkg = raw_pkg.replace('-', '_')
        if norm_pkg and norm_pkg not in installed:
            try:
                __import__(norm_pkg)
            except Exception:
                missing.append(raw_pkg)

    if missing:
        print("Missing packages:", ", ".join(missing))
        return False
    else:
        print("All requirements satisfied.")
        return True


def main():
    parser = argparse.ArgumentParser(description="Check installed Python requirements for PlayVoice Plugin.")
    parser.add_argument("requirements_file", nargs="?", default="requirements.txt", help="Path to requirements.txt file")
    args = parser.parse_args()

    success = check_requirements(args.requirements_file)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
