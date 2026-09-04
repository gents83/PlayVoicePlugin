#!/usr/bin/env python3
"""Verify the Python environment used by the PlayVoice OpenVoice service."""

import argparse
import importlib.metadata as meta
import os
import re
import sys
from typing import Dict, List, Optional, Tuple

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(errors="replace")

SUPPORTED_PYTHON = (3, 10)
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
    "pykakasi": ["pykakasi"],
    "anyascii": ["anyascii"],
    "inflect": ["inflect"],
    "onnxruntime": ["onnxruntime"],
    "g2pkk": ["g2pkk"],
    "jamo": ["jamo"],
    "langid": ["langid"],
    "loguru": ["loguru"],
    "txtsplit": ["txtsplit"],
    "unidic": ["unidic"],
    "gruut": ["gruut"],
    "tensorboard": ["tensorboard"],
    "gradio": ["gradio"],
    "unidic_lite": ["unidic_lite"],
    "unidic-lite": ["unidic_lite"],
    "ipadic": ["ipadic"],
}


def check_python_compatibility(version_info: Optional[Tuple[int, int]] = None) -> bool:
    """Return whether the interpreter can install the current OpenVoice pins."""
    detected = version_info or (sys.version_info.major, sys.version_info.minor)
    version_text = f"{detected[0]}.{detected[1]}"
    required_text = f"{SUPPORTED_PYTHON[0]}.{SUPPORTED_PYTHON[1]}.x"
    if detected != SUPPORTED_PYTHON:
        print(
            f"Unsupported Python interpreter: {sys.executable} ({version_text}). "
            f"PlayVoice currently requires Python {required_text}; OpenVoice pins numpy==1.22.0, "
            "which has no compatible wheel for this interpreter. Create or select a fresh Python 3.10 virtual environment."
        )
        return False
    print(f"Python preflight passed: {sys.executable} ({version_text}).")
    return True


def _parse_requirement(requirement: str) -> Tuple[str, Optional[str]]:
    raw = requirement.split(";", 1)[0].strip()
    if "git+" in raw or "github.com" in raw:
        repository = raw.rsplit("/", 1)[-1]
        repository = re.sub(r"\.git$", "", repository, flags=re.IGNORECASE)
        return repository.lower().replace("_", "-"), None
    match = re.match(r"^([A-Za-z0-9_.-]+)\s*(?:==\s*([^\s]+))?", raw)
    return (match.group(1).lower(), match.group(2)) if match else ("", None)


def _read_requirements(requirements_file: str) -> List[str]:
    with open(requirements_file, "r", encoding="utf-8") as requirements:
        return [line.strip() for line in requirements if line.strip() and not line.lstrip().startswith("#")]


def _installed_distributions() -> Dict[str, str]:
    installed = {}
    for distribution in meta.distributions():
        name = distribution.metadata.get("Name") if distribution.metadata else None
        if name:
            installed[name.lower().replace("-", "_")] = distribution.version
    return installed


def _is_installed(package: str, installed: Dict[str, str]) -> bool:
    if package.replace("-", "_") in installed:
        return True
    for candidate in PACKAGE_IMPORT_MAP.get(package, [package]):
        if candidate.replace("-", "_") in installed:
            return True
        try:
            __import__(candidate)
            return True
        except Exception:
            continue
    return False


def _prepare_mecab() -> None:
    try:
        import unidic_lite
        import MeCab

        mecabrc = os.path.join(unidic_lite.DICDIR, "mecabrc")
        os.environ["MECABRC"] = mecabrc
        try:
            import unidic
            unidic.DICDIR = unidic_lite.DICDIR
        except Exception:
            pass

        original_init = MeCab.Tagger.__init__
        def safe_init(self, args=""):
            if not args:
                args = f"-d {unidic_lite.DICDIR} -r {mecabrc}"
            try:
                original_init(self, args)
            except Exception:
                original_init(self, f"-d {unidic_lite.DICDIR} -r {mecabrc}")
        MeCab.Tagger.__init__ = safe_init
    except Exception:
        pass


def check_requirements(requirements_file: str) -> bool:
    if not check_python_compatibility():
        return False
    if not os.path.exists(requirements_file):
        print(f"Error: Requirements file not found at '{requirements_file}'")
        return False

    installed = _installed_distributions()
    missing = []
    mismatched = []
    requirements = _read_requirements(requirements_file)
    requirements.extend(["myshell-openvoice", "melo-tts"])
    seen = set()

    for requirement in requirements:
        package, exact_version = _parse_requirement(requirement)
        normalized_package = package.replace("-", "_")
        if not package or normalized_package in seen:
            continue
        seen.add(normalized_package)
        if not _is_installed(package, installed):
            missing.append(package)
            continue
        if exact_version and normalized_package in installed and installed[normalized_package] != exact_version:
            mismatched.append(f"{package}=={exact_version} (installed {installed[normalized_package]})")

    if mismatched:
        print("Version mismatches:", ", ".join(mismatched))
    if missing:
        print("Missing packages:", ", ".join(missing))
    if mismatched or missing:
        return False

    _prepare_mecab()
    try:
        from openvoice.api import ToneColorConverter  # noqa: F401
        from openvoice.se_extractor import get_se  # noqa: F401
        from melo.api import TTS  # noqa: F401
    except Exception as error:
        print(f"OpenVoice engine imports failed: {error}")
        return False

    print("All requirements are installed and the OpenVoice engine imports successfully.")
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description="Check PlayVoice Python requirements.")
    parser.add_argument("requirements_file", nargs="?", default="requirements.txt")
    parser.add_argument("--preflight-only", action="store_true", help="Only validate the supported Python interpreter.")
    args = parser.parse_args()

    success = check_python_compatibility() if args.preflight_only else check_requirements(args.requirements_file)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
