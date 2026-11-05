"""Offline encoding tests only, no Windows API or AE process access."""
from pathlib import Path
import importlib.util
import sys
import unittest
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('encoding_guard',Path(__file__).with_name('qa_parameter_encoding.py'))
qa=importlib.util.module_from_spec(spec);sys.modules[spec.name]=qa;spec.loader.exec_module(qa)


class EncodingTests(unittest.TestCase):
    def test_observed_cp936_pf_bytes_must_not_be_called_utf8(self):
        raw=bytes.fromhex('bbacbfe9')+b'\0'*28
        result=qa.inspect_encoding(raw,'滑块')
        self.assertTrue(result['uniqueObservedEncoding']);self.assertEqual(result['matchingCandidates'],['cp936'])
        self.assertFalse(result['runtimeEnabled']);self.assertFalse(result['hostVerified']);self.assertFalse(result['writeAuthorized'])

    def test_ascii_original_cannot_discriminate_utf8_and_cp936(self):
        result=qa.inspect_encoding(b'Slider\0'+b'\0'*25,'Slider')
        self.assertFalse(result['uniqueObservedEncoding']);self.assertEqual(set(result['matchingCandidates']),{'cp936','utf-8'})

    def test_utf8_original_and_display_mismatch_are_distinct(self):
        raw='滑块'.encode('utf-8')+b'\0'*26
        self.assertEqual(qa.inspect_encoding(raw,'滑块')['matchingCandidates'],['utf-8'])
        self.assertEqual(qa.inspect_encoding(raw,'Other')['matchingCandidates'],[])

    def test_fixed_32_byte_buffer_and_nonempty_terminated_original_required(self):
        for raw in [b'',b'A'*31,b'A'*32,b'\0'*32,'Slider',b'A'*33]:
            with self.assertRaises(ValueError):qa.inspect_encoding(raw,'Slider')

    def test_nonzero_tail_is_preserved_not_normalized_into_restore_data(self):
        raw=b'Slider\0'+b'X'*25
        result=qa.inspect_encoding(raw,'Slider')
        self.assertEqual(result['originalRaw32Hex'],raw.hex())

    def test_prepared_native_bytes_have_explicit_codec_and_strict_roundtrip(self):
        result=qa.prepare_label_bytes('源数值','cp936')
        expected='源数值'.encode('cp936')
        self.assertEqual(result['nativeRaw32Hex'],(expected+b'\0'*(32-len(expected))).hex())
        self.assertEqual(result['nativeByteCount'],6);self.assertEqual(result['utf8ByteCount'],9)
        self.assertTrue(result['previewOnly']);self.assertFalse(result['writeAuthorized'])
        self.assertEqual(qa.prepare_label_bytes('源数值','utf-8')['nativeByteCount'],9)

    def test_no_lossy_fallback_bestfit_or_unknown_codec(self):
        for text,codec in [('😀','cp936'),('x','mbcs'),('x','ascii'),('x','utf-16-le'),('x',None)]:
            with self.assertRaises(ValueError):qa.prepare_label_bytes(text,codec)

    def test_utf8_portability_and_native_length_limits_are_independent(self):
        self.assertEqual(qa.prepare_label_bytes('a'*31,'cp936')['nativeByteCount'],31)
        for text in ['a'*32,'中'*11,'😀'*8]:
            with self.assertRaises(ValueError):qa.prepare_label_bytes(text,'utf-8')

    def test_controls_lone_surrogates_empty_and_nonstring_refused(self):
        for text in ['',None,123,'bad\0name','bad\nname','bad\tname','bad\x7fname','\ud800','\udfff']:
            with self.assertRaises(ValueError):qa.prepare_label_bytes(text,'utf-8')


if __name__=='__main__':unittest.main(verbosity=2)
