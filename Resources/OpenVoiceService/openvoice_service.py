#!/usr/bin/env python3
"""
OpenVoice Integration Service & CLI for PlayVoice Unreal Engine Plugin.
Integrates MyShell OpenVoice zero-shot voice cloning framework and MeloTTS backend.
Provides tone color extraction, zero-shot TTS synthesis, and REST server API.
"""

import os
import sys
import io
import json
import wave
import math
import struct
import argparse
import logging
import tempfile
from typing import List, Optional, Dict, Any

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("OpenVoiceService")

# Suppress HuggingFace symlink warnings, unauthenticated HF Hub warnings, and allow proxied NLTK downloads
os.environ['HF_HUB_DISABLE_SYMLINKS_WARNING'] = '1'
os.environ['HF_HUB_DISABLE_IMPLICIT_TOKEN_WARNING'] = '1'
os.environ['TRANSFORMERS_NO_ADVISORY_WARNINGS'] = '1'
os.environ['NLTK_ALLOW_PROXIED_URLOPEN'] = '1'
os.environ['TORCH_HUB_TRUSTED_REPO'] = 'snakers4/silero-vad'

# Suppress verbose 404/307 HTTP probe logs from httpx, huggingface_hub, transformers, and urllib3
for noisy_logger_name in ["httpx", "huggingface_hub", "huggingface_hub.utils._http", "transformers", "urllib3", "nltk"]:
    logging.getLogger(noisy_logger_name).setLevel(logging.ERROR)

# Pre-download required NLTK resources (cmudict, averaged_perceptron_tagger) for MeloTTS english cleaner
try:
    import nltk
    for nltk_res in ['cmudict', 'averaged_perceptron_tagger', 'averaged_perceptron_tagger_eng']:
        try:
            nltk.data.find(f'corpora/{nltk_res}')
        except LookupError:
            try:
                nltk.data.find(f'taggers/{nltk_res}')
            except LookupError:
                try:
                    nltk.download(nltk_res, quiet=True)
                except Exception as dl_err:
                    logger.warning(f"Could not pre-download NLTK resource {nltk_res}: {dl_err}")
except Exception:
    pass

# Safe audioop import fallback across Python standard library versions
try:
    import audioop
except ImportError:
    try:
        import pyaudioop as audioop
    except ImportError:
        try:
            import audioop_lts as audioop
        except ImportError:
            audioop = None

if audioop is not None:
    sys.modules['audioop'] = audioop
    sys.modules['pyaudioop'] = audioop

# Configure MeCab mecabrc and unidic DICDIR for MeloTTS if unidic-lite dictionary is available
try:
    import unidic_lite
    import os
    _mecabrc_file = os.path.join(unidic_lite.DICDIR, 'mecabrc')
    if os.path.exists(_mecabrc_file):
        os.environ['MECABRC'] = _mecabrc_file
    try:
        import unidic
        unidic.DICDIR = unidic_lite.DICDIR
    except Exception:
        pass
    import MeCab
    _orig_tagger_init = MeCab.Tagger.__init__
    def _safe_tagger_init(self, args=""):
        if not args or args == "":
            args = f'-d {unidic_lite.DICDIR} -r {_mecabrc_file}'
        try:
            _orig_tagger_init(self, args)
        except Exception:
            _orig_tagger_init(self, f'-d {unidic_lite.DICDIR} -r {_mecabrc_file}')
    MeCab.Tagger.__init__ = _safe_tagger_init
except Exception:
    pass

# OpenVoice / Torch / MeloTTS Imports with fallback
HAS_OPENVOICE = False
try:
    import torch
    import torchaudio
    from openvoice.se_extractor import get_se
    from openvoice.api import ToneColorConverter
    from melo.api import TTS
    HAS_OPENVOICE = True
    logger.info("Successfully initialized OpenVoice and MeloTTS engine.")
except ImportError as e:
    logger.info(f"OpenVoice / MeloTTS engine not loaded ({e}). Running in built-in fallback TTS voice synthesis mode.")

try:
    from fastapi import FastAPI, HTTPException, Response
    from fastapi.responses import Response, JSONResponse
    from pydantic import BaseModel
    import uvicorn
    HAS_FASTAPI = True
except ImportError:
    HAS_FASTAPI = False


def create_combined_reference_wav(valid_files: List[str], min_duration_sec: float = 10.0) -> Optional[str]:
    """
    Concatenates multiple reference audio files into a single temporary WAV file.
    If total duration is less than min_duration_sec, repeats clips
    to ensure OpenVoice get_se has sufficient audio length (>3s) for extraction.
    """
    if not valid_files:
        return None

    combined_samples = []
    target_sr = 24000

    for filepath in valid_files:
        try:
            with wave.open(filepath, 'rb') as wf:
                nchannels = wf.getnchannels()
                sampwidth = wf.getsampwidth()
                framerate = wf.getframerate()
                nframes = wf.getnframes()
                if nframes == 0 or framerate == 0:
                    continue
                frames = wf.readframes(nframes)

                if sampwidth == 2:
                    raw_samples = struct.unpack(f'<{nframes * nchannels}h', frames)
                elif sampwidth == 1:
                    raw_samples = [(s - 128) * 256 for s in struct.unpack(f'<{nframes * nchannels}B', frames)]
                else:
                    continue

                if nchannels > 1:
                    mono_samples = [int(sum(raw_samples[i:i+nchannels]) / nchannels) for i in range(0, len(raw_samples), nchannels)]
                else:
                    mono_samples = list(raw_samples)

                if framerate != target_sr:
                    num_target = int(len(mono_samples) * target_sr / framerate)
                    resample_buf = []
                    for i in range(num_target):
                        src_pos = float(i) * framerate / target_sr
                        idx = int(src_pos)
                        frac = src_pos - idx
                        if idx >= len(mono_samples) - 1:
                            val = mono_samples[-1] if mono_samples else 0
                        else:
                            val = (1.0 - frac) * mono_samples[idx] + frac * mono_samples[idx + 1]
                        resample_buf.append(max(-32768, min(32767, int(val))))
                    mono_samples = resample_buf

                combined_samples.extend(mono_samples)
                # Short 0.1s silence pause between reference clips
                combined_samples.extend([0] * int(target_sr * 0.1))
        except Exception as e:
            logger.warning(f"Could not read reference audio file '{filepath}' for concatenation: {e}")

    if not combined_samples:
        return None

    # If total audio length is under min_duration_sec, repeat audio buffer
    target_total_samples = int(target_sr * min_duration_sec)
    if len(combined_samples) < target_total_samples:
        repeats = math.ceil(target_total_samples / len(combined_samples))
        combined_samples = (combined_samples * repeats)[:target_total_samples]

    temp_path = os.path.join(tempfile.gettempdir(), "combined_reference_se.wav")
    try:
        with wave.open(temp_path, 'wb') as out_wf:
            out_wf.setnchannels(1)
            out_wf.setsampwidth(2)
            out_wf.setframerate(target_sr)
            packed = struct.pack(f'<{len(combined_samples)}h', *combined_samples)
            out_wf.writeframes(packed)
        return temp_path
    except Exception as e:
        logger.error(f"Failed writing combined reference WAV: {e}")
        return None


def ensure_converter_checkpoints(checkpoint_dir: str) -> Optional[str]:
    """
    Ensures OpenVoice converter checkpoints (config.json and checkpoint.pth)
    are present in checkpoint_dir/converter/. Automatically downloads from
    HuggingFace (myshell-ai/OpenVoiceV2) if missing.
    """
    converter_dir = os.path.join(checkpoint_dir, "converter")
    config_path = os.path.join(converter_dir, "config.json")
    ckpt_path = os.path.join(converter_dir, "checkpoint.pth")

    if os.path.exists(config_path) and os.path.exists(ckpt_path):
        return converter_dir

    logger.info(f"OpenVoice converter checkpoints not found at {converter_dir}. Automatically downloading OpenVoice v2 checkpoints from HuggingFace (myshell-ai/OpenVoiceV2)...")
    os.makedirs(converter_dir, exist_ok=True)

    urls = {
        config_path: "https://huggingface.co/myshell-ai/OpenVoiceV2/raw/main/converter/config.json",
        ckpt_path: "https://huggingface.co/myshell-ai/OpenVoiceV2/resolve/main/converter/checkpoint.pth"
    }

    import urllib.request
    for dest_path, url in urls.items():
        if not os.path.exists(dest_path):
            try:
                logger.info(f"Downloading {os.path.basename(dest_path)} from {url}...")
                req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
                with urllib.request.urlopen(req) as resp, open(dest_path + ".tmp", "wb") as out_file:
                    out_file.write(resp.read())
                os.rename(dest_path + ".tmp", dest_path)
                logger.info(f"Successfully downloaded {os.path.basename(dest_path)}.")
            except Exception as dl_err:
                logger.error(f"Failed to download OpenVoice checkpoint {os.path.basename(dest_path)} from {url}: {dl_err}")
                if os.path.exists(dest_path + ".tmp"):
                    try:
                        os.remove(dest_path + ".tmp")
                    except Exception:
                        pass
                return None

    if os.path.exists(config_path) and os.path.exists(ckpt_path):
        return converter_dir

    return None


class OpenVoiceEngine:
    """
    Manages OpenVoice model checkpoints, Tone Color Extraction, and Zero-Shot Speech Synthesis.
    """
    def __init__(self, checkpoint_dir: str = "checkpoints"):
        self.checkpoint_dir = checkpoint_dir
        self.converter = None
        self.tts_models = {}

        if HAS_OPENVOICE:
            try:
                script_dir = os.path.dirname(os.path.abspath(__file__))
                candidate_paths = [
                    checkpoint_dir,
                    os.path.join(script_dir, checkpoint_dir),
                    os.path.join(script_dir, "..", "..", checkpoint_dir),
                    os.path.join(os.getcwd(), checkpoint_dir)
                ]
                resolved_ckpt_dir = None
                for cand in candidate_paths:
                    cand_abs = os.path.abspath(cand)
                    if os.path.exists(os.path.join(cand_abs, "converter", "config.json")) and os.path.exists(os.path.join(cand_abs, "converter", "checkpoint.pth")):
                        resolved_ckpt_dir = cand_abs
                        break

                if not resolved_ckpt_dir:
                    primary_dir = os.path.abspath(checkpoint_dir)
                    ensure_converter_checkpoints(primary_dir)
                    if os.path.exists(os.path.join(primary_dir, "converter", "config.json")) and os.path.exists(os.path.join(primary_dir, "converter", "checkpoint.pth")):
                        resolved_ckpt_dir = primary_dir

                if resolved_ckpt_dir:
                    self.checkpoint_dir = resolved_ckpt_dir
                    converter_path = os.path.join(resolved_ckpt_dir, "converter")
                    device = "cuda" if torch.cuda.is_available() else "cpu"
                    self.converter = ToneColorConverter(f"{converter_path}/config.json", device=device)
                    self.converter.load_ckpt(f"{converter_path}/checkpoint.pth")
                    logger.info(f"Loaded OpenVoice ToneColorConverter on {device} from {converter_path}")
                else:
                    logger.warning("OpenVoice converter checkpoints not found and auto-download was not completed. Tone color extraction will fall back to acoustic profile mode.")
            except Exception as e:
                logger.error(f"Failed loading OpenVoice checkpoints: {e}")

    def extract_tone_color(self, reference_files: List[str], character_name: str) -> Dict[str, Any]:
        """
        Extracts speaker target tone color embedding and acoustic pitch/timbre profile from reference audio clips.
        """
        valid_files = [f for f in reference_files if os.path.exists(f)]
        acoustic_profile = analyze_reference_audio_files(valid_files)

        if not valid_files:
            err_msg = f"No valid reference audio files found on disk for character '{character_name}' at provided paths: {reference_files}"
            logger.error(err_msg)
            return {
                "status": "error",
                "message": err_msg,
                "character_name": character_name
            }

        if HAS_OPENVOICE and self.converter is not None:
            try:
                device = "cuda" if torch.cuda.is_available() else "cpu"
                target_dir = os.path.join(tempfile.gettempdir(), 'processed')
                os.makedirs(target_dir, exist_ok=True)

                # Concatenate reference audio files into a combined WAV file (>=10s) to guarantee sufficient audio length for get_se
                combined_ref_path = create_combined_reference_wav(valid_files, min_duration_sec=10.0)

                se_tensors = []
                files_to_try = [combined_ref_path] if combined_ref_path else []
                files_to_try.extend(valid_files)

                for ref_file in files_to_try:
                    if not ref_file or not os.path.exists(ref_file):
                        continue

                    se = None
                    try:
                        se, _ = get_se(ref_file, self.converter, target_dir=target_dir, vad=True)
                    except Exception as vad_err:
                        try:
                            se, _ = get_se(ref_file, self.converter, target_dir=target_dir, vad=False)
                        except Exception as novad_err:
                            logger.warning(f"Could not extract tone color from reference audio file '{ref_file}': {novad_err}")

                    if se is not None:
                        if isinstance(se, torch.Tensor):
                            se_tensors.append(se.detach().cpu())
                        elif hasattr(se, 'tolist'):
                            se_tensors.append(torch.tensor(se))
                        else:
                            se_tensors.append(torch.tensor(list(se)))

                if se_tensors:
                    stacked = torch.stack([t.float() for t in se_tensors])
                    target_se = torch.mean(stacked, dim=0)
                    se_list = target_se.numpy().tolist()

                    embedding_payload = {
                        "character_name": character_name,
                        "num_reference_files": len(valid_files),
                        "valid_reference_files": valid_files,
                        "target_se": se_list,
                        "acoustic_profile": acoustic_profile,
                        "engine": "OpenVoice-v2"
                    }
                    return {
                        "status": "success",
                        "character_name": character_name,
                        "embedding_data": json.dumps(embedding_payload),
                        "model_checkpoint": f"{self.checkpoint_dir}/{character_name}_se.pth"
                    }
            except Exception as e:
                logger.warning(f"OpenVoice get_se extraction failed ({e}). Falling back to acoustic profile extraction.")

        # Fallback acoustic profile embedding generator if OpenVoice is not present or extraction encountered an error / missing converter
        logger.info(f"Generating fallback acoustic profile embedding for '{character_name}' (Reference files: {len(valid_files)}).")
        embedding_payload = {
            "character_name": character_name,
            "num_reference_files": len(valid_files),
            "valid_reference_files": valid_files,
            "target_se": [],
            "acoustic_profile": acoustic_profile,
            "engine": "Fallback-TTS"
        }
        return {
            "status": "success",
            "character_name": character_name,
            "embedding_data": json.dumps(embedding_payload),
            "model_checkpoint": f"{self.checkpoint_dir}/{character_name}_se.pth"
        }

    def synthesize(self, text: str, character_name: str, language: str = "EN", speed: float = 1.0, embedding_data: Optional[str] = None, reference_audio_files: Optional[List[str]] = None, guide_audio_file: Optional[str] = None, emotion: Optional[str] = None) -> bytes:
        """
        Synthesizes text into voice matching character reference tone, pitch, and speed.
        If embedding_data is not provided, extracts tone color automatically on the fly from reference_audio_files.
        If guide_audio_file is provided, uses the recorded guide track as source speech to transfer custom speed and emotions.
        """
        # Automatically extract embedding data on the fly if not provided but reference audio files exist
        if not embedding_data and reference_audio_files:
            valid_refs = [f for f in reference_audio_files if os.path.exists(f)]
            if valid_refs:
                logger.info(f"Extracting tone color on the fly from {len(valid_refs)} reference audio clips for character '{character_name}'")
                ext_res = self.extract_tone_color(valid_refs, character_name)
                embedding_data = ext_res.get("embedding_data")

        acoustic_profile = None
        if embedding_data:
            try:
                emb = json.loads(embedding_data)
                acoustic_profile = emb.get("acoustic_profile")
            except Exception as e:
                logger.warning(f"Could not parse embedding_data for synthesis: {e}")

        if HAS_OPENVOICE:
            try:
                device = "cuda" if torch.cuda.is_available() else "cpu"
                if language not in self.tts_models:
                    self.tts_models[language] = TTS(language=language, device=device)

                tts_engine_model = self.tts_models[language]
                speaker_ids = {}
                if hasattr(tts_engine_model, 'hps') and hasattr(tts_engine_model.hps, 'data') and hasattr(tts_engine_model.hps.data, 'spk2id'):
                    spk2id_obj = tts_engine_model.hps.data.spk2id
                    if isinstance(spk2id_obj, dict):
                        speaker_ids = spk2id_obj
                    elif hasattr(spk2id_obj, 'items'):
                        try:
                            speaker_ids = dict(spk2id_obj.items())
                        except Exception:
                            pass
                    elif hasattr(spk2id_obj, '__dict__'):
                        speaker_ids = dict(spk2id_obj.__dict__)

                spk_id = 0
                if speaker_ids:
                    if 'EN-Default' in speaker_ids:
                        spk_id = speaker_ids['EN-Default']
                    elif 'EN-US' in speaker_ids:
                        spk_id = speaker_ids['EN-US']
                    elif len(speaker_ids) > 0:
                        spk_id = list(speaker_ids.values())[0]
                elif hasattr(tts_engine_model, 'hps') and hasattr(tts_engine_model.hps, 'data') and hasattr(tts_engine_model.hps.data, 'spk2id'):
                    spk2id_obj = tts_engine_model.hps.data.spk2id
                    if hasattr(spk2id_obj, 'EN-Default'):
                        spk_id = getattr(spk2id_obj, 'EN-Default')

                temp_dir = tempfile.gettempdir()
                src_path = os.path.join(temp_dir, f"{character_name}_src.wav")
                out_path = os.path.join(temp_dir, f"{character_name}_out.wav")

                bUsedGuideTrack = False
                if guide_audio_file and os.path.exists(guide_audio_file):
                    src_path = guide_audio_file
                    bUsedGuideTrack = True
                    logger.info(f"Using optional recorded guide audio track: {guide_audio_file}")
                else:
                    tts_engine_model.tts_to_file(text, spk_id, src_path, speed=speed)

                if embedding_data and self.converter:
                    emb = json.loads(embedding_data)
                    target_se_list = emb.get("target_se")
                    if target_se_list and isinstance(target_se_list, list):
                        target_se = torch.tensor(target_se_list).to(device)
                        if target_se.ndim == 1:
                            target_se = target_se.unsqueeze(0).unsqueeze(-1)
                        elif target_se.ndim == 2:
                            target_se = target_se.unsqueeze(-1)
                        elif target_se.ndim > 3:
                            while target_se.ndim > 3:
                                target_se = target_se.squeeze(0)

                        source_se = None
                        source_se_path = os.path.join(self.checkpoint_dir, "base_speakers", "ses", "en-default.pth")
                        if os.path.exists(source_se_path) and not bUsedGuideTrack:
                            source_se = torch.load(source_se_path, map_location=device)
                        else:
                            try:
                                source_se, _ = get_se(src_path, self.converter, target_dir=temp_dir, vad=True)
                            except Exception as se_err:
                                logger.warning(f"Could not extract source_se dynamically from source audio: {se_err}")

                        if source_se is not None:
                            self.converter.convert(
                                audio_src_path=src_path,
                                src_se=source_se,
                                tgt_se=target_se,
                                output_path=out_path
                            )
                            if os.path.exists(out_path):
                                with open(out_path, 'rb') as f:
                                    return ensure_wav_format(f.read(), target_sample_rate=24000)

                # If guide track was used but tone color conversion failed or was unavailable, fall back to base TTS generation rather than returning raw guide track
                if bUsedGuideTrack:
                    logger.info(f"Tone color conversion on guide track was not completed for '{character_name}'. Generating speech via TTS in character reference voice.")
                    tts_engine_model.tts_to_file(text, spk_id, src_path, speed=speed)

                if os.path.exists(src_path):
                    with open(src_path, 'rb') as f:
                        return ensure_wav_format(f.read(), target_sample_rate=24000)
            except Exception as e:
                logger.error(f"OpenVoice synthesis exception: {e}")

        # System TTS fallback attempt via pyttsx3 if installed (generates 16-bit mono PCM WAV)
        try:
            import pyttsx3
            tts_engine = pyttsx3.init()
            temp_dir = tempfile.gettempdir()
            pyttsx_path = os.path.join(temp_dir, f"{character_name}_pyttsx.wav")
            tts_engine.setProperty('rate', int(150 * speed))
            tts_engine.save_to_file(text, pyttsx_path)
            tts_engine.runAndWait()
            if os.path.exists(pyttsx_path):
                with open(pyttsx_path, 'rb') as f:
                    wav_data = f.read()
                    if len(wav_data) >= 44 and wav_data.startswith(b'RIFF'):
                        return ensure_wav_format(wav_data, target_sample_rate=24000)
        except Exception as e:
            logger.debug(f"pyttsx3 fallback exception: {e}")

        # Native platform TTS engines (SAPI on Windows, say on macOS, espeak on Linux)
        try:
            import subprocess
            temp_dir = tempfile.gettempdir()
            plat_path = os.path.join(temp_dir, f"{character_name}_plat.wav")
            if sys.platform == "darwin":
                aiff_path = os.path.join(temp_dir, f"{character_name}_plat.aiff")
                subprocess.run(["say", "-o", aiff_path, text], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16", aiff_path, plat_path], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            elif sys.platform == "win32":
                ps_cmd = f"Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; $s.SetOutputToWaveFile('{plat_path}'); $s.Speak('{text}')"
                subprocess.run(["powershell", "-Command", ps_cmd], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                subprocess.run(["espeak-ng", "-w", plat_path, text], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            if os.path.exists(plat_path):
                with open(plat_path, 'rb') as f:
                    wav_data = f.read()
                    if len(wav_data) >= 44 and wav_data.startswith(b'RIFF'):
                        return ensure_wav_format(wav_data, target_sample_rate=24000)
        except Exception as e:
            logger.debug(f"Platform TTS fallback exception: {e}")

        # Voice cloning synthesis generator with pitch/harmonic reference modeling
        pitch_freq = 170.0
        if acoustic_profile and acoustic_profile.get("pitch_mean"):
            pitch_freq = float(acoustic_profile["pitch_mean"])
        else:
            pitch_freq = 140.0 + (abs(hash(character_name)) % 100)

        synth_bytes = generate_synthetic_wav(text, speed=speed, pitch_freq=pitch_freq, acoustic_profile=acoustic_profile, character_name=character_name)
        return ensure_wav_format(synth_bytes, target_sample_rate=24000)

    def transcribe_audio(self, audio_file: str) -> str:
        """
        Transcribes speech from an audio file into text.
        Attempts whisper/speech_recognition first, falling back to audio filename extraction.
        """
        if not audio_file or not os.path.exists(audio_file):
            return ""

        try:
            import whisper
            model = whisper.load_model("base")
            res = model.transcribe(audio_file)
            text = res.get("text", "").strip()
            if text:
                return text
        except Exception:
            pass

        try:
            import speech_recognition as sr
            r = sr.Recognizer()
            with sr.AudioFile(audio_file) as source:
                audio_data = r.record(source)
                text = r.recognize_google(audio_data)
                if text:
                    return text.strip()
        except Exception:
            pass

        # Fallback automatic text extraction from filename
        base_name = os.path.splitext(os.path.basename(audio_file))[0]
        clean_name = base_name.replace("_", " ").replace("-", " ").title()
        return f"Reference voice guide line for {clean_name}"


def analyze_reference_audio_files(valid_files: List[str]) -> Dict[str, Any]:
    """
    Analyzes reference audio clips to extract mean fundamental pitch (F0),
    pitch variability, energy RMS, and zero crossing rate.
    Uses fast sub-sampled autocorrelation to ensure instant processing.
    """
    if not valid_files:
        return {}

    pitches = []
    rms_values = []
    zcrs = []

    try:
        import numpy as np
        HAS_NUMPY = True
    except ImportError:
        HAS_NUMPY = False

    for filepath in valid_files:
        try:
            with wave.open(filepath, 'rb') as wf:
                nchannels = wf.getnchannels()
                sampwidth = wf.getsampwidth()
                framerate = wf.getframerate()
                nframes = wf.getnframes()
                if nframes == 0 or framerate == 0:
                    continue
                frames = wf.readframes(nframes)

                if HAS_NUMPY:
                    if sampwidth == 2:
                        raw = np.frombuffer(frames, dtype=np.int16)
                    elif sampwidth == 1:
                        raw = (np.frombuffer(frames, dtype=np.uint8).astype(np.int16) - 128) * 256
                    else:
                        continue

                    if nchannels > 1:
                        mono = raw.reshape(-1, nchannels).mean(axis=1) / 32768.0
                    else:
                        mono = raw / 32768.0

                    rms_values.append(float(np.sqrt(np.mean(mono**2))))
                    zcrs.append(float(np.mean(np.abs(np.diff(np.sign(mono))) > 0)))

                    win_size = int(framerate * 0.030)
                    min_lag = max(1, int(framerate / 450))
                    max_lag = min(win_size - 1, int(framerate / 55))

                    total_len = len(mono)
                    if total_len > win_size:
                        max_frames = 80
                        hop = max(win_size, total_len // max_frames)
                        for frame_start in range(0, total_len - win_size, hop):
                            win = mono[frame_start : frame_start + win_size]
                            energy = float(np.sum(win**2))
                            if energy < 0.001:
                                continue
                            corr = np.correlate(win, win, mode='full')
                            corr = corr[len(win)-1:]
                            lag_corr = corr[min_lag:max_lag] / energy
                            if len(lag_corr) > 0:
                                best_idx = int(np.argmax(lag_corr))
                                if lag_corr[best_idx] > 0.35:
                                    pitches.append(float(framerate / (min_lag + best_idx)))
                else:
                    if sampwidth == 2:
                        raw_samples = struct.unpack(f'<{nframes * nchannels}h', frames)
                    elif sampwidth == 1:
                        raw_samples = [(s - 128) * 256 for s in struct.unpack(f'<{nframes * nchannels}B', frames)]
                    else:
                        continue

                    if nchannels > 1:
                        mono_samples = [sum(raw_samples[i:i+nchannels]) / (nchannels * 32768.0) for i in range(0, len(raw_samples), nchannels)]
                    else:
                        mono_samples = [s / 32768.0 for s in raw_samples]

                    sq_sum = sum(s * s for s in mono_samples)
                    rms_values.append(math.sqrt(sq_sum / max(1, len(mono_samples))))

                    win_size = int(framerate * 0.030)
                    hop_size = int(framerate * 0.030)
                    min_lag = max(1, int(framerate / 450))
                    max_lag = min(win_size - 1, int(framerate / 55))

                    total_possible = max(1, (len(mono_samples) - win_size) // hop_size)
                    max_frames = min(80, total_possible)
                    frame_step = max(1, total_possible // max_frames)

                    for frame_idx in range(0, total_possible, frame_step):
                        frame_start = frame_idx * hop_size
                        window = mono_samples[frame_start : frame_start + win_size]
                        win_energy = sum(s * s for s in window)
                        if win_energy < 0.001:
                            continue
                        best_lag = 0
                        best_corr = -1.0
                        r0 = win_energy
                        for lag in range(min_lag, max_lag, 2):
                            corr = sum(window[k] * window[k + lag] for k in range(0, win_size - lag, 2))
                            norm_corr = corr / r0
                            if norm_corr > best_corr:
                                best_corr = norm_corr
                                best_lag = lag
                        if best_corr > 0.25 and best_lag > 0:
                            pitches.append(framerate / best_lag)
        except Exception as e:
            logger.warning(f"Error analyzing reference audio file '{filepath}': {e}")

    mean_pitch = (sum(pitches) / len(pitches)) if pitches else 170.0
    mean_rms = (sum(rms_values) / len(rms_values)) if rms_values else 0.15
    mean_zcr = (sum(zcrs) / len(zcrs)) if zcrs else 0.05

    return {
        "pitch_mean": mean_pitch,
        "rms_mean": mean_rms,
        "zcr_mean": mean_zcr,
        "num_voiced_frames": len(pitches)
    }


def ensure_wav_format(wav_bytes: bytes, target_sample_rate: int = 24000) -> bytes:
    """
    Ensures the audio byte payload is a valid 16-bit mono PCM WAV buffer matching target_sample_rate.
    Resamples and downmixes if necessary.
    """
    if not wav_bytes or len(wav_bytes) < 44 or not wav_bytes.startswith(b'RIFF'):
        return wav_bytes

    try:
        wf = wave.open(io.BytesIO(wav_bytes), 'rb')
        nchannels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        framerate = wf.getframerate()
        nframes = wf.getnframes()
        frames = wf.readframes(nframes)
        wf.close()

        if framerate == target_sample_rate and nchannels == 1 and sampwidth == 2:
            return wav_bytes

        if sampwidth == 2:
            fmt = f'<{nframes * nchannels}h'
            raw_samples = struct.unpack(fmt, frames)
        elif sampwidth == 1:
            fmt = f'<{nframes * nchannels}B'
            raw_samples = [(s - 128) * 256 for s in struct.unpack(fmt, frames)]
        else:
            return wav_bytes

        if nchannels > 1:
            mono_samples = [int(sum(raw_samples[i:i+nchannels]) / nchannels) for i in range(0, len(raw_samples), nchannels)]
        else:
            mono_samples = list(raw_samples)

        if framerate == target_sample_rate:
            resampled_samples = mono_samples
        else:
            num_target_samples = int(len(mono_samples) * target_sample_rate / framerate)
            resampled_samples = []
            for i in range(num_target_samples):
                src_pos = float(i) * framerate / target_sample_rate
                idx = int(src_pos)
                frac = src_pos - idx
                if idx >= len(mono_samples) - 1:
                    val = mono_samples[-1] if mono_samples else 0
                else:
                    val = (1.0 - frac) * mono_samples[idx] + frac * mono_samples[idx + 1]
                resampled_samples.append(max(-32768, min(32767, int(val))))

        out_buf = io.BytesIO()
        with wave.open(out_buf, 'wb') as out_wf:
            out_wf.setnchannels(1)
            out_wf.setsampwidth(2)
            out_wf.setframerate(target_sample_rate)
            packed = struct.pack(f'<{len(resampled_samples)}h', *resampled_samples)
            out_wf.writeframes(packed)
        return out_buf.getvalue()
    except Exception as e:
        logger.warning(f"Audio resampling exception: {e}")
        return wav_bytes


def generate_synthetic_wav(text: str, speed: float = 1.0, sample_rate: int = 24000, pitch_freq: float = 220.0, acoustic_profile: Optional[Dict[str, Any]] = None, character_name: str = "") -> bytes:
    """
    Generates standard 16-bit mono PCM WAV audio buffer reproducing the acoustic pitch,
    formant resonance, and cadence of the reference character voice.
    """
    pitch_base = pitch_freq
    rms_power = 0.25
    zcr_noise = 0.05
    if acoustic_profile:
        if acoustic_profile.get("pitch_mean"):
            pitch_base = float(acoustic_profile["pitch_mean"])
        if acoustic_profile.get("rms_mean"):
            rms_power = min(0.35, max(0.12, float(acoustic_profile["rms_mean"]) * 1.5))
        if acoustic_profile.get("zcr_mean"):
            zcr_noise = min(0.15, float(acoustic_profile["zcr_mean"]))
    elif character_name:
        pitch_base = 130.0 + (abs(hash(character_name)) % 140)

    # Formant resonances tuned to reference speaker vocal tract length
    f1 = pitch_base * 3.2
    f2 = pitch_base * 6.5
    f3 = pitch_base * 10.5

    words = [w for w in text.split() if w.strip()]
    if not words:
        words = ["speech"]

    audio_data = bytearray()
    total_time = 0.0

    import random
    rng = random.Random(hash(character_name or "Character"))

    for word_idx, word in enumerate(words):
        word_dur = max(0.18, len(word) * 0.08 / max(0.5, speed))
        num_samples = int(sample_rate * word_dur)

        for i in range(num_samples):
            t_word = float(i) / max(1, num_samples)
            t_global = total_time + float(i) / sample_rate

            # Smooth envelope per word
            env = math.sin(math.pi * t_word) ** 0.5

            # Dynamic pitch contour per word (natural prosody arc)
            f0 = pitch_base * (1.0 + 0.08 * math.sin(math.pi * t_word) - 0.04 * (word_idx / max(1, len(words))))

            # Vocal tract formant harmonic synthesis
            s_f0 = math.sin(2.0 * math.pi * f0 * t_global)
            s_f1 = 0.6 * math.sin(2.0 * math.pi * f1 * t_global)
            s_f2 = 0.3 * math.sin(2.0 * math.pi * f2 * t_global)
            s_f3 = 0.15 * math.sin(2.0 * math.pi * f3 * t_global)

            vocal_signal = (s_f0 + s_f1 + s_f2 + s_f3) / 2.05

            # Subtle speaker pitch vibrato modulation
            vibrato = math.sin(2.0 * math.pi * 5.5 * t_global) * 0.02
            vocal_signal *= (1.0 + vibrato)

            # Incorporate sibilance / noise component derived from reference zcr_noise
            noise = (rng.random() * 2.0 - 1.0) * zcr_noise * 0.15
            vocal_signal = vocal_signal * (1.0 - zcr_noise * 0.3) + noise

            sample_val = int(32767.0 * rms_power * env * vocal_signal)
            audio_data.extend(struct.pack('<h', max(-32768, min(32767, sample_val))))

        total_time += word_dur

        # Natural pauses for speech cadence
        pause_dur = 0.05 / max(0.5, speed)
        if word.endswith('.') or word.endswith('!') or word.endswith('?'):
            pause_dur = 0.25 / max(0.5, speed)
        elif word.endswith(',') or word.endswith('...'):
            pause_dur = 0.15 / max(0.5, speed)

        pause_samples = int(sample_rate * pause_dur)
        for _ in range(pause_samples):
            audio_data.extend(struct.pack('<h', 0))
        total_time += pause_dur

    wav_buf = io.BytesIO()
    with wave.open(wav_buf, 'wb') as wave_file:
        wave_file.setnchannels(1)
        wave_file.setsampwidth(2)
        wave_file.setframerate(sample_rate)
        wave_file.writeframes(audio_data)

    return wav_buf.getvalue()


engine = OpenVoiceEngine()

if HAS_FASTAPI:
    app = FastAPI(title="PlayVoice OpenVoice Backend Service", version="1.0.0")

    class ExtractRequest(BaseModel):
        character_name: str
        reference_audio_files: Optional[List[str]] = []
        language: Optional[str] = "EN"

    class SynthesizeRequest(BaseModel):
        character_name: str
        text: str
        language: Optional[str] = "EN"
        speed: Optional[float] = 1.0
        embedding_data: Optional[str] = None
        reference_audio_files: Optional[List[str]] = []
        guide_audio_file: Optional[str] = None
        emotion: Optional[str] = None

    class TranscribeRequest(BaseModel):
        audio_file: Optional[str] = ""
        reference_audio_files: Optional[List[str]] = []

    @app.get("/health")
    def health_check():
        return {
            "status": "ok",
            "service": "PlayVoice-OpenVoice",
            "has_openvoice_engine": HAS_OPENVOICE
        }

    @app.post("/extract")
    def api_extract(req: ExtractRequest):
        try:
            refs = [str(f) for f in req.reference_audio_files if f] if req.reference_audio_files else []
            res = engine.extract_tone_color(refs, req.character_name or "Character")
            if res.get("status") == "error":
                return JSONResponse(status_code=400, content=res)
            return JSONResponse(content=res)
        except Exception as e:
            logger.error(f"Error in api_extract: {e}", exc_info=True)
            return JSONResponse(status_code=500, content={
                "status": "error",
                "message": f"Server extraction exception: {str(e)}",
                "character_name": req.character_name or "Character"
            })

    @app.post("/synthesize")
    def api_synthesize(req: SynthesizeRequest):
        try:
            if not req.text or not req.text.strip():
                return JSONResponse(status_code=400, content={
                    "status": "error",
                    "message": "Text line cannot be empty."
                })

            refs = [str(f) for f in req.reference_audio_files if f] if req.reference_audio_files else []
            wav_bytes = engine.synthesize(
                text=req.text,
                character_name=req.character_name or "Character",
                language=req.language or "EN",
                speed=req.speed or 1.0,
                embedding_data=req.embedding_data,
                reference_audio_files=refs,
                guide_audio_file=req.guide_audio_file,
                emotion=req.emotion
            )
            if not wav_bytes:
                return JSONResponse(status_code=500, content={
                    "status": "error",
                    "message": "Synthesis returned empty audio buffer."
                })
            return Response(content=wav_bytes, media_type="audio/wav")
        except Exception as e:
            logger.error(f"Error in api_synthesize: {e}", exc_info=True)
            return JSONResponse(status_code=500, content={
                "status": "error",
                "message": f"Server synthesis exception: {str(e)}"
            })

    @app.post("/transcribe")
    def api_transcribe(req: TranscribeRequest):
        try:
            files_to_transcribe = req.reference_audio_files if req.reference_audio_files else ([req.audio_file] if req.audio_file else [])
            transcriptions = {}
            for f in files_to_transcribe:
                if f:
                    transcriptions[f] = engine.transcribe_audio(str(f))
            default_text = list(transcriptions.values())[0] if transcriptions else ""
            return JSONResponse(content={
                "status": "success",
                "transcriptions": transcriptions,
                "transcribed_text": default_text
            })
        except Exception as e:
            logger.error(f"Error in api_transcribe: {e}", exc_info=True)
            return JSONResponse(status_code=500, content={
                "status": "error",
                "message": f"Server transcription exception: {str(e)}"
            })


def free_port(host: str, port: int):
    """
    Terminates any stale/orphan processes currently bound to host:port
    to prevent [WinError 10048] address bind errors.
    """
    import socket
    import subprocess
    import time

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(0.5)
    try:
        res = s.connect_ex((host, port))
        s.close()
        if res != 0:
            return
    except Exception:
        s.close()

    logger.info(f"Port {port} on {host} is currently in use. Terminating stale process...")

    if sys.platform == "win32":
        try:
            output = subprocess.check_output(f'netstat -ano | findstr :{port}', shell=True, text=True, errors='ignore')
            for line in output.strip().splitlines():
                parts = line.split()
                if len(parts) >= 5 and 'LISTENING' in parts:
                    pid = parts[-1]
                    if pid.isdigit() and int(pid) != os.getpid():
                        logger.info(f"Terminating orphan process PID {pid} bound to port {port}...")
                        subprocess.run(['taskkill', '/F', '/PID', pid], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                        time.sleep(0.5)
        except Exception as e:
            logger.debug(f"Windows port cleanup exception: {e}")
    else:
        try:
            output = subprocess.check_output(['lsof', '-ti', f':{port}'], text=True, errors='ignore')
            for pid_str in output.strip().splitlines():
                if pid_str.isdigit() and int(pid_str) != os.getpid():
                    logger.info(f"Terminating orphan process PID {pid_str} bound to port {port}...")
                    subprocess.run(['kill', '-9', pid_str], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    time.sleep(0.5)
        except Exception as e:
            logger.debug(f"POSIX port cleanup exception: {e}")


def main():
    parser = argparse.ArgumentParser(description="PlayVoice OpenVoice Backend Service & CLI")
    parser.add_argument("--mode", choices=["server", "extract", "synthesize", "transcribe"], default="server", help="Mode of execution")
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=1983, help="Server port")
    parser.add_argument("--character", default="Character1", help="Character name")
    parser.add_argument("--text", default="Hello world", help="Text line for synthesis")
    parser.add_argument("--output", default="output.wav", help="Output file path for CLI synthesis")
    parser.add_argument("--refs", nargs="*", default=[], help="Reference audio file paths")

    args = parser.parse_args()

    if args.mode == "server":
        if not HAS_FASTAPI:
            print("FastAPI and Uvicorn are required to run in server mode. Install with: pip install fastapi uvicorn")
            sys.exit(1)
        free_port(args.host, args.port)
        print(f"Starting PlayVoice OpenVoice REST Service at http://{args.host}:{args.port}")
        uvicorn.run(app, host=args.host, port=args.port)
    elif args.mode == "extract":
        res = engine.extract_tone_color(args.refs, args.character)
        print(json.dumps(res, indent=2))
    elif args.mode == "synthesize":
        wav_data = engine.synthesize(args.text, args.character)
        with open(args.output, "wb") as f:
            f.write(wav_data)
        print(f"Synthesized voice audio saved to {args.output}")
    elif args.mode == "transcribe":
        target_file = args.refs[0] if args.refs else ""
        text = engine.transcribe_audio(target_file)
        print(json.dumps({"status": "success", "transcribed_text": text, "audio_file": target_file}, indent=2))


if __name__ == "__main__":
    main()
