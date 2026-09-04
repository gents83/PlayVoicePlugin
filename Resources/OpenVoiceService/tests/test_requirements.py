import importlib.util
import os
import unittest


CHECKER_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "check_requirements.py"))


def load_checker():
    spec = importlib.util.spec_from_file_location("playvoice_check_requirements", CHECKER_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RequirementsPreflightTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checker = load_checker()

    def test_python_3_10_is_supported(self):
        self.assertTrue(self.checker.check_python_compatibility((3, 10)))

    def test_python_3_14_is_rejected(self):
        self.assertFalse(self.checker.check_python_compatibility((3, 14)))

    def test_exact_requirement_is_parsed(self):
        self.assertEqual(self.checker._parse_requirement("numpy==1.22.0"), ("numpy", "1.22.0"))

    def test_git_requirement_is_parsed_without_git_suffix(self):
        self.assertEqual(
            self.checker._parse_requirement("git+https://github.com/myshell-ai/OpenVoice.git"),
            ("openvoice", None),
        )


if __name__ == "__main__":
    unittest.main()
