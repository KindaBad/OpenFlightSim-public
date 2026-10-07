"""Installation must deduplicate app-local DLLs without hiding ABI conflicts."""
import subprocess
import tempfile
import unittest
from pathlib import Path

HELPER = Path(__file__).resolve().parents[1] / 'cmake/ResolveRuntimeConflicts.cmake'


class RuntimeDependencyTests(unittest.TestCase):
    def resolve(self, different=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            copies = [root / 'client/abseil_dll.dll', root / 'server/abseil_dll.dll']
            for index, copy in enumerate(copies):
                copy.parent.mkdir()
                copy.write_bytes(b'ABI-one' if not different or index == 0 else b'ABI-two')
            script = root / 'install.cmake'
            script.write_text(f'''include("{HELPER.as_posix()}")
set(conflicts_FILENAMES "abseil_dll.dll")
set(conflicts_abseil_dll.dll "{copies[0].as_posix()};{copies[1].as_posix()}")
set(dependencies "{copies[0].as_posix()}")
ofs_resolve_runtime_conflicts(dependencies conflicts)
list(LENGTH dependencies count)
if(NOT count EQUAL 1)
  message(FATAL_ERROR "DLL was not deduplicated")
endif()
file(INSTALL DESTINATION "{root.as_posix()}/stage" TYPE FILE FILES ${{dependencies}})
''')
            result = subprocess.run(['cmake', '-P', str(script)], capture_output=True, text=True)
            installed = (root / 'stage/abseil_dll.dll').is_file()
            return result, installed

    def test_identical_app_local_copies_install_once(self):
        result, installed = self.resolve()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(installed)

    def test_different_dll_contents_block_installation(self):
        result, installed = self.resolve(different=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Conflicting runtime DLL contents', result.stderr)
        self.assertFalse(installed)


if __name__ == '__main__':
    unittest.main()
