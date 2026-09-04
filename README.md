# PlayVoice Plugin for Unreal Engine 5.8

## Bring Your Characters to Life

Give every character a voice that feels memorable, expressive, and truly their own. **PlayVoice** brings OpenVoice-v2 voice cloning into Unreal Engine authoring, so you can turn a short, clean reference recording into a reusable character voice and generate localized dialogue directly from your project.

Create a gruff hero, a bright sidekick, a mysterious narrator, or an entire cast of original voices. You can also reproduce a recognizable or famous person's voice **only when you have their permission and the necessary rights**. PlayVoice is designed for authorized voice replication, character prototyping, localization, machinima, cinematics, and interactive storytelling.

### From Voice Sample to In-Game Dialogue

1. Add a few reference recordings for a character.
2. Extract a language-specific voice model in the Unreal Editor.
3. Add your dialogue lines and optional guide tracks for timing, emotion, and delivery.
4. Pre-render the lines into SoundWave assets.
5. Play them instantly in gameplay through Blueprint.

The result is production-friendly dialogue that is authored ahead of time, packaged with your game, and ready to play without runtime synthesis or network calls.

### Why Creators Use PlayVoice

- **Distinctive character voices:** Capture tone color from a short reference recording instead of commissioning a complete voice library for every iteration.
- **Expressive delivery:** Use guide tracks to transfer the pacing, emotion, and prosody of a performance into generated dialogue.
- **Localization at scale:** Build separate voice models and cached lines for each supported language.
- **Fast iteration:** Rewrite a line, regenerate its SoundWave, and hear the result in the editor without rebuilding a voice pipeline.
- **Gameplay-ready output:** Ship pre-rendered Unreal SoundWaves with predictable, zero-synthesis-latency playback.
- **Local authoring:** Run the OpenVoice service locally during editor-time creation, keeping packaged builds independent from Python and HTTP.

> **Responsible use:** Only clone or imitate voices when you have the speaker's consent and the rights to use the recordings. Do not use PlayVoice to impersonate people, mislead audiences, or create unauthorized replicas.

---

## Technical Overview

**PlayVoice** is an Unreal Engine 5.8 plugin that uses an OpenVoice-v2 Python service during editor authoring to create language-specific voice models and pre-rendered SoundWaves. Packaged games play those generated SoundWaves without Python, HTTP, or runtime synthesis.

### Key Features

1. **CharacterVoice Asset (`UCharacterVoiceAsset`)**:
   - Manage character reference audio clips in WAV, MP3, or FLAC format.
   - Capture tone color from reference recordings and persist language-specific embeddings.
   - Configure language, speed multiplier, output sample rate, and model parameters.
   - Organize dialogue text lines by String Table ID, key, and language.

2. **Zero-Latency Playback System**:
   - Plays authored SoundWave assets during gameplay.
   - Pre-render dialogue in the editor instead of synthesizing during play.
   - Resolve the matching character, String Table key, and language-specific cache entry through Blueprint.

3. **OpenVoice Model Extraction and Synthesis**:
   - Use zero-shot voice cloning through a standalone FastAPI/Uvicorn backend.
   - Extract a real OpenVoice-v2 speaker tone-color embedding for each configured language.
   - Use a recorded guide track as the source for prosody and emotion transfer.
   - Keep inference and reference processing at OpenVoice's native 24 kHz, with optional final output resampling.

4. **Blueprint Library and Editor Customization**:
   - Use `PlayCharacterVoice` to play cached lines from Blueprint.
   - Use `PrecacheCharacterVoiceLines` and `GenerateVoiceSoundWave` for authoring workflows.
   - Generate models and batches of localized lines from the `CharacterVoice` details panel.
   - Configure service startup, dependency checks, and Python setup from Project Settings.

5. **Editor Authoring and Packaged Playback**:
   - Run Python, model extraction, and line rendering in supported desktop editor environments.
   - Package only authored `USoundWave` assets for runtime playback.
   - Keep Python, HTTP, and synthesis out of packaged targets.

6. **Automated Testing and CI/CD**:
   - C++ Automation Unit Tests cover asset caching, PCM/WAV conversion, normalization, settings, Blueprint behavior, and String Table integration.
   - GitHub Actions validate plugin structure, JSON syntax, and Python service contracts on Linux and Windows.

---

## OpenVoice Model Choice

We selected **OpenVoice** (developed by MyShell.ai) as the primary open-source voice model for this plugin because it provides:

- **Zero-shot voice cloning:** Extract tone color from a short reference recording without full model fine-tuning.
- **Tone color control:** Separate a speaker's voice identity from language and speech delivery.
- **Fast local inference:** Author voice lines through a local service rather than a hosted runtime dependency.
- **Open-source availability:** Review the upstream model's license and usage terms before distributing a product that uses it.

---

## Directory Structure

```text
PlayVoicePlugin/
├── PlayVoicePlugin.pkg.json
├── .github/
│   └── workflows/
│       └── ci.yml                          # GitHub Actions CI/CD pipeline
├── PlayVoicePlugin.uplugin                # Plugin descriptor with platform allow lists
├── Source/
│   ├── PlayVoicePlugin/                   # Runtime Module
│   │   ├── PlayVoicePlugin.Build.cs
│   │   ├── Public/
│   │   │   ├── CharacterVoiceAsset.h     # CharacterVoice Data Asset definition
│   │   │   ├── PlayVoiceSubsystem.h      # GameInstance subsystem managing playback and caching
│   │   │   ├── PlayVoiceBlueprintLibrary.h# Blueprint-callable nodes
│   │   │   ├── PlayVoiceAudioUtils.h     # Dynamic SoundWave PCM/WAV loader
│   │   │   └── PlayVoiceSettings.h       # Project Developer Settings
│   │   └── Private/
│   │       └── Tests/
│   │           └── PlayVoiceAutomationTests.cpp # C++ unit and automation tests
│   └── PlayVoicePluginEditor/             # Editor Module
│       ├── PlayVoicePluginEditor.Build.cs
│       ├── Public/
│       │   ├── CharacterVoiceAssetCustomization.h # Asset Editor detail panel customization
│       │   └── PlayVoiceSettingsCustomization.h   # Settings detail panel customization
│       └── Private/
└── Resources/
    └── OpenVoiceService/
        ├── openvoice_service.py           # OpenVoice REST backend and CLI
        └── requirements.txt               # Python dependencies
```

---

## Setup Instructions

### 1. In-Editor Requirements Setup & Verification (Recommended)
1. Create or select a fresh Python **3.10.x** virtual environment. The current OpenVoice dependency pins require Python 3.10; Python 3.11+ is not supported by this requirements set.
2. Open your Unreal Engine 5.8 project containing the **PlayVoice** plugin.
3. Go to **Project Settings -> Project -> PlayVoice Settings**.
4. Under **Service Setup**:
   - **Start OpenVoice Service**: Click this button to manually launch the local OpenVoice REST service backend from Unreal Editor.
   - The OpenVoice service is started on demand by **Start OpenVoice Service**, model extraction, or voice-line pre-rendering.
5. Under **Requirements Setup**, click **Install Python 3.10** if the required interpreter is not installed; the button downloads the official Python 3.10.11 Windows installer. Run the downloaded installer, leave the executable path empty for automatic discovery or select the new executable, then rerun setup.
6. Customize your setup preferences if desired:
   - **Python Executable Path**: Optional Python 3.10 executable. Leave empty for automatic Windows `py.exe -3.10` discovery.
   - Setup creates/reuses `Resources/OpenVoiceService/.venv` and saves its verified Python executable path.
   - **Requirements File Path**: Path to requirements file (default: `Resources/OpenVoiceService/requirements.txt`).
   - **Extra Pip Arguments**: Optional additional flags for `pip install` (e.g. `--upgrade`, `--no-cache-dir`).
7. Click **Check Requirements Status** to verify if all required Python packages are installed.
8. Click **Launch Setup / Install Requirements** to launch automated dependency installation.

### 2. Manual Python Backend Setup (Alternative)
Navigate to `Resources/OpenVoiceService`. Use the verified interpreter saved by the plugin (on Windows, `.venv/Scripts/python.exe`) to install dependencies manually if needed:

```bash
.venv/Scripts/python.exe -m pip install -r requirements.txt
```

The first server start downloads the OpenVoice-v2 converter checkpoints when they are missing. Keep the editor's **Python Executable Path** pointed at this same environment.

### 3. Start OpenVoice Service
Run the OpenVoice REST service:

```bash
python openvoice_service.py --mode server --host 127.0.0.1 --port 1983
```

### 4. Unreal Engine Project Setup
1. Copy `PlayVoicePlugin` into your project's `Plugins/` folder.
2. Open your Unreal Engine 5.8 project.
3. Enable **PlayVoice Plugin** in **Edit -> Plugins**.
4. Configure service URL under **Project Settings -> Project -> PlayVoice Settings** (default: `http://127.0.0.1:1983`). Enable **Improve Output Quality** to resample final generated SoundWaves to **Default Sample Rate** (enabled by default; default rate: `48000` Hz / 48 kHz). When disabled, output remains at OpenVoice's native 24 kHz. OpenVoice model inference and reference processing always remain at native 24 kHz.

---

## Blueprint Usage & Workflow

### 1. Create a `CharacterVoice` Asset
1. In the Content Browser, right-click -> **Miscellaneous -> Data Asset**.
2. Select **CharacterVoiceAsset**.
3. Name your asset (e.g. `DA_HeroVoice`).

### 2. Add Reference Audio & Extract Model
1. Open `DA_HeroVoice`.
2. In **Reference Audio Files**, add paths to sample WAV audio files for your character.
3. In the **OpenVoice Model Actions** section of the details panel, click **Generate OpenVoice Model**.

### 3. Ensure Zero-Delay Playback (Pre-processing)
To eliminate latency during gameplay dialogue:
1. Add String Table keys to the **Voice Lines** array in `DA_HeroVoice` and record optional guide tracks for prosody/emotion.
2. Click **Generate Precached Sounds from VoiceLines** in the Editor details panel after generating a model for every language.
3. Generation creates one SoundWave package for each `(language, key)` combination. Re-running it deletes and replaces the previous package, then saves the generated SoundWave and the `CharacterVoiceAsset` cache references. Generated output uses the configured **Default Sample Rate**; OpenVoice processing remains at native 24 kHz.
4. The **Clean Precached Sound Waves** action removes generated packages and clears their references. The Blueprint precache node only reports already-authored cache entries and does not run Python in a packaged game.

### 4. Play Voice Lines in Blueprints
Use **Get String Table ID and Key from Text** on the localized text, then connect its outputs to the new `Play Character Voice` node:
- **Character Name**: the `CharacterName` value from the voice asset, such as `Rayman`.
- **String Table Id**: the node’s `String Table Id` output.
- **Key**: the node’s `Key` output.
- **Language Code**: the generated language, such as `EN`.
- **Target Audio Component / Location**: optional audio component or world location.

The node resolves the matching `CharacterVoiceAsset`, String Table, key, and language-specific precached SoundWave. It never starts Python or performs runtime synthesis.

---

## Running Automation Unit Tests

In Unreal Editor:
1. Open **Tools -> Session Frontend** (or `Tools -> Automation`).
2. Filter for `PlayVoice`.
3. Filter for `PlayVoice.UnitTests` to run the complete asset, cache, audio, normalization, settings, Blueprint, and String Table coverage.
4. Click **Start Tests**.

---

## License

MIT License. See `LICENSE` for details.
