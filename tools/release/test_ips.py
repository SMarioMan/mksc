"""IPS format edge cases, checked with a second existing IPS decoder."""
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ring'))
from restore_audio_assets import apply_ips
from ips import EOF_OFFSET, apply, create


class IPS(unittest.TestCase):
    def check(self, source, target):
        patch = create(source, target)
        self.assertEqual(apply(source, patch), target)
        self.assertEqual(apply_ips(source, patch), target)
        return patch

    def test_size_and_no_changes(self):
        self.assertEqual(self.check(b'abc', b'abc'), b'PATCHEOF')
        self.check(b'abcdef', b'ab')
        self.check(b'ab', b'ab\0\0\0')
        self.check(b'abc', b'')

    def test_run_and_record_boundaries(self):
        patch = self.check(bytes(140000), b'x' * 140000)
        self.assertLess(len(patch), 50)
        source = bytes(EOF_OFFSET + 65540)
        target = bytearray(source)
        target[EOF_OFFSET:EOF_OFFSET + 65537] = b'x' * 65537
        self.check(source, bytes(target))

    def test_sixteen_megabyte_image(self):
        self.check(b"abc", bytes(0x1000000))

    def test_varied_deltas(self):
        rng = random.Random(20261020)
        for _ in range(100):
            source = rng.randbytes(rng.randrange(4096))
            target = bytearray(source)
            for _ in range(20):
                start = rng.randrange(len(target) + 1)
                target[start:start + rng.randrange(32)] = rng.randbytes(rng.randrange(32))
            self.check(source, bytes(target))

    def test_reject_malformed(self):
        for patch in (b'', b'PATCH', b'PATCH\0\0\1\0\2x',
                      b'PATCH\0\0\1\0\0\0\0xEOF', b'PATCHEOFx'):
            with self.assertRaises(ValueError):
                apply(b'', patch)


if __name__ == '__main__':
    unittest.main()
