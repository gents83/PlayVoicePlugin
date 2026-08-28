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

# Map pip package names to possible module import names or distribution aliases
PACKAGE_IMPORT_MAP = {
    "melo_tts": ["melo", "melo_tts", "melotts"],
    "melo-tts": ["melo", "melo_tts", "melotts"],
    "myshell_openvoice": ["openvoice", "myshell_openvoice"],
    "myshell-openvoice": ["openvoice", "myshell_openvoice"],
    "openvoice": ["openvoice"],
    "audioop_lts": ["audioop", "pyaudioop", "audioop_lts"],
    "audioop-lts": ["audioop", "pyaudioop", "audioop_lts"],
    "pyaudioop": ["audioop", "pyaudioop", "audioop_lts"],
    "whisper_timestamped": ["whisper_timestamped"],
    "whisper-timestamped": ["whisper_timestamped"],
    "eng_to_ipa": ["eng_to_ipa"],
    "eng-to-ipa": ["eng_to_ipa"],
    "speechrecognition": ["speech_recognition"],
    "mecab_python3": ["MeCab", "mecab"],
    "mecab-python3": ["MeCab", "mecab"],
    "g2p_en": ["g2p_en"],
    "cached_path": ["cached_path"],
    "cached-path": ["cached_path"],
    "pypinyin": ["pypinyin"],
    "cutlet": ["cutlet"],
    "inflect": ["inflect"],
    "onnxruntime": ["onnxruntime"],
}


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

    # Always check core OpenVoice and MeloTTS packages as well (deduplicated)
    required_pkgs = []
    seen = set()
    for item in lines + ["myshell-openvoice", "melo-tts"]:
        if item not in seen:
            seen.add(item)
            required_pkgs.append(item)

    for req in required_pkgs:
        if req.startswith('#') or req.startswith('--'):
            continue

        # If line contains an optional extra marker (e.g. '; extra == ...'), ignore for base requirements check
        if ';' in req:
            condition_part = req.split(';', 1)[1].strip()
            if 'extra ==' in condition_part or 'extra!=' in condition_part or 'extra' in condition_part:
                continue

        # Extract package or repository module name
        raw_req = req.split(';')[0].strip()
        if 'git+' in raw_req or 'github.com' in raw_req:
            repo_name = raw_req.rstrip('.git').split('/')[-1].lower()
            raw_pkg = repo_name.replace('_', '-').replace('melotts', 'melo-tts').replace('openvoice', 'myshell-openvoice')
        else:
            raw_pkg = raw_req.split('@')[0].split('>=')[0].split('<=')[0].split('==')[0].split('~=')[0].split('!=')[0].strip().lower()

        norm_pkg = raw_pkg.replace('-', '_')

        if not norm_pkg:
            continue

        # Check distribution names
        if norm_pkg in installed:
            continue

        # Check alternative distribution names & import module names
        candidates = PACKAGE_IMPORT_MAP.get(norm_pkg, [norm_pkg, raw_pkg])
        bInstalled = False
        for candidate in candidates:
            cand_norm = candidate.replace('-', '_')
            if cand_norm in installed:
                bInstalled = True
                break
            try:
                __import__(candidate)
                bInstalled = True
                break
            except Exception:
                pass

        if not bInstalled and raw_pkg not in missing:
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
