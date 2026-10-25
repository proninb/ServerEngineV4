import json
import pathlib
import unittest
from fetch_symbols import validate


class PublicPdbValidation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        repo = pathlib.Path(__file__).resolve().parents[2]
        manifest = repo / 'build/runtime-shm-faults-01/symbols/manifest.json'
        if not manifest.exists():
            raise unittest.SkipTest('Fetch matching OS symbols first')
        cls.kernel = next(r for r in json.loads(manifest.read_text()) if r['module'] == 'ntoskrnl')

    def test_real_public_pdb(self):
        k = self.kernel
        identity = validate(pathlib.Path(k['pdb']), k['guid'], k['image_age'])
        self.assertEqual(identity['dbi_age'], k['image_age'])
        self.assertGreaterEqual(identity['age'], k['image_age'])

    def test_wrong_guid_rejected(self):
        k = self.kernel
        with self.assertRaises(AssertionError):
            validate(pathlib.Path(k['pdb']), '00000000-0000-0000-0000-000000000000', k['image_age'])

    def test_wrong_dbi_age_rejected(self):
        k = self.kernel
        with self.assertRaises(AssertionError):
            validate(pathlib.Path(k['pdb']), k['guid'], k['image_age'] + 1)


if __name__ == '__main__':
    unittest.main()
