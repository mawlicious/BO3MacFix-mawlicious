"""ABI regression cases independent of the captured game descriptor corpus."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location(
    'generate_havok_schema', Path(__file__).parents[1] / 'tools/generate_havok_schema.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def member(name, kind, offset, flags=0):
    return dict(name=name, type=kind, subtype=0, offset=offset, flags=flags, count=0, cls='0x0')


def record(address, name, parent, size, members):
    return dict(address=address, name=name, parent=parent, size=size, version=0, members=members)


class SchemaTests(unittest.TestCase):
    def fixture(self):
        return [
            record('0x110000008', 'hkBaseObject', '0x0', 8, []),
            record('0x110000010', 'hkReferencedObject', '0x110000008', 16,
                   [member('memSizeAndRefCount', 8, 8, 1024)]),
            record('0x110000020', 'Derived', '0x110000010', 24,
                   [member('value', 11, 12), member('text', 33, 16)]),
        ]

    def test_base_tail_reuse_and_ignored_storage(self):
        generator = MODULE.Generator(self.fixture())
        generator.select('Derived')
        cls = generator.names['Derived']
        windows, mac = generator.layout(cls, False), generator.layout(cls, True)
        self.assertEqual(windows['size'], 32)
        self.assertEqual(mac['size'], 24)
        self.assertEqual([offset for _, offset in windows['fields']], [8, 16, 24])
        self.assertEqual([offset for _, offset in mac['fields']], [8, 12, 16])
        header = generator.render()
        self.assertIn('{16, 12, HKCONV_BYTES', header)
        self.assertIn('{24, 16, HKCONV_STRING', header)
        self.assertNotIn('/* memSizeAndRefCount */', header)

    def test_simd_alignment_amplifies_base_padding_difference(self):
        records = self.fixture()
        records[-1]['size'] = 32
        records[-1]['members'][-1] = member('up', 12, 16)
        generator = MODULE.Generator(records)
        generator.select('Derived')
        windows = generator.layout(generator.names['Derived'], False)
        self.assertEqual(windows['size'], 48)
        self.assertEqual(windows['fields'][-1][1], 32)

    def test_metadata_layout_mismatch_is_rejected(self):
        records = self.fixture()
        records[-1]['members'][0]['offset'] = 16
        generator = MODULE.Generator(records)
        generator.select('Derived')
        with self.assertRaisesRegex(ValueError, 'Mac layout mismatch Derived.value'):
            generator.render()

    def test_unsupported_serialized_type_is_rejected(self):
        records = self.fixture()
        records[-1]['members'][0]['type'] = 34
        generator = MODULE.Generator(records)
        generator.select('Derived')
        with self.assertRaisesRegex(ValueError, 'Unsupported type 34'):
            generator.render()


if __name__ == '__main__':
    unittest.main()
