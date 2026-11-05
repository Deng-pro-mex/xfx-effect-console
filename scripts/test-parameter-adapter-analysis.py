"""Offline PE/ABI candidate checks. These tests never load a DLL or contact AE."""
from pathlib import Path
import importlib.util
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("parameter_adapter_analysis", Path(__file__).with_name("analyze-parameter-adapter.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture(extra_symbols=()):
    data = bytearray(0x3400)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 0x84, 0x8664, 3, 0, 0, 0, 0xf0, 0x2022)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x20b)
    struct.pack_into('<Q', data, optional + 24, 0x180000000)
    struct.pack_into('<I', data, optional + 56, 0x4000)
    struct.pack_into('<I', data, optional + 108, 16)
    struct.pack_into('<II', data, optional + 112, 0x3000, 0x700)
    for i, (name, rva, raw, flags) in enumerate([
        (b'.text', 0x1000, 0x400, 0x60000020),
        (b'.data', 0x2000, 0x1400, 0xc0000040),
        (b'.rdata', 0x3000, 0x2400, 0x40000040),
    ]):
        at = optional + 0xf0 + i * 40
        struct.pack_into('<8sIIIIIIHHI', data, at, name, 0x1000, rva, 0x1000, raw, 0, 0, 0, 0, flags)
    def offset(rva):
        return (rva & 0xfff) + {1: 0x400, 2: 0x1400, 3: 0x2400}[rva >> 12]
    symbols = [
        ('?GetParent@TDB_StreamBase@@QEBAPEAV2@XZ', 0x1000, '488b4120c3'),
        ('?GetMatchName@TDB_StreamBase@@QEBAAEBVTDB_MatchName@@XZ', 0x1020, '488d4108c3'),
        ('?GetNumStreams@TDB_IndexedStreamGroup@@UEBAIXZ', 0x1040, '488b8128020000482b812002000048c1f803c3'),
        ('?GetNumStreams@TDB_NamedStreamGroup@@UEBAIXZ', 0x1060, '8b8130020000c3'),
        ('?GetDisplayName@TDB_StreamBase@@QEBAAEBVstring@@XZ', 0x1080, '488d81f0000000c3'),
        ('?SetDisplayName@TDB_StreamBase@@QEAAXAEBV?$basic_string@_W@std@@@Z', 0x10a0, '48895c2408574883ec20'),
    ]
    symbols.extend(extra_symbols)
    struct.pack_into('<IIHHIIIIIII', data, offset(0x3000), 0, 0, 0, 0, 0, 1, len(symbols), len(symbols), 0x3040, 0x3080, 0x30c0)
    name_rva = 0x3100
    for index, (name, rva, code) in enumerate(symbols):
        struct.pack_into('<I', data, offset(0x3040) + index * 4, rva)
        struct.pack_into('<I', data, offset(0x3080) + index * 4, name_rva)
        struct.pack_into('<H', data, offset(0x30c0) + index * 2, index)
        encoded = name.encode('ascii') + b'\0'
        data[offset(name_rva):offset(name_rva) + len(encoded)] = encoded
        name_rva += len(encoded)
        raw = bytes.fromhex(code)
        data[offset(rva):offset(rva) + len(raw)] = raw
    typename = b'.?AVFLT_FCSeqSpec@@\0'
    data[offset(0x3810):offset(0x3810) + len(typename)] = typename
    struct.pack_into('<IIIIII', data, offset(0x3880), 1, 0, 0, 0x3800, 0, 0x3880)
    struct.pack_into('<QQ', data, offset(0x38c0), 0x180003880, 0x180001000)
    return data


class AdapterAnalysisTests(unittest.TestCase):
    def test_exact_export_addresses_and_file_backed_bytes(self):
        pe = module.PeImage(bytes(fixture()))
        exports = pe.exports()
        self.assertEqual(exports['?GetParent@TDB_StreamBase@@QEBAPEAV2@XZ'], 0x1000)
        self.assertEqual(pe.span(0x1000, 5, executable=True).hex(), '488b4120c3')

    def test_only_getter_proven_fields_are_candidates(self):
        evidence = module.parameter_getters(module.PeImage(bytes(fixture())))
        self.assertEqual(evidence['fields']['parent_offset'], 0x20)
        self.assertEqual(evidence['fields']['guid_offset'], 8)
        self.assertEqual(evidence['fields']['group_vector'], 0x220)
        self.assertEqual(evidence['fields']['named_map'], 0x228)
        self.assertEqual(evidence['fields']['display_cache'], 0xf0)
        self.assertTrue(all(row['bytes'] for row in evidence['exports']))

    def test_unknown_instruction_bytes_do_not_inherit_a_known_build(self):
        data = fixture()
        data[0x400:0x405] = bytes.fromhex('488b4128c3')
        result = module.parameter_getters(module.PeImage(bytes(data)))
        self.assertNotIn('parent_offset', result['fields'])
        self.assertTrue(result['missing'])

    def test_primary_rtti_requires_exact_type_and_executable_target(self):
        pe = module.PeImage(bytes(fixture()))
        self.assertEqual(pe.primary_vtables('.?AVFLT_FCSeqSpec@@'), [0x38c8])
        self.assertEqual(pe.primary_vtables('.?AVOther@@'), [])
        data = fixture()
        struct.pack_into('<Q', data, 0x2cc8, 0x180002000)
        self.assertEqual(module.PeImage(bytes(data)).primary_vtables('.?AVFLT_FCSeqSpec@@'), [])

    def test_ambiguous_primary_vtable_is_not_selected(self):
        data = fixture()
        struct.pack_into('<QQ', data, 0x2d00, 0x180003880, 0x180001000)
        self.assertEqual(module.PeImage(bytes(data)).primary_vtables('.?AVFLT_FCSeqSpec@@'), [0x38c8, 0x3908])
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('FLT.dll', 'TDB.dll', 'U.dll'):
                (root / name).write_bytes(data)
            result = module.analyze(root)
            self.assertNotIn('root_vtable', result['candidateReadConfig'])
            self.assertTrue(any('unique' in text for text in result['missing']))

    def test_malformed_or_wrong_architecture_fails_closed(self):
        cases = [b'', b'MZ', bytes(1024)]
        for offset, value in [(0x84, b'\x4c\x01'), (0x98, b'\x0b\x01'), (0x3c, b'\xff' * 4)]:
            data = fixture()
            data[offset:offset + len(value)] = value
            cases.append(bytes(data))
        for raw in cases:
            with self.subTest(size=len(raw)):
                with self.assertRaises(ValueError):
                    module.PeImage(raw)

    def test_spans_cannot_read_zero_fill_cross_sections_or_data_as_code(self):
        pe = module.PeImage(bytes(fixture()))
        for rva, size, executable in [(0, 1, False), (0x1fff, 2, False), (0x4000, 1, False), (0x2000, 8, True), (0x1000, 0, True)]:
            with self.subTest(rva=rva, size=size):
                with self.assertRaises(ValueError):
                    pe.span(rva, size, executable=executable)

    def test_rtti_search_budget_prevents_unbounded_nested_scans(self):
        from unittest.mock import patch
        pe = module.PeImage(bytes(fixture()))
        with patch.object(module, 'MAX_RTTI_SCAN_BYTES', 1):
            with self.assertRaisesRegex(ValueError, 'budget'):
                pe.primary_vtables('.?AVFLT_FCSeqSpec@@')

    def test_virtual_mapping_overlap_is_not_hidden_by_nonoverlapping_raw_bytes(self):
        data = fixture()
        struct.pack_into('<I', data, 0x98 + 0xf0 + 8, 0x2000)
        with self.assertRaises(ValueError):
            module.PeImage(bytes(data))

    def test_zero_fill_tail_is_not_original_file_evidence(self):
        data = fixture()
        struct.pack_into('<I', data, 0x98 + 56, 0x5000)
        struct.pack_into('<I', data, 0x98 + 0xf0 + 2 * 40 + 8, 0x1800)
        pe = module.PeImage(bytes(data))
        with self.assertRaises(ValueError):
            pe.span(0x4000, 1)

    def test_getter_ambiguity_and_parent_overload_agreement(self):
        display = ('?GetDisplayName@TDB_StreamBase@@QEBAAEBVother_string@@XZ', 0x10c0, '488d81f0000000c3')
        result = module.parameter_getters(module.PeImage(bytes(fixture([display]))))
        self.assertNotIn('display_get', result['fields'])
        self.assertTrue(any('display_get' in text for text in result['missing']))
        parent = ('?GetParent@TDB_StreamBase@@QEAAPEAV2@XZ', 0x10c0, '488b4120c3')
        agreed = module.parameter_getters(module.PeImage(bytes(fixture([parent]))))
        self.assertEqual(agreed['fields']['parent_offset'], 0x20)
        changed = (*parent[:2], '488b4128c3')
        disagreed = module.parameter_getters(module.PeImage(bytes(fixture([changed]))))
        self.assertNotIn('parent_get', disagreed['fields'])
        self.assertNotIn('parent_offset', disagreed['fields'])

    def test_export_ordinals_and_forwarders_are_guarded(self):
        data = fixture()
        struct.pack_into('<H', data, 0x24c0, 0xffff)
        with self.assertRaises(ValueError):
            module.PeImage(bytes(data)).exports()
        data = fixture()
        struct.pack_into('<I', data, 0x2440, 0x3600)
        data[0x2a00:0x2a0c] = b'other.func\0\0'
        self.assertNotIn('?GetParent@TDB_StreamBase@@QEBAPEAV2@XZ', module.PeImage(bytes(data)).exports())

    def test_report_is_always_non_enabling_and_binds_all_module_hashes(self):
        import hashlib
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('FLT.dll', 'TDB.dll', 'U.dll'):
                (root / name).write_bytes(fixture())
            result = module.analyze(root)
            self.assertFalse(result['runtimeEnabled'])
            self.assertFalse(result['hostVerified'])
            self.assertEqual(set(result['modules']), {'FLT.dll', 'TDB.dll', 'U.dll'})
            self.assertEqual(result['modules']['FLT.dll']['sha256'], hashlib.sha256(fixture()).hexdigest())
            self.assertEqual(result['candidateReadConfig']['root_vtable'], 0x38c8)
            self.assertNotIn('enabled', result['candidateReadConfig'])
            self.assertTrue(result['missing'], 'runtime/encoding/expression QA must stay missing after static analysis')

    def test_missing_modules_and_reparse_sources_are_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                module.analyze(Path(folder))
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('FLT.dll', 'TDB.dll', 'U.dll'):
                (root / name).write_bytes(fixture())
            source = root / 'FLT.dll'
            original = root / 'original.dll'
            source.rename(original)
            try:
                source.symlink_to(original)
            except OSError:
                self.skipTest('host does not permit symlink creation')
            with self.assertRaises(ValueError):
                module.analyze(root)


if __name__ == '__main__':
    unittest.main(verbosity=2)
