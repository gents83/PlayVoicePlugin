# PlayVoice Plugin for Unreal Engine 5.8

**PlayVoice** is an Unreal Engine 5.8 plugin that uses an OpenVoice-v2 Python service during editor authoring to create language-specific voice models and pre-rendered SoundWaves. Packaged games play those generated SoundWaves without Python, HTTP, or runtime synthesis.

---

## Key Features

1. **CharacterVoice Asset (`UCharacterVoiceAsset`)**:
   - Manage character reference audio clips (WAV/MP3/FLAC) to capture tone, speed, and voice color.
   - Configure language, speed multiplier, and model embedding parameters.
   - Configure dialogue text lines for batch pre-processing.

2. **Zero-Latency Playback System**:
   - Pre-renders dialogue lines in advance (`PrecacheAllVoiceLines` or editor detail panel button).
   - Instantaneous playback during gameplay with zero delay or synthesis lag when triggering voice lines.

3. **OpenVoice Model Extraction & Synthesis**:
   - OpenVoice-v2 zero-shot voice cloning through the standalone FastAPI/Uvicorn backend.
   - Extracts a real speaker tone-color embedding for each configured language.
   - A recorded guide track supplies source prosody and emotion; the `emotion` text field is metadata only.
   - Model extraction and line rendering require a ready OpenVoice-v2 backend; fallback TTS is not a valid model.

4. **Blueprint Library & Details Customization**:
   - Simple Blueprint node: `PlayCharacterVoice` to play lines instantly.
   - Blueprint nodes: `PrecacheCharacterVoiceLines` and `GenerateVoiceSoundWave`.
   - Custom Editor Detail Panel (`FCharacterVoiceAssetCustomization`) for `CharacterVoice` assets in Unreal Editor with **"Generate OpenVoice Model"**, **"Generate Precached Sounds from VoiceLines"**, and cleanup controls.
   - Custom Settings Details Panel (`FPlayVoiceSettingsCustomization`) in **Project Settings -> Engine -> PlayVoice Settings** with **"Start OpenVoice Service"**, **"Check Requirements Status"**, and **"Launch Setup / Install Requirements"** buttons. The service starts only when one of these actions or a generation action needs it.

5. **Editor Authoring / Packaged Playback**:
   - The Python service and model-generation actions run in supported desktop editor environments.
   - Packaged targets use platform-supported `USoundWave` playback only; they do not launch Python or perform HTTP synthesis.

6. **Automated Testing & CI/CD**:
   - C++ Automation Unit Tests (`PlayVoiceAutomationTests.cpp`) testing asset caching, PCM/WAV conversion, and settings defaults.
   - GitHub Actions CI/CD workflow (`.github/workflows/ci.yml`) testing plugin structure, JSON syntax, and backend audio synthesis on Linux, Windows, and macOS.

---

## Directory Structure

```text
PlayVoicePlugin/
├── .github/
│   └── workflows/
│       └── ci.yml                          # GitHub Actions CI/CD pipeline
├── PlayVoicePlugin.uplugin                # Plugin descriptor with platform allow lists
├── Source/
│   ├── PlayVoicePlugin/                   # Runtime Module
│   │   ├── PlayVoicePlugin.Build.cs
│   │   ├── Public/
│   │   │   ├── CharacterVoiceAsset.h     # CharacterVoice Data Asset definition
│   │   │   ├── PlayVoiceSubsystem.h      # GameInstance subsystem managing TTS & caching
│   │   │   ├── PlayVoiceBlueprintLibrary.h# Blueprint Callable Nodes
│   │   │   ├── PlayVoiceAudioUtils.h     # Dynamic SoundWave PCM/WAV loader
│   │   │   └── PlayVoiceSettings.h       # Project Developer Settings
│   │   └── Private/
│   │       └── Tests/
│   │           └── PlayVoiceAutomationTests.cpp # C++ Unit & Automation Tests
│   └── PlayVoicePluginEditor/             # Editor Module
│       ├── PlayVoicePluginEditor.Build.cs
│       ├── Public/
│       │   ├── CharacterVoiceAssetCustomization.h # Asset Editor Detail Panel Customization
│       │   └── PlayVoiceSettingsCustomization.h   # Settings Editor Detail Panel Customization
│       └── Private/
└── Resources/
    └── OpenVoiceService/
        ├── openvoice_service.py           # OpenVoice REST Backend & CLI
        └── requirements.txt               # Python dependencies
```

---

## OpenVoice Model Choice

We selected **OpenVoice** (developed by MyShell.ai) as the primary open-source TTS model for this plugin for several reasons:
- **Zero-Shot Voice Cloning**: Only requires a few seconds of reference audio clips to extract tone color without full fine-tuning.
- **Tone Color Control**: Decouples tone color from base language and speech style.
- **Performance & Licensing**: Free, open-source license with fast inference speed.

---

## Setup Instructions

### 1. In-Editor Requirements Setup & Verification (Recommended)
1. Open your Unreal Engine 5.8 project containing the **PlayVoice** plugin.
2. Go to **Project Settings -> Engine -> PlayVoice Settings**.
3. Under **Service Setup**:
   - **Start OpenVoice Service**: Click this button to manually launch the local OpenVoice REST service backend from Unreal Editor.
   - The OpenVoice service is started on demand by **Start OpenVoice Service**, model extraction, or voice-line pre-rendering.
4. Under **Requirements Setup**, customize your setup preferences if desired:
   - **Python Executable Path**: Path to Python binary (default: `python`).
   - **Requirements File Path**: Path to requirements file (default: `Resources/OpenVoiceService/requirements.txt`).
   - **Target Installation Directory**: Leave empty. Dependencies are installed into the environment selected by **Python Executable Path**.
   - **Extra Pip Arguments**: Optional additional flags for `pip install` (e.g. `--upgrade`, `--no-cache-dir`).
5. Click **Check Requirements Status** to verify if all required Python packages are installed.
6. Click **Launch Setup / Install Requirements** to launch automated dependency installation.

### 2. Manual Python Backend Setup (Alternative)
Navigate to `Resources/OpenVoiceService` and install dependencies into the Python environment used by the editor:

```bash
python -m pip install -r requirements.txt
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
4. Configure service URL under **Project Settings -> Engine -> PlayVoice Settings** (default: `http://127.0.0.1:1983`).

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
3. Generation creates one SoundWave package for each `(language, key)` combination. Re-running it deletes and replaces the previous package, then saves the generated SoundWave and the `CharacterVoiceAsset` cache references.
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
3. Check `PlayVoice.UnitTests.CharacterVoiceAssetCaching`, `PlayVoice.UnitTests.AudioUtilsPCMAndWAVParsing`, and `PlayVoice.UnitTests.PlayVoiceSettingsDefaults`.
4. Click **Start Tests**.

---

## License

MIT License. See `LICENSE` for details.
