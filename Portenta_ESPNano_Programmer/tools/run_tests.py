#!/usr/bin/env python3
"""Run the project unittest suite with a concise pass-count summary."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class SummaryTestRunner(unittest.TextTestRunner):
    def run(self, test: unittest.TestSuite) -> unittest.TestResult:
        result = super().run(test)
        not_passed = (
            len(result.failures)
            + len(result.errors)
            + len(result.skipped)
            + len(result.expectedFailures)
            + len(result.unexpectedSuccesses)
        )
        passed = result.testsRun - not_passed
        self.stream.writeln()
        self.stream.writeln(f"{passed} of {result.testsRun} PASSED")
        return result


def main() -> int:
    suite = unittest.defaultTestLoader.discover(str(REPO_ROOT / "tests"))
    result = SummaryTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
