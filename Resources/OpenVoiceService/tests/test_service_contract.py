import importlib.util
import io
import json
import os
import tempfile
import unittest
import wave


SERVICE_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "openvoice_service.py"))


def load_service():
    spec = importlib.util.spec_from_file_location("playvoice_openvoice_service", SERVICE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ServiceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.service = load_service()

    def setUp(self):
        self.previous_has_openvoice = self.service.HAS_OPENVOICE
        self.previous_engine = self.service.engine

    def tearDown(self):
        self.service.HAS_OPENVOICE = self.previous_has_openvoice
        self.service.engine = self.previous_engine

    def make_wav(self, directory, name="reference.wav"):
        path = os.path.join(directory, name)
        with wave.open(path, "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(24000)
            output.writeframes(b"\x00\x00" * 2400)
        return path

    def test_extraction_requires_openvoice_embedding(self):
        with tempfile.TemporaryDirectory() as directory:
            reference = self.make_wav(directory)
            engine = self.service.OpenVoiceEngine.__new__(self.service.OpenVoiceEngine)
            engine.converter = None
            self.service.HAS_OPENVOICE = False

            result = engine.extract_tone_color([reference], "TestHero")

            self.assertEqual(result["status"], "error")
            self.assertNotIn("embedding_data", result)

    def test_health_is_not_ready_without_openvoice_engine(self):
        self.service.HAS_OPENVOICE = False
        result = self.service.health_check()
        self.assertEqual(result.status_code, 503)
        payload = json.loads(result.body)
        self.assertNotEqual(payload["status"], "ok")
        self.assertFalse(payload["ready"])

    def test_deferred_engine_initialization_does_not_report_checkpoint_error(self):
        original_has_openvoice = self.service.HAS_OPENVOICE
        self.service.HAS_OPENVOICE = True
        try:
            engine = self.service.OpenVoiceEngine(load_existing=False)
            self.assertIsNone(engine.initialization_error)
            self.assertFalse(engine.ready)
        finally:
            self.service.HAS_OPENVOICE = original_has_openvoice

    def test_combined_reference_paths_are_unique(self):
        with tempfile.TemporaryDirectory() as directory:
            reference = self.make_wav(directory)
            self.service.HAS_OPENVOICE = False
            first = self.service.create_combined_reference_wav([reference])
            second = self.service.create_combined_reference_wav([reference])
            self.assertNotEqual(first, second)
            self.assertTrue(os.path.exists(first))
            self.assertTrue(os.path.exists(second))
            os.remove(first)
            os.remove(second)

    def test_short_guide_is_padded_for_source_embedding(self):
        with tempfile.TemporaryDirectory() as directory:
            guide = self.make_wav(directory, "guide.wav")
            padded = self.service.create_padded_embedding_wav(guide, directory)
            self.assertTrue(os.path.exists(padded))
            with wave.open(padded, "rb") as output:
                self.assertGreaterEqual(output.getnframes() / output.getframerate(), 6.0)

    def test_guide_requires_ready_openvoice_engine(self):
        with tempfile.TemporaryDirectory() as directory:
            guide = self.make_wav(directory, "guide.wav")
            engine = self.service.OpenVoiceEngine.__new__(self.service.OpenVoiceEngine)
            engine.converter = None
            engine.ready = False
            self.service.HAS_OPENVOICE = False

            with self.assertRaises(RuntimeError):
                engine.synthesize(
                    text="Hello",
                    character_name="TestHero",
                    embedding_data=json.dumps({"target_se": [1.0]}),
                    guide_audio_file=guide,
                )

    def test_synthesis_requires_openvoice_engine(self):
        engine = self.service.OpenVoiceEngine.__new__(self.service.OpenVoiceEngine)
        engine.converter = None
        engine.ready = False
        self.service.HAS_OPENVOICE = False

        with self.assertRaises(RuntimeError):
            engine.synthesize(text="Hello", character_name="TestHero")

    def test_missing_requested_guide_is_rejected(self):
        engine = self.service.OpenVoiceEngine.__new__(self.service.OpenVoiceEngine)
        engine.converter = None
        self.service.HAS_OPENVOICE = True

        with self.assertRaises(FileNotFoundError):
            engine.synthesize(
                text="Hello",
                character_name="TestHero",
                embedding_data=json.dumps({"target_se": [1.0]}),
                guide_audio_file="missing-guide.wav",
            )

    def test_synthesize_request_forwards_configured_sample_rate(self):
        if not self.service.HAS_FASTAPI:
            self.skipTest("FastAPI is unavailable")

        class CapturingEngine:
            def __init__(self):
                self.sample_rate = None
                self.improve_output = None

            def synthesize(self, **kwargs):
                self.sample_rate = kwargs["output_sample_rate"]
                self.improve_output = kwargs["improve_output"]
                return b"RIFF" + b"\x00" * 40

        previous_engine = self.service.engine
        capturing_engine = CapturingEngine()
        self.service.engine = capturing_engine
        try:
            request = self.service.SynthesizeRequest(
                character_name="TestHero",
                text="Hello",
                sample_rate=48000,
                improve_output=True,
            )
            self.service.api_synthesize(request)
            self.assertEqual(capturing_engine.sample_rate, 48000)
            self.assertTrue(capturing_engine.improve_output)

            request.improve_output = False
            self.service.api_synthesize(request)
            self.assertEqual(capturing_engine.sample_rate, 24000)
            self.assertFalse(capturing_engine.improve_output)
        finally:
            self.service.engine = previous_engine

    def test_invalid_output_sample_rate_is_rejected(self):
        engine = self.service.OpenVoiceEngine.__new__(self.service.OpenVoiceEngine)
        with self.assertRaises(ValueError):
            engine.synthesize(
                text="Hello",
                character_name="TestHero",
                output_sample_rate=0,
            )

    def test_output_sample_rate_is_applied_to_wav(self):
        input_buffer = self.service.generate_synthetic_wav("Hello", sample_rate=24000)
        output_buffer = self.service.ensure_wav_format(input_buffer, target_sample_rate=192000)

        with wave.open(io.BytesIO(input_buffer), "rb") as input_wav:
            input_duration = input_wav.getnframes() / input_wav.getframerate()
        with wave.open(io.BytesIO(output_buffer), "rb") as output_wav:
            self.assertEqual(output_wav.getframerate(), 192000)
            self.assertEqual(output_wav.getnchannels(), 1)
            self.assertEqual(output_wav.getsampwidth(), 2)
            self.assertAlmostEqual(
                output_wav.getnframes() / output_wav.getframerate(),
                input_duration,
                delta=1 / 24000,
            )


if __name__ == "__main__":
    unittest.main()
