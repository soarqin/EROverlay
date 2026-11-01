"""Reference exports use committed bytes, never uncommitted checkout contents."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from export_cpp_reference import export_reference


class ReferenceExportTest(unittest.TestCase):
    def test_pinned_export_and_destination_guards(self) -> None:
        root = Path(__file__).resolve().parents[1]
        (root / "target").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root / "target", prefix="reference-test-") as temporary:
            temporary = Path(temporary).resolve()
            self.assertTrue(temporary.is_relative_to(root / "target"))
            repository = temporary / "repository"
            repository.mkdir()

            def git(*args: str) -> str:
                return subprocess.check_output(["git", "-C", str(repository), *args], text=True).strip()

            git("init", "--quiet")
            (repository / "source.txt").write_bytes(b"committed\n")
            git("add", "source.txt")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                "commit", "--quiet", "-m", "reference fixture")
            commit = git("rev-parse", "HEAD")
            (repository / "source.txt").write_bytes(b"uncommitted\n")
            output = temporary / "export"
            self.assertEqual(export_reference(repository, commit, output), commit)
            self.assertEqual((output / "source.txt").read_bytes(), b"committed\n")
            with self.assertRaises(FileExistsError):
                export_reference(repository, commit, output)
            with self.assertRaises(ValueError):
                export_reference(repository, commit, root / "source-outside-target")
            self.assertFalse((root / "source-outside-target").exists())
            self.assertEqual(git("status", "--porcelain"), "M source.txt")


if __name__ == "__main__":
    unittest.main()
