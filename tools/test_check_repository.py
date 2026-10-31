"""Regression checks for the public-source scan, using synthetic trees only."""
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import check_repository


class RepositoryScanTests(unittest.TestCase):
    def setUp(self):
        parent = Path(__file__).resolve().parents[1] / '.build/repository-scan-tests'
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write(self, relative, content):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if isinstance(content, bytes) else content.encode('utf-8'))
        return path

    def scan(self):
        output = io.StringIO()
        with patch.object(check_repository, 'ROOT', self.root), contextlib.redirect_stdout(output):
            check_repository.main()
        return output.getvalue()

    def test_clean_archive_does_not_require_git(self):
        self.write('README.md', '[Module](module.py)')
        self.write('module.py', 'answer = 42\n')
        self.assertIn('2 public source files, 1 Python syntax checks', self.scan())

    def test_local_codegraph_metadata_is_excluded(self):
        self.write('README.md', 'Public source\n')
        for directory in ('.codegraph', 'nested/.codegraph'):
            with self.subTest(directory=directory):
                database = self.write(directory + '/codegraph.db', b'\x80\xff')
                try:
                    self.assertIn('1 public source files, 0 Python syntax checks', self.scan())
                finally:
                    database.unlink()

    def test_similarly_named_source_directory_is_still_checked(self):
        for directory in ('.codegraph-sources', '.superpowers-sources'):
            with self.subTest(directory=directory):
                path = self.write(directory + '/plugin.dll', b'synthetic')
                try:
                    with self.assertRaisesRegex(ValueError, 'Generated/asset/credential file'):
                        self.scan()
                finally:
                    path.unlink()

    def test_local_workflow_records_are_excluded(self):
        self.write('README.md', 'Public source\n')
        self.write('.superpowers/sdd/task/progress.md', 'C:' + '/' + 'Users' + '/Example/progress')
        self.assertIn('1 public source files, 0 Python syntax checks', self.scan())

    def test_forbidden_files_outside_metadata_are_still_rejected(self):
        for name in ('model.mod3', 'component.DLL', '.env.local'):
            with self.subTest(name=name):
                path = self.write(name, 'synthetic')
                try:
                    with self.assertRaisesRegex(ValueError, 'Generated/asset/credential file'):
                        self.scan()
                finally:
                    path.unlink()

    def test_private_patterns_are_rejected_without_disclosing_content(self):
        examples = ('C:' + '/' + 'Users' + '/Example/private', 'ghp_' + 'A' * 24)
        for text in examples:
            with self.subTest(kind='synthetic private pattern'):
                path = self.write('source.txt', text)
                try:
                    with self.assertRaisesRegex(ValueError, 'Private path or credential pattern') as error:
                        self.scan()
                    self.assertNotIn(text, str(error.exception))
                finally:
                    path.unlink()

    def test_invalid_source_is_not_silently_skipped(self):
        path = self.write('source.py', 'def broken(:\n')
        with self.assertRaises(SyntaxError):
            self.scan()
        path.unlink()
        self.write('source.txt', b'\x80\xff')
        with self.assertRaises(UnicodeDecodeError):
            self.scan()

    def test_broken_local_documentation_link_is_still_rejected(self):
        self.write('README.md', '[Missing](missing.md)')
        with self.assertRaisesRegex(ValueError, 'Broken local documentation link'):
            self.scan()


if __name__ == '__main__':
    unittest.main()
