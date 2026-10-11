"""Independent backend selection and package command contracts."""

from argparse import Namespace
from itertools import product
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from backend_profiles import BACKENDS, PROFILES, selected_backends
import check_packages


class BackendProfileTests(unittest.TestCase):
    def test_profiles_cover_each_independent_selection_exactly_once(self):
        expected = {tuple(backend for backend, enabled in zip(BACKENDS, flags) if enabled)
                    for flags in product((False, True), repeat=4)}
        actual = [selected_backends(profile) for profile in PROFILES]
        self.assertEqual(len(actual), 16)
        self.assertEqual(set(actual), expected)
        self.assertEqual(selected_backends("all"), BACKENDS)

    def test_invalid_or_duplicate_backends_are_rejected(self):
        for profile in ("", "both", "sqlite+sqlite", "mysql+odbc", "core+mysql"):
            with self.subTest(profile=profile), self.assertRaises(ValueError):
                selected_backends(profile)
        self.assertEqual(selected_backends("mariadb+mysql"), ("mysql", "mariadb"))

    def test_conan_receives_four_explicit_options_for_every_subset(self):
        for profile in PROFILES:
            args = Namespace(backends=profile, conan_profile="test", conan_setting=[], conan_conf=[])
            with self.subTest(profile=profile), patch.object(check_packages, "run") as run:
                check_packages.check_conan(args, {"version": "0.5.0"}, Path("work"))
                command = run.call_args.args
                selected = selected_backends(profile)
                for backend, option in zip(BACKENDS, ("sqlite3", "postgresql", "mysql", "mariadb")):
                    self.assertIn(f"orm-cxx/*:with_{option}={backend in selected}", command)


if __name__ == "__main__":
    unittest.main()
