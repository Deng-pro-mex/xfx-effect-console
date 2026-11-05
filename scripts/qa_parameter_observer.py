"""Experimental injected-reader QA observer: reads candidates, NEVER writes AE.

No CLI, no import-time IO, and no process-opening code. A trusted operator must
first bind a fresh owned-project public snapshot to the actual PID/creation,
loaded modules and disk hashes, then take another fresh snapshot afterwards.
This module cannot establish ownership or authorize a later write. All reports
retain false runtime/host/parameter-association verification flags.

Layout candidates derive from the local 25.4 disk observations documented by
this project, not from a verified public ABI. Layout reference: MIT-licensed
After-Effects-Plugin-Localizer dc5a591c78d67ba7b351c99e9f8222af6f160e4c,
Copyright (c) 2026 verse. Full permission/warranty notice is distributed in
native/third_party/After-Effects-Plugin-Localizer.LICENSE.txt. No SDK opaque ref is dereferenced.
The circular list is only a candidate; rejected shape is NOT auto-adapted to a
second layout. Full declared read scope must finish, even after four hits.
"""
from dataclasses import dataclass
import json
from pathlib import PureWindowsPath
import re
import struct
import time

ROOT_VTABLE_RVA = 809488
MAX_ADDRESS = 0x7fffffffffff
EXPECTED_NAMES = ('XFX Source', 'XFX By Name', 'XFX By Match', 'XFX By Index')
HASHES = {
    'FLT':'42a2a0135d1e6c4225ca0e943aa48badbcd10c86507730b9ad40ebf71f03def7',
    'TDB':'b285572116480989486ba327c341d1d5c0e3c9c78465ea56c36c141de96a1aea',
    'U':'67021786017d1dc0b7014b5116416fc0fb95a4666a7ce90c40b23db861240c07'}
GETTERS = ((236992, bytes.fromhex('488b4120c3')), (236160, bytes.fromhex('488d4108c3')),
    (177008, bytes.fromhex('488b8128020000482b812002000048c1f803c3')),
    (177040, bytes.fromhex('8b8130020000c3')))


def _path(value):
    if not isinstance(value, str) or not re.fullmatch(r'[a-zA-Z]:[\\/][^\x00-\x1f]+', value) or ':' in value[2:]:
        raise ValueError('Invalid local absolute observed path')
    path = value.replace('\\', '/').lower()
    if any(piece in ('', '.', '..') for piece in path.split('/')[1:]):
        raise ValueError('Ambiguous observed path')
    return path


def verify_fresh_snapshot(reference, receipt, sample_id):
    """Compare data from the trusted operator's exclusive new receipt, not an auth API."""
    if not re.fullmatch('[a-f0-9]{32}', sample_id or '') or receipt.get('sampleId') != sample_id or receipt.get('ok') is not True:
        raise ValueError('Missing fresh snapshot challenge')
    if receipt.get('runtimeEnabled') is not False or receipt.get('hostVerified') is not False:
        raise ValueError('Unexpected enabling receipt')
    snapshot = receipt.get('snapshot')
    canonical = lambda value: json.dumps(value, sort_keys=True, ensure_ascii=True, allow_nan=False)
    if canonical(snapshot) != canonical(reference):
        raise ValueError('Owned public snapshot changed')
    if snapshot.get('kind') != 'xfx-parameter-qa-snapshot' or snapshot.get('schemaVersion') != 1 or not re.fullmatch('[a-f0-9]{32}', snapshot.get('nonce', '')):
        raise ValueError('Invalid owned snapshot schema')
    suffix = '/xfx-parameter-qa-' + snapshot['nonce'] + '/'
    if not any(_path(snapshot['project']['file']).endswith(suffix + name) for name in ('fixture.aep','reopened.aep')) or snapshot['project']['dirty'] is not False:
        raise ValueError('Not a clean owned QA file')
    return snapshot


def verify_host(reader, expected, observe_file):
    """Independently query actual loaded paths and read exact known getter bytes.

    observe_file is the trusted caller's pinned disk observer. No module loading,
    private function invocation, heap scan, or process-opening occurs here.
    """
    actual = reader.identity()
    if type(expected.get('pid')) is not int or expected.get('version') != '25.4x86' or expected.get('modules') != HASHES:
        raise ValueError('Not the disk-observed 25.4 build')
    if actual['pid'] != expected['pid'] or actual['creationTime'] != expected['creationTime'] or _path(actual['image']) != _path(expected['image']):
        raise ValueError('Actual process identity changed')
    if actual['processMachine'] != 0 or actual['nativeMachine'] != 0x8664 or PureWindowsPath(actual['image']).name.lower() != 'afterfx.exe':
        raise ValueError('Not native x64 AE')
    directory = _path(str(PureWindowsPath(actual['image']).parent))
    modules = {}
    for module in reader.modules():
        name = PureWindowsPath(module['path']).name.upper()
        if name not in ('FLT.DLL','TDB.DLL','U.DLL'):
            continue
        key = name[:-4]
        if key in modules or _path(str(PureWindowsPath(module['path']).parent)) != directory:
            raise ValueError('Duplicate or foreign loaded module path')
        pointer(module['base'])
        span(module['base'], module['size'])
        observed = observe_file(module['path'])
        if _path(observed['canonical']) != _path(module['path']) or observed['sha256'] != HASHES[key] or not 0 < observed['size'] <= 128*1024**2:
            raise ValueError('Module disk observation mismatch')
        modules[key] = dict(module, sha256=observed['sha256'], fileIdentity=observed['fileIdentity'])
    if set(modules) != set(HASHES):
        raise ValueError('Required module not actually loaded')
    tdb = modules['TDB']
    for rva, expected_bytes in GETTERS:
        if rva + len(expected_bytes) > tdb['size'] or reader.read(tdb['base'] + rva, len(expected_bytes)) != expected_bytes:
            raise ValueError('Runtime getter bytes do not match static candidate')
    if ROOT_VTABLE_RVA + 8 > modules['FLT']['size']:
        raise ValueError('Root vtable outside loaded image')
    return {'identity':actual, 'modules':modules}


@dataclass(frozen=True)
class Limits:
    max_bytes: int = 2 * 1024**3
    max_ms: int = 20000
    max_reads: int = 8192
    max_regions: int = 65536
    max_hits: int = 256
    max_groups: int = 64
    max_nodes: int = 4096


def span(address, size):
    if type(address) is not int or type(size) is not int or address <= 0 or size <= 0 or address > MAX_ADDRESS or size > MAX_ADDRESS - address:
        raise ValueError('Invalid or overflowing read span')


def pointer(address):
    span(address, 1)
    if address % 8:
        raise ValueError('Unaligned structural pointer')
    return address


class ObservationFailure(OSError):
    """A transport or consistency failure, never a shape rejection."""


class BudgetFailure(ObservationFailure):
    """A cooperative budget failure, never a shape rejection."""


class Budget:
    def __init__(self, reader, limits, clock):
        defaults = Limits()
        for field, value in vars(limits).items():
            if type(value) is not int or value <= 0 or value > getattr(defaults, field):
                raise ValueError('Invalid or raised QA budget')
        self.reader, self.limits, self.clock = reader, limits, clock
        self.start = self.last = clock()
        self.read_bytes = self.read_calls = self.regions = self.nodes = 0
        self.guards = []

    def check(self):
        now = self.clock()
        if now < self.last or (now - self.start) * 1000 > self.limits.max_ms:
            raise BudgetFailure('QA elapsed budget exhausted or clock moved backwards')
        self.last = now

    def read(self, at, size, guard=True):
        span(at, size)
        self.check()
        if size > 1024**2 or self.read_calls >= self.limits.max_reads or size > self.limits.max_bytes - self.read_bytes:
            raise BudgetFailure('QA byte/read budget exhausted')
        self.read_calls += 1
        self.read_bytes += size
        try:
            value = self.reader.read(at, size)
        except Exception as error:
            raise ObservationFailure('Reader failed: ' + str(error)) from error
        if not isinstance(value, bytes) or len(value) != size:
            raise ObservationFailure('Incomplete memory read')
        self.check()
        if guard:
            self.guards.append((at, value))
        return value

    def u64(self, at):
        pointer(at)
        return struct.unpack('<Q', self.read(at, 8))[0]

    def string(self, stream):
        cache = self.read(stream + 0xf0, 32)
        length, capacity = struct.unpack_from('<QQ', cache, 16)
        if length > capacity or capacity > 4096:
            raise ValueError('Unsupported UTF16 string length/capacity')
        storage = stream + 0xf0 if capacity <= 7 else pointer(struct.unpack_from('<Q', cache)[0])
        value = self.read(storage, (length + 1) * 2)
        if value[-2:] != b'\0\0':
            raise ValueError('Unterminated UTF16 string')
        text = value[:-2].decode('utf-16-le', errors='strict')
        if '\0' in text:
            raise ValueError('Embedded UTF16 terminator')
        return text

    def verify(self):
        for address, before in self.guards:
            if self.read(address, len(before), guard=False) != before:
                raise ObservationFailure('Candidate structure changed during observation')
        self.check()


def _scan(budget, vtable):
    maximum = budget.reader.max_address
    if type(maximum) is not int or not 0 < maximum <= MAX_ADDRESS:
        raise ValueError('Invalid reader maximum address')
    cursor, hits = 0, []
    needle = struct.pack('<Q', vtable)
    while cursor <= maximum:
        budget.check()
        region = budget.reader.region(cursor)
        budget.check()
        budget.regions += 1
        if budget.regions > budget.limits.max_regions:
            raise BudgetFailure('QA region budget exhausted')
        base, size = region['base'], region['size']
        if type(base) is not int or type(size) is not int or base < 0 or base > cursor or size <= 0 or size > MAX_ADDRESS - base or base + size <= cursor:
            raise ValueError('Nonprogressing or overflowing region enumeration')
        end = base + size
        if region['state'] == 0x1000 and region['type'] == 0x20000 and region['protect'] in (4, 8):
            # Guard/noaccess/executable/shared regions are excluded, not silently
            # failed reads. Scope is eligible private RW memory, NOT every page.
            at = max(base, cursor)
            at += (-at) % 8
            while at + 8 <= end:
                amount = min(1024**2, (end - at) // 8 * 8)
                data = budget.read(at, amount, guard=False)
                offset = data.find(needle)
                while offset >= 0:
                    if offset % 8 == 0:
                        if len(hits) >= budget.limits.max_hits:
                            raise BudgetFailure('QA root hit budget exhausted')
                        hits.append(at + offset)
                    offset = data.find(needle, offset + 1)
                at += amount
        cursor = end
    return hits


def _group(budget, parent, roots, vtable, diagnostic):
    if len(roots) != 4:
        raise ValueError('Candidate parade does not contain four scanned roots')
    begin, end = budget.u64(parent + 0x220), budget.u64(parent + 0x228)
    pointer(begin)
    if end < begin or end - begin != 32:
        raise ValueError('Candidate parade vector count differs')
    ordered = struct.unpack('<QQQQ', budget.read(begin, 32))
    if len(set(ordered)) != 4 or set(ordered) != set(roots):
        raise ValueError('Candidate vector membership differs')
    rows, root_guids = [], set()
    names = [budget.string(root) for root in ordered]
    diagnostic['rootNamesInVectorOrder'] = names
    diagnostic['observedChildren'] = []
    seen_children, seen_nodes = set(), set()
    for root in ordered:
        pointer(root)
        if budget.u64(root) != vtable or budget.u64(root + 0x20) != parent:
            raise ValueError('Root vtable or attached parent changed')
        root_guid = budget.read(root + 8, 16)
        if not any(root_guid):
            raise ValueError('Empty root raw GUID')
        root_guids.add(root_guid)
        head, count = pointer(budget.u64(root + 0x228)), budget.u64(root + 0x230)
        if not 1 <= count <= 16:
            raise ValueError('Unsupported named-container count')
        node, tail = struct.unpack('<QQ', budget.read(head, 16))
        previous = head
        for _ in range(count):
            budget.nodes += 1
            if budget.nodes > budget.limits.max_nodes:
                raise BudgetFailure('QA node budget exhausted')
            pointer(node)
            if node == head or node in seen_nodes:
                raise ValueError('Named-list cycle or alias')
            seen_nodes.add(node)
            raw = budget.read(node, 40)
            next_node, prev = struct.unpack_from('<QQ', raw)
            child = pointer(struct.unpack_from('<Q', raw, 32)[0])
            guid = raw[16:32]
            if prev != previous or not any(guid) or child in seen_children:
                raise ValueError('Named-list backlink GUID or child alias')
            seen_children.add(child)
            if budget.u64(child + 0x20) != root or budget.read(child + 8, 16) != guid:
                raise ValueError('Child GUID or attached parent mismatch')
            pf = budget.read(child + 0x40, 32)
            diagnostic['observedChildren'].append({'rootPtr':hex(root), 'childPtr':hex(child),
                'childRawGuidHex':guid.hex(), 'pfRaw32Hex':pf.hex(), 'namedContainerCount':count})
            if not 1 <= pf.find(b'\0') <= 31:
                raise ValueError('PF candidate name is missing bounded terminator')
            rows.append({'rootPtr':hex(root), 'childPtr':hex(child), 'rootRawGuidHex':root_guid.hex(),
                'childRawGuidHex':guid.hex(), 'pfRaw32Hex':pf.hex(), 'displayUtf16':budget.string(child)})
            previous, node = node, next_node
        if node != head or tail != previous:
            raise ValueError('Named-list sentinel or count mismatch')
    if len(root_guids) != 1 or tuple(names) != EXPECTED_NAMES:
        raise ValueError('Root raw GUID or public-name ordering is not correlated')
    return {'paradePtr':hex(parent), 'rootNamesInVectorOrder':names, 'rows':rows}


def observe_candidates(reader, flt_base, limits=None, clock=time.monotonic):
    result = {'ok':False, 'runtimeEnabled':False, 'hostVerified':False, 'parameterAssociationVerified':False,
        'structuralCandidatesOnly':True, 'qaOnlyPointersNotLexiconData':True, 'coverageComplete':False,
        'scannedScope':'committed private PAGE_READWRITE/PAGE_WRITECOPY', 'candidateGroups':[], 'groupDiagnostics':[], 'rejections':[]}
    budget = None
    try:
        pointer(flt_base)
        span(flt_base, ROOT_VTABLE_RVA + 8)
        budget = Budget(reader, limits or Limits(), clock)
        vtable = flt_base + ROOT_VTABLE_RVA
        hits = _scan(budget, vtable)
        groups = {}
        for root in hits:
            parent = budget.u64(root + 0x20)
            if not parent or parent % 8 or parent > MAX_ADDRESS:
                result['rejections'].append('Scanned hit has no aligned parent')
                continue
            groups.setdefault(parent, []).append(root)
            if len(groups) > budget.limits.max_groups:
                raise BudgetFailure('QA parade budget exhausted')
        candidates = []
        for parent, roots in groups.items():
            diagnostic = {'paradePtr':hex(parent), 'rootCount':len(roots), 'structuralOnly':True}
            result['groupDiagnostics'].append(diagnostic)
            try:
                candidates.append(_group(budget, parent, roots, vtable, diagnostic))
            except (ValueError, UnicodeError) as error:
                # Transport/clock/budget failures have independent OSError
                # types and cannot enter this shape-only rejection handler.
                result['rejections'].append(str(error)[:200])
        budget.verify()
        result['coverageComplete'] = True
        correlated = [group for group in result['groupDiagnostics'] if tuple(group.get('rootNamesInVectorOrder', ())) == EXPECTED_NAMES]
        if len(correlated) > 1:
            raise ValueError('No unique target: public-name-correlated aliases are ambiguous, including rejected child layouts')
        if len(candidates) != 1:
            raise ValueError('No unique public-name-correlated structural candidate')
        result['candidateGroups'] = candidates
        result['ok'] = True
    except (ValueError, OSError, UnicodeError, KeyError, struct.error) as error:
        result['error'] = str(error)[:512]
    if budget is not None:
        result['statistics'] = {'readBytes':budget.read_bytes, 'readCalls':budget.read_calls, 'regions':budget.regions, 'nodes':budget.nodes}
    return result
