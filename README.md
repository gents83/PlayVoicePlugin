# PlayVoice Plugin for Unreal Engine 5.8

## Bring characters to life, locally

**PlayVoice is a local-first voice authoring workflow for Unreal Engine 5.8.** Turn a short, authorized reference recording into a reusable character voice, shape delivery with guide tracks, and ship localized dialogue as ordinary Unreal `USoundWave` assets.

No runtime Python. No runtime HTTP. No last-minute synthesis surprises. Author in the editor, review the result, and package the audio your game will actually play.

### From idea to playable dialogue

1. **Create a voice identity.** Add a `CharacterVoiceAsset` and define the character name, default language, and supported languages.
2. **Capture tone color.** Provide clean reference audio, then extract an OpenVoice-v2 model for each language.
3. **Direct the performance.** Add String Table keys and optional guide tracks for timing, cadence, emotion, and delivery.
4. **Render the library.** Generate language-aware SoundWaves, normalize their levels when needed, and review them in the editor.
5. **Connect gameplay.** Use the PlayVoice Blueprint nodes to resolve a character, String Table ID, key, and language into cached audio.

For the complete walkthrough, see [HOW_TO.md](HOW_TO.md).

### A voice pipeline built for iteration

- **Prototype faster:** Rewrite a line, regenerate its matching SoundWave, and hear the change without rebuilding a separate voice pipeline.
- **Localize with confidence:** Keep models, guide tracks, and cached lines organized by language instead of hiding localization in runtime logic.
- **Preserve performance:** Package authored SoundWaves for predictable playback with no synthesis latency during gameplay.
- **Direct delivery:** Use a recorded guide track to carry pacing and prosody into a generated line.
- **Stay in Unreal:** Configure Python requirements, launch the local OpenVoice service, create assets, generate audio, and validate cache references from the editor workflow.
- **Automate responsibly:** Use Blueprint playback nodes for fixed keys, localized text identifiers, or direct asset/key lookups.

### What you can build

- Character barks and reactive dialogue for gameplay.
- Localized conversations driven by String Tables.
- Cinematic and machinima performances with repeatable delivery.
- Temporary voices for narrative prototyping and previsualization.
- A production-ready, pre-rendered cast that stays independent of network access at runtime.

> **Responsible voice use:** Only clone or imitate voices when you have the speaker's consent and the rights required for your project. PlayVoice is intended for authorized voice replication, character prototyping, localization, machinima, cinematics, and interactive storytelling. Do not use it to impersonate people, mislead audiences, or create unauthorized replicas.

### Start with one line

Install the plugin, complete the Python 3.10 requirements setup, create `DA_HeroVoice`, add one clean reference recording, and generate a single `Hero_Greeting` line. Once that loop works, add languages, guide tracks, and the rest of the cast.

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

> **Complete authoring guide:** See [HOW_TO.md](HOW_TO.md) for the full CharacterVoiceAsset setup, parameter reference, button order, example, and visual walkthrough.

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
