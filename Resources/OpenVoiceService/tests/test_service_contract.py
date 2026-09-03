import importlib.util
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


if __name__ == "__main__":
    unittest.main()
