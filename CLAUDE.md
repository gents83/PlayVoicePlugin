# PlayVoicePlugin

## Scope

PlayVoicePlugin is an Unreal Engine 5.8 plugin for editor-time OpenVoice-v2 voice authoring.

- `PlayVoicePlugin` is the Runtime module. Packaged builds play authored `USoundWave` assets only.
- `PlayVoicePluginEditor` owns Python service startup, model extraction, guide-track rendering, and SoundWave pre-rendering.
- `Resources/OpenVoiceService/openvoice_service.py` is the local FastAPI/Uvicorn backend.
- Packaged builds must not launch Python, call HTTP, or synthesize missing lines.

## Key files

- `Source/PlayVoicePlugin/Public/CharacterVoiceAsset.h` and `Private/CharacterVoiceAsset.cpp`: language/reference data, embedding persistence, guide paths, and language-aware SoundWave caches.
- `Source/PlayVoicePlugin/Private/PlayVoiceSubsystem.cpp`: playback and editor-gated request compatibility.
- `Source/PlayVoicePluginEditor/Private/CharacterVoiceAssetCustomization.cpp`: editor model extraction and `(language, key)` batch rendering.
- `Source/PlayVoicePluginEditor/Private/PlayVoicePluginEditorModule.cpp`: service process launch and resource-path resolution.
- `Resources/OpenVoiceService/openvoice_service.py`: OpenVoice-v2 extraction, guide conversion, REST API, and CLI.
- `Resources/OpenVoiceService/requirements.txt`: dependencies installed into the interpreter configured in PlayVoice Settings.
- `Source/PlayVoicePlugin/Private/Tests/PlayVoiceAutomationTests.cpp`: Unreal automation coverage.
- `Resources/OpenVoiceService/tests/`: Python service contract tests.

## Workflow

1. Set **Python Executable Path** to the virtual-environment interpreter containing OpenVoice and MeloTTS.
2. Install `Resources/OpenVoiceService/requirements.txt` with that same interpreter. Do not use `TargetInstallDir`; the launched service does not support a separate pip target.
3. Start the service from Project Settings or trigger a generation action; the service is started on demand. `/health` is ready only after OpenVoice-v2 imports and converter checkpoints load.
4. Configure reference audio per language and click **Generate OpenVoice Model**.
5. Configure String Table keys in `Voice Lines`; record optional guide tracks; click **Pre-process All Voice Lines** before packaging.
6. Verify generated SoundWaves for every required `(language, key)` combination in the asset and reload the asset before packaging.

Manual service command:

```bash
python Resources/OpenVoiceService/openvoice_service.py --mode server --host 127.0.0.1 --port 1983
```

Python tests:

```bash
python -m unittest discover -s Resources/OpenVoiceService/tests -v
```

Run Unreal automation tests from Session Frontend by filtering for `PlayVoice.UnitTests`. Build the Unreal Editor target with the normal Hurricane project build workflow.

## Non-obvious rules

- Send canonical absolute filesystem paths to Python. Relative guide paths must be resolved against the project and asset recording folders before `/synthesize`.
- A guide track is the source speech signal for prosody and emotion transfer. The `emotion` request field is metadata; arbitrary emotion labels are not interpreted by OpenVoice.
- `/extract` succeeds only with a real nonempty OpenVoice-v2 `target_se`. Acoustic-profile or fallback TTS data is not a model.
- `ModelCheckpointPath` points to the JSON embedding persisted by `SaveModelToFile`; the service must not report a nonexistent `.pth` artifact.
- SoundWave cache lookup is language-specific. Never replace a legacy single pointer without migrating it to the default-language entry.
- A requested guide conversion failure must be reported; do not silently substitute unrelated fallback TTS.
- Do not copy inference code from `D:/PROJECTS/mgentile/speechtospeech`: its committed converter uses obsolete OpenVoice-v1 APIs, although its explicit reference/guide data flow is useful as a UI comparison.
- `PrecacheCharacterVoiceLines` cannot create audio in a packaged build. Generation is an editor authoring action.

## Debugging

Check, in order:

1. The editor's configured Python interpreter and `python -m pip` environment.
2. `/health` response and service logs for import/checkpoint readiness.
3. Absolute reference and guide paths and whether each file exists.
4. The extraction response's `status`, `embedding_data`, and nonempty `target_se`.
5. The generated package name and language-specific cache entry.

Do not terminate arbitrary processes to free the service port. Stop only the service process owned by the editor/plugin.

Keep README setup instructions, requirements, settings UI, service launch arguments, HTTP response contracts, and this file synchronized when changing the workflow.
