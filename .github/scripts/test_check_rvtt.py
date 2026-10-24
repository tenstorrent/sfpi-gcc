import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("check_rvtt", Path(__file__).with_name("check-rvtt.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CheckSummaryTest(unittest.TestCase):
    clean = """PASS: g++.target/riscv/tt/tensix/example.C (test for excess errors)
XFAIL: g++.target/riscv/tt/tensix/expected.C scan-assembler
\t\t=== g++ Summary ===
# of expected passes\t1
# of expected failures\t1
"""

    def test_clean_and_expected_failure(self):
        self.assertEqual(module.check_summary(self.clean, 0), [])

    def test_truncated_summary(self):
        self.assertTrue(module.check_summary(self.clean.split("===")[0], 0))

    def test_inconsistent_count(self):
        self.assertTrue(module.check_summary(self.clean.replace("passes\t1", "passes\t2"), 0))

    def test_unexpected_results(self):
        for kind in ("FAIL", "XPASS", "UNRESOLVED", "UNTESTED", "ERROR"):
            with self.subTest(kind=kind):
                self.assertTrue(module.check_summary(f"{kind}: unexpected\n" + self.clean, 0))

    def test_empty_suite(self):
        self.assertTrue(module.check_summary("\t=== g++ Summary ===\n", 0))

    def test_duplicate_summary(self):
        self.assertTrue(module.check_summary(self.clean + self.clean, 0))

    def test_runner_failure(self):
        self.assertTrue(module.check_summary(self.clean, 2))


if __name__ == "__main__":
    unittest.main()
