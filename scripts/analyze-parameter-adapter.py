"""Bounded disk-only PE observations for the experimental parameter-label backend.

Never imports/loads an AE DLL, opens a process, invokes AE, or enables an adapter.
The MSVC RTTI/known-getter checks are adapted from verse's MIT-licensed
After-Effects-Plugin-Localizer, commit dc5a591c78d67ba7b351c99e9f8222af6f160e4c.
See native/third_party/After-Effects-Plugin-Localizer.LICENSE.txt.
This deliberately narrow stdlib reader is not a disassembler or a QA certificate.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

MAX_MODULE_BYTES = 128 * 1024 * 1024
MAX_RTTI_SCAN_BYTES = 512 * 1024 * 1024


class PeImage:
    def __init__(self, data):
        if not isinstance(data, bytes) or not 0x100 <= len(data) <= MAX_MODULE_BYTES:
            raise ValueError('Invalid PE file size')
        self.data = data
        if data[:2] != b'MZ':
            raise ValueError('Missing DOS signature')
        pe = self.unpack('<I', 0x3c)[0]
        if pe < 0x40 or data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('Missing PE signature')
        machine, count, _, _, _, optional_size, _ = self.unpack('<HHIIIHH', pe + 4)
        optional = pe + 24
        if machine != 0x8664 or not 1 <= count <= 96 or optional_size < 120:
            raise ValueError('Expected bounded x64 PE32+ image')
        if self.unpack('<H', optional)[0] != 0x20b or optional + optional_size > len(data):
            raise ValueError('Invalid PE32+ optional header')
        self.base = self.unpack('<Q', optional + 24)[0]
        self.image_size = self.unpack('<I', optional + 56)[0]
        if not 0 < self.image_size <= 256 * 1024 * 1024:
            raise ValueError('Invalid image size')
        directory_count = self.unpack('<I', optional + 108)[0]
        if not 1 <= directory_count <= 16 or 112 + directory_count * 8 > optional_size:
            raise ValueError('Invalid PE data directories')
        self.export_rva, self.export_size = self.unpack('<II', optional + 112)
        if self.export_rva + self.export_size > self.image_size:
            raise ValueError('Export directory exceeds image')
        self.sections = []
        table = optional + optional_size
        header_end = table + count * 40
        if header_end > len(data):
            raise ValueError('Truncated section table')
        for index in range(count):
            name, virtual_size, rva, size, raw, _, _, _, _, flags = self.unpack('<8sIIIIIIHHI', table + index * 40)
            if rva + max(virtual_size, size) > self.image_size or raw + size > len(data) or (size and raw < header_end):
                raise ValueError('Invalid section range')
            section = {'name': name.rstrip(b'\0').decode('ascii', 'replace'), 'rva': rva, 'size': size, 'virtual_size': max(virtual_size, size), 'raw': raw, 'flags': flags}
            for previous in self.sections:
                if ((size and previous['size'] and max(raw, previous['raw']) < min(raw + size, previous['raw'] + previous['size'])) or
                        (section['virtual_size'] and previous['virtual_size'] and max(rva, previous['rva']) < min(rva + section['virtual_size'], previous['rva'] + previous['virtual_size']))):
                    raise ValueError('Overlapping section ranges')
            self.sections.append(section)

    def unpack(self, form, offset):
        if offset < 0 or offset + struct.calcsize(form) > len(self.data):
            raise ValueError('Truncated file record')
        return struct.unpack_from(form, self.data, offset)

    def section(self, rva, size=1):
        if not isinstance(rva, int) or not isinstance(size, int) or rva < 0 or size <= 0:
            raise ValueError('Invalid RVA span')
        for section in self.sections:
            if section['rva'] <= rva and rva + size <= section['rva'] + section['size']:
                return section
        raise ValueError('Span is not backed by one file section')

    def span(self, rva, size, *, executable=False):
        section = self.section(rva, size)
        if executable and not section['flags'] & 0x20000000:
            raise ValueError('Span is not executable code')
        raw = section['raw'] + rva - section['rva']
        return self.data[raw:raw + size]

    def rva_at(self, raw):
        for section in self.sections:
            if section['raw'] <= raw < section['raw'] + section['size']:
                return section['rva'] + raw - section['raw']
        raise ValueError('File offset is not backed by a section')

    def ascii_string(self, rva):
        section = self.section(rva)
        size = min(4096, section['rva'] + section['size'] - rva)
        raw = self.span(rva, size)
        end = raw.find(b'\0')
        if end < 0:
            raise ValueError('Unterminated export name')
        try:
            return raw[:end].decode('ascii')
        except UnicodeDecodeError as error:
            raise ValueError('Invalid export name') from error

    def exports(self):
        if not self.export_rva or self.export_size < 40:
            raise ValueError('Missing export directory')
        values = struct.unpack('<IIHHIIIIIII', self.span(self.export_rva, 40))
        function_count, name_count, functions, names, ordinals = values[6:]
        if not 0 < name_count <= function_count <= 100000:
            raise ValueError('Invalid export counts')
        fn = self.span(functions, function_count * 4)
        nm = self.span(names, name_count * 4)
        od = self.span(ordinals, name_count * 2)
        result = {}
        for index in range(name_count):
            ordinal = struct.unpack_from('<H', od, index * 2)[0]
            if ordinal >= function_count:
                raise ValueError('Invalid export ordinal')
            target = struct.unpack_from('<I', fn, ordinal * 4)[0]
            text = self.ascii_string(struct.unpack_from('<I', nm, index * 4)[0])
            if not text or text in result:
                raise ValueError('Empty or duplicate export name')
            if self.export_rva <= target < self.export_rva + self.export_size:
                continue  # Forwarded exports are not local code evidence.
            self.section(target)
            result[text] = target
        return result

    def primary_vtables(self, typename):
        # Original-file offsets remain distinct from RVAs and image-base VAs.
        needle = typename.encode('ascii') + b'\0'
        remaining = min(MAX_RTTI_SCAN_BYTES, len(self.data) * 64)
        def find(needle, start):
            nonlocal remaining
            # Conservative scanned-byte charge bounds even repeated negative
            # searches on adversarial RTTI lookalikes. No partial acceptance.
            cost = len(self.data) - start
            if cost > remaining:
                raise ValueError('RTTI scan budget exceeded')
            remaining -= cost
            return self.data.find(needle, start)
        result = set()
        position = 0
        while True:
            position = find(needle, position)
            if position < 0:
                break
            start = position
            position += 1
            try:
                descriptor = self.rva_at(start) - 16
                self.span(descriptor, len(needle) + 16)
            except ValueError:
                continue
            marker = struct.pack('<I', descriptor)
            at = 0
            while True:
                at = find(marker, at)
                if at < 0:
                    break
                raw_col = at - 12
                at += 1
                try:
                    col = self.rva_at(raw_col)
                    signature, offset, _, type_rva, _, self_rva = struct.unpack('<IIIIII', self.span(col, 24))
                    if signature != 1 or offset != 0 or self_rva != col or type_rva != descriptor:
                        continue
                except ValueError:
                    continue
                reference = struct.pack('<Q', self.base + col)
                ref = 0
                while True:
                    ref = find(reference, ref)
                    if ref < 0:
                        break
                    raw_vtable = ref + 8
                    ref += 1
                    try:
                        vtable = self.rva_at(raw_vtable)
                        section = self.section(vtable, 8)
                        if vtable % 8 or section['flags'] & 0x80000000:
                            continue
                        function = struct.unpack('<Q', self.span(vtable, 8))[0] - self.base
                        self.span(function, 1, executable=True)
                    except ValueError:
                        continue
                    result.add(vtable)
        return sorted(result)


def parameter_getters(pe):
    definitions = (
        ('?GetParent@TDB_StreamBase@@', 'parent_get'),
        ('?GetMatchName@TDB_StreamBase@@', 'stream_match_get'),
        ('?GetNumStreams@TDB_IndexedStreamGroup@@', 'indexed_count_get'),
        ('?GetNumStreams@TDB_NamedStreamGroup@@', 'count_get'),
        ('?GetDisplayName@TDB_StreamBase@@', 'display_get'),
        ('?SetDisplayName@TDB_StreamBase@@QEAAXAEBV?$basic_string@_W', 'display_set'),
    )
    symbols = pe.exports()
    fields, exports, missing = {}, [], []
    for prefix, field in definitions:
        found = [(name, rva) for name, rva in symbols.items() if name.startswith(prefix)]
        if not found or (len(found) != 1 and field != 'parent_get'):
            missing.append('No unique local export for ' + field)
            continue
        try:
            evidence = [(name, rva, pe.span(rva, 32, executable=True)) for name, rva in found]
        except ValueError as error:
            missing.append(field + ': ' + str(error))
            continue
        if field == 'parent_get' and any(raw[:5] != evidence[0][2][:5] for _, _, raw in evidence):
            missing.append('Parent overload getter bytes disagree')
            continue
        name, rva, raw = evidence[0]
        fields[field] = rva
        exports.append({'field': field, 'symbol': name, 'rva': rva, 'bytes': raw.hex()})
        if field == 'parent_get' and raw[:5] == bytes.fromhex('488b4120c3'):
            fields['parent_offset'] = 0x20
        elif field == 'stream_match_get' and raw[:5] == bytes.fromhex('488d4108c3'):
            fields['guid_offset'] = 8
        elif field == 'indexed_count_get' and raw[:3] == bytes.fromhex('488b81') and raw[7:10] == bytes.fromhex('482b81') and raw[14:19] == bytes.fromhex('48c1f803c3'):
            end, begin = struct.unpack_from('<I', raw, 3)[0], struct.unpack_from('<I', raw, 10)[0]
            if 0x80 <= begin <= 0x800 and begin % 8 == 0 and end == begin + 8:
                fields['group_vector'] = begin
        elif field == 'count_get' and raw[:2] == bytes.fromhex('8b81') and raw[6] == 0xc3:
            counter = struct.unpack_from('<I', raw, 2)[0]
            if 0x88 <= counter <= 0x808 and counter % 8 == 0:
                fields['named_map'] = counter - 8
        elif field == 'display_get' and raw[:3] == bytes.fromhex('488d81') and raw[7] == 0xc3:
            cache = struct.unpack_from('<I', raw, 3)[0]
            if 0x80 <= cache <= 0x800 and cache % 8 == 0:
                fields['display_cache'] = cache
    for field in ('parent_offset', 'guid_offset', 'group_vector', 'named_map', 'display_cache'):
        if field not in fields:
            missing.append('No exact instruction evidence for ' + field)
    return {'fields': fields, 'exports': exports, 'missing': missing}


def analyze(support):
    support = Path(support).absolute()
    for component in (support, *support.parents):
        if component.is_symlink() or component.is_junction():
            raise ValueError('Module ancestry must not contain reparse links')
    modules = {}
    images = {}
    for name in ('FLT.dll', 'TDB.dll', 'U.dll'):
        path = support / name
        if path.is_symlink() or path.is_junction() or not path.is_file() or not 0 < path.stat().st_size <= MAX_MODULE_BYTES:
            raise ValueError('Module missing, oversized or linked: ' + name)
        # Bounded read, even if a source is changed after stat. No loaded-image reads.
        with path.open('rb') as source:
            data = source.read(MAX_MODULE_BYTES + 1)
        images[name] = PeImage(data)
        modules[name] = {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}
    getters = parameter_getters(images['TDB.dll'])
    candidate = dict(getters['fields'])
    missing = list(getters['missing'])
    roots = images['FLT.dll'].primary_vtables('.?AVFLT_FCSeqSpec@@')
    if len(roots) == 1:
        candidate['root_vtable'] = roots[0]
    else:
        missing.append('FLT root primary vtable is not unique: ' + str(len(roots)))
    missing.extend([
        'Named-container family and ownership are not runtime verified',
        'Host string ABI and encoding are not runtime verified',
        'No owned-project write/restore, persistence, dirty, expression or rendering QA',
    ])
    return {'kind': 'xfx-parameter-static-observations', 'schemaVersion': 1,
            'runtimeEnabled': False, 'hostVerified': False, 'modules': modules,
            'candidateReadConfig': candidate, 'getterEvidence': getters['exports'],
            'rootCandidates': roots, 'missing': missing}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--support-files', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(analyze(args.support_files), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
