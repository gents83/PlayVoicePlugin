# PlayVoice Plugin for Unreal Engine 5.8

**PlayVoice** is a powerful, cross-platform plugin for **Unreal Engine 5.8** that integrates open-source **OpenVoice** zero-shot Text-To-Speech (TTS) voice cloning and synthesis into Unreal Engine Blueprints and C++.

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
   - OpenVoice open-source zero-shot voice cloning framework support.
   - Standalone Python backend server (`openvoice_service.py`) using FastAPI and Uvicorn.
   - Extracts speaker tone color embedding vectors and performs flexible speech synthesis.

4. **Blueprint Library & Details Customization**:
   - Simple Blueprint node: `PlayCharacterVoice` to play lines instantly.
   - Blueprint nodes: `PrecacheCharacterVoiceLines` and `GenerateVoiceSoundWave`.
   - Custom Editor Detail Panel (`FCharacterVoiceAssetCustomization`) for `CharacterVoice` assets in Unreal Editor with **"Generate OpenVoice Model"** and **"Pre-process All Voice Lines"** buttons.
   - Custom Settings Details Panel (`FPlayVoiceSettingsCustomization`) in **Project Settings -> Engine -> PlayVoice Settings** with **"Start OpenVoice Service"**, **"Check Requirements Status"**, and **"Launch Setup / Install Requirements"** buttons, plus an option to automatically start the REST service on editor startup (`bAutoStartServiceOnEditorStartup`).

5. **Cross-Platform Readiness**:
   - Designed to run on **Windows (Win64), macOS, Linux, Android, iOS, PlayStation 5 (PS5), Xbox Series X/S, and Nintendo Switch**.
   - Uses standard platform-agnostic Unreal Engine memory and audio abstractions.

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
   - **Auto Start Service On Editor Startup**: Enable this option to automatically start the OpenVoice REST service whenever Unreal Editor opens.
4. Under **Requirements Setup**, customize your setup preferences if desired:
   - **Python Executable Path**: Path to Python binary (default: `python`).
   - **Requirements File Path**: Path to requirements file (default: `Resources/OpenVoiceService/requirements.txt`).
   - **Target Installation Directory**: Optional custom installation folder (`--target` parameter).
   - **Extra Pip Arguments**: Optional additional flags for `pip install` (e.g. `--upgrade`, `--no-cache-dir`).
5. Click **Check Requirements Status** to verify if all required Python packages are installed.
6. Click **Launch Setup / Install Requirements** to launch automated dependency installation.

### 2. Manual Python Backend Setup (Alternative)
Navigate to `Resources/OpenVoiceService` and install dependencies manually:

```bash
cd Resources/OpenVoiceService
pip install -r requirements.txt
```

### 3. Start OpenVoice Service
Run the OpenVoice REST service:

```bash
python openvoice_service.py --mode server --host 127.0.0.1 --port 8000
```

### 4. Unreal Engine Project Setup
1. Copy `PlayVoicePlugin` into your project's `Plugins/` folder.
2. Open your Unreal Engine 5.8 project.
3. Enable **PlayVoice Plugin** in **Edit -> Plugins**.
4. Configure service URL under **Project Settings -> Engine -> PlayVoice Settings** (default: `http://127.0.0.1:8000`).

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
To eliminate any latency during gameplay dialogue:
1. Add dialogue text strings to the **Lines To Preprocess** array in `DA_HeroVoice`.
2. Click **Pre-process All Voice Lines** in the Editor details panel (or call `PrecacheCharacterVoiceLines` in Blueprint on game startup).

### 4. Play Voice Lines in Blueprints
Use the `Play Character Voice` Blueprint node anywhere in your Blueprints:
- **Target Asset**: `DA_HeroVoice`
- **Text Line**: `"Hello brave adventurer!"`
- **Target Audio Component / Location**: Optional audio component or world location.

Because the voice line was precached, it will play **instantly with zero delay**.

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
