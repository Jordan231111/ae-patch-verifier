"""Transactional native engine publication on macOS and Linux.

Compilation/generation happens in a sibling tree. An atomic directory exchange
publishes the complete set; failed builds leave the current tree untouched.
"""
import ctypes
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


def manifest(directory):
    return {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(directory.rglob('*')) if p.is_file()}


def exchange(first, second):
    libc = ctypes.CDLL(None, use_errno=True)
    if sys.platform == 'darwin':
        operation, cwd = libc.renameatx_np, -2
    elif sys.platform.startswith('linux'):
        operation, cwd = libc.renameat2, -100
    else:
        raise RuntimeError('Atomic directory exchange is required; supported hosts are macOS and Linux')
    operation.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    operation.restype = ctypes.c_int
    if operation(cwd, os.fsencode(first), cwd, os.fsencode(second), 2) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(second))


class NativeStage:
    def __init__(self, target):
        self.target = pathlib.Path(target).resolve()
        self.before = manifest(self.target)
        self.temporary = tempfile.TemporaryDirectory(prefix='.native-staging-', dir=self.target.parent)
        self.directory = pathlib.Path(self.temporary.name) / 'native'
        shutil.copytree(self.target, self.directory)

    def publish(self):
        if manifest(self.target) != self.before:
            raise RuntimeError('Native sources changed during generation; refusing to overwrite those edits')
        exchange(self.directory, self.target)
        self.temporary.cleanup()  # after the swap, this contains the old tree


def build_current():
    root = pathlib.Path(__file__).resolve().parents[1]
    stage = NativeStage(root / 'native')
    subprocess.run(['bash', str(root / 'scripts/build-web-engine.sh')], check=True,
                   env=dict(os.environ, AE_NATIVE_DIR=str(stage.directory)))
    stage.publish()


if __name__ == '__main__':
    build_current()
