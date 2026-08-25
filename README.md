# PlayVoice Plugin for Unreal Engine 5.8

**PlayVoice** is a powerful plugin for **Unreal Engine 5.8** that integrates open-source **OpenVoice** zero-shot Text-To-Speech (TTS) voice cloning and synthesis into Unreal Engine Blueprints and C++.

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

---

## Directory Structure

```text
PlayVoicePlugin/
├── PlayVoicePlugin.uplugin
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
│   └── PlayVoicePluginEditor/             # Editor Module
│       ├── PlayVoicePluginEditor.Build.cs
│       ├── Public/
│       │   └── CharacterVoiceAssetCustomization.h # Editor Detail Panel Customization
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

### 1. Install Python Backend Dependencies
Navigate to `Resources/OpenVoiceService` and install dependencies:

```bash
cd Resources/OpenVoiceService
pip install -r requirements.txt
```

### 2. Start OpenVoice Service
Run the OpenVoice REST service:

```bash
python openvoice_service.py --mode server --host 127.0.0.1 --port 8000
```

### 3. Unreal Engine Project Setup
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

## License

MIT License. See `LICENSE` for details.
