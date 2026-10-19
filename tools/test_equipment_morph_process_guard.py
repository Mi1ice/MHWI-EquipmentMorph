"""Run the production process guard against isolated PowerShell query outcomes."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('guard_management', ROOT / 'tools/manage_equipment_morph_mod.py')
management = importlib.util.module_from_spec(spec)
spec.loader.exec_module(management)
real_run = subprocess.run
GAME = ROOT / '.build/process-guard-fixture'


class ProcessGuardTests(unittest.TestCase):
    def query(self, body):
        def run(arguments, **kwargs):
            arguments = list(arguments)
            arguments[-1] = 'function Get-CimInstance { ' + body + ' }; ' + arguments[-1]
            return real_run(arguments, **kwargs)
        return patch.object(management.subprocess, 'run', side_effect=run)

    def test_successful_empty_query(self):
        with self.query('return'):
            management.closed(GAME)

    def test_selected_game_running(self):
        with self.query('[pscustomobject]@{ExecutablePath=$env:MHWI_MORPH_GAME_EXE}'):
            with self.assertRaises(ValueError): management.closed(GAME)

    def test_other_game_directory(self):
        with self.query("[pscustomobject]@{ExecutablePath=($env:MHWI_MORPH_GAME_EXE+'.other')}"):
            management.closed(GAME)

    def test_unreadable_executable_path(self):
        with self.query('[pscustomobject]@{ExecutablePath=$null}'):
            with self.assertRaises(ValueError): management.closed(GAME)

    def test_nonterminating_query_error(self):
        with self.query("Write-Error 'Simulated query failure'"):
            with self.assertRaises(ValueError): management.closed(GAME)

    def test_terminating_query_error(self):
        with self.query("throw 'Simulated query failure'"):
            with self.assertRaises(ValueError): management.closed(GAME)

    def test_invalid_executable_path(self):
        with self.query('[pscustomobject]@{ExecutablePath=([string][char]0)}'):
            with self.assertRaises(ValueError): management.closed(GAME)

    def test_query_failure_stops_before_installation(self):
        report = dict(source=str(GAME / 'source'), game=str(GAME), conflicts=[])
        with patch.object(management, 'plan', return_value=report), patch.object(management, 'target', side_effect=AssertionError('installation reached after failed query')) as target:
            with self.query("Write-Error 'Simulated query failure'"):
                with self.assertRaises(ValueError): management.install(GAME / 'source', GAME, GAME / 'validator')
            target.assert_not_called()


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ProcessGuardTests))
    if not result.wasSuccessful(): sys.exit(1)
    print(f'PASS: {result.testsRun} process-guard checks; isolated query outcomes, no game files')
