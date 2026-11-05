"""Offline FakeMemory tests only. No process-opening code or AE interaction."""
from pathlib import Path
import copy
import importlib.util
import struct
import sys
import unittest

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('qa_parameter_observer', Path(__file__).with_name('qa_parameter_observer.py'))
qa = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = qa
spec.loader.exec_module(qa)


class Memory:
    def __init__(self):
        self.max_address = 0x9fff
        self.data = bytearray(0xa000)
        self.calls = []
        self.clock = 0
        self.short = False
        self.fail = False
        self.stale = False
        self.regions = [dict(base=0, size=0x1000, state=0x10000, type=0, protect=1),
            dict(base=0x1000, size=0x8000, state=0x1000, type=0x20000, protect=4),
            dict(base=0x9000, size=0x1000, state=0x10000, type=0, protect=1)]
        self.base = 0x180000000
        self.roots = [0x2000, 0x3000, 0x4000, 0x5000]
        self.q(0x1220, 0x1800)
        self.q(0x1228, 0x1820)
        for i, root in enumerate(self.roots):
            self.q(0x1800+i*8, root)
            self.q(root, self.base+qa.ROOT_VTABLE_RVA)
            self.raw(root+8, b'R'*16)
            self.q(root+0x20, 0x1000)
            self.string(root, qa.EXPECTED_NAMES[i], root+0xa00)
            head, node, child = root+0x600, root+0x680, root+0x800
            self.q(root+0x228, head)
            self.q(root+0x230, 1)
            self.q(head, node)
            self.q(head+8, node)
            self.q(node, head)
            self.q(node+8, head)
            self.raw(node+16, b'C'*16)
            self.q(node+32, child)
            self.raw(child+8, b'C'*16)
            self.q(child+0x20, root)
            self.raw(child+0x40, b'Slider\0'+b'\0'*25)
            self.string(child, '滑块', 0)

    def q(self, address, value):
        self.raw(address, struct.pack('<Q', value))

    def raw(self, address, data):
        self.data[address:address+len(data)] = data

    def string(self, address, text, heap):
        data = text.encode('utf-16-le')+b'\0\0'
        if len(text)>7:
            self.q(address+0xf0, heap)
            self.raw(heap, data)
            capacity = len(text)
        else:
            self.raw(address+0xf0, data)
            capacity = 7
        self.q(address+0x100, len(text))
        self.q(address+0x108, capacity)

    def read(self, address, size):
        self.calls.append((address,size))
        if self.fail:
            raise OSError('ReadProcessMemory failed')
        value=bytes(self.data[address:address+size])
        if self.short:
            return value[:-1]
        if self.stale and len(self.calls)>40 and address==self.roots[0]:
            return b'\0'*size
        return value

    def region(self, address):
        self.calls.append(('region',address))
        for region in self.regions:
            if region['base']<=address<region['base']+region['size']:
                return copy.copy(region)
        raise OSError('Query failed, not normal exhaustion')

    def now(self):
        return self.clock


def observe(memory=None, **limits):
    memory=memory or Memory()
    before=bytes(memory.data)
    result=qa.observe_candidates(memory,memory.base,qa.Limits(**limits),memory.now)
    assert bytes(memory.data)==before, 'Observer wrote memory'
    return result


class Host:
    def __init__(self):
        self.expected={'pid':123,'creationTime':'1'*16,'image':r'C:\AE\AfterFX.exe','version':'25.4x86','modules':qa.HASHES.copy()}
        self.actual=dict(self.expected,processMachine=0,nativeMachine=0x8664)
        self.mods=[dict(base=0x180000000+i*0x1000000,size=0x200000,path='C:/AE/'+key+'.dll') for i,key in enumerate(qa.HASHES)]
        self.reads=[];self.bad_hash=False;self.short=False
    def identity(self):return self.actual.copy()
    def modules(self):return copy.deepcopy(self.mods)
    def file(self,path):return dict(canonical=path,sha256=('f'*64 if self.bad_hash else qa.HASHES[Path(path).stem]),size=1000,fileIdentity='pinned-test-file')
    def read(self,at,size):
        self.reads.append((at,size));base=self.mods[1]['base']
        for rva,value in qa.GETTERS:
            if at==base+rva and size==len(value):return value[:-1] if self.short else value
        raise AssertionError('Read outside compiled getter whitelist')


class ObserverTests(unittest.TestCase):
    def test_host_verification_uses_actual_loaded_paths_and_exact_getters(self):
        host=Host();report=qa.verify_host(host,host.expected,host.file)
        self.assertEqual(set(report['modules']),set(qa.HASHES));self.assertEqual(len(host.reads),4)
        for mutate in [lambda h:h.actual.update(pid=124),lambda h:h.actual.update(creationTime='f'*16),lambda h:h.actual.update(image='C:/Other/AfterFX.exe'),
            lambda h:h.actual.update(processMachine=0x14c),lambda h:h.mods.pop(),lambda h:h.mods.append(copy.deepcopy(h.mods[0])),
            lambda h:h.mods[0].update(path='C:/Foreign/FLT.dll'),lambda h:setattr(h,'bad_hash',True)]:
            host=Host();mutate(host)
            with self.assertRaises(ValueError):qa.verify_host(host,host.expected,host.file)
            self.assertEqual(host.reads,[])
        host=Host();host.short=True
        with self.assertRaises(ValueError):qa.verify_host(host,host.expected,host.file)
        host=Host();host.mods[1]['size']=8
        with self.assertRaises(ValueError):qa.verify_host(host,host.expected,host.file)

    def test_fresh_sample_is_type_exact_and_never_confuses_saved_copy_with_reopen_proof(self):
        reference={'kind':'xfx-parameter-qa-snapshot','schemaVersion':1,'nonce':'1'*32,'project':{'file':'C:/Temp/XFX-Parameter-QA-'+('1'*32)+'/Reopened.aep','dirty':False},'samples':[0,10,20,30]}
        receipt={'ok':True,'sampleId':'2'*32,'snapshot':copy.deepcopy(reference),'runtimeEnabled':False,'hostVerified':False}
        self.assertEqual(qa.verify_fresh_snapshot(reference,receipt,'2'*32),reference)
        for mutate in [lambda r:r.update(sampleId='3'*32),lambda r:r.update(ok=False),lambda r:r.update(hostVerified=True),
            lambda r:r['snapshot']['samples'].__setitem__(0,False),lambda r:r['snapshot']['project'].update(dirty=True),lambda r:r['snapshot']['project'].update(file='C:/User.aep')]:
            bad=copy.deepcopy(receipt);mutate(bad)
            with self.assertRaises(ValueError):qa.verify_fresh_snapshot(reference,bad,'2'*32)

    def test_observed_paths_cannot_redirect_to_ads_unc_or_dot_parent(self):
        for value in [r'\\host\share\AfterFX.exe','C:/AE/../AfterFX.exe','C:/AE/file.dll:stream','C:/AE//file.dll','relative.dll','C:/AE/\nfile.dll']:
            with self.assertRaises(ValueError):qa._path(value)


    def test_valid_full_declared_scope_correlates_names_not_a_write_certificate(self):
        result=observe()
        self.assertTrue(result['ok'],result.get('error'))
        self.assertTrue(result['coverageComplete'])
        self.assertFalse(result['runtimeEnabled']);self.assertFalse(result['hostVerified'])
        self.assertFalse(result['parameterAssociationVerified'])
        group=result['candidateGroups'][0]
        self.assertEqual(group['rootNamesInVectorOrder'],list(qa.EXPECTED_NAMES))
        self.assertEqual(len(group['rows']),4)
        self.assertEqual(group['rows'][0]['displayUtf16'],'滑块')

    def test_scope_must_finish_after_four_hits_and_partial_reads_are_not_success(self):
        for option in ['short','fail']:
            memory=Memory();setattr(memory,option,True)
            result=observe(memory);self.assertFalse(result['ok']);self.assertFalse(result['coverageComplete']);self.assertEqual(result['candidateGroups'],[])
        memory=Memory()
        original=memory.region
        def query(address):
            if address>=0x9000:raise OSError('late region query failure')
            return original(address)
        memory.region=query
        self.assertFalse(observe(memory)['coverageComplete'])

    def test_caps_are_bounded_and_never_return_partial_groups(self):
        for limits in [dict(max_bytes=10),dict(max_reads=1),dict(max_regions=1),dict(max_hits=3),dict(max_nodes=2),dict(max_bytes=2**40),dict(max_ms=0)]:
            with self.subTest(limits=limits):
                result=observe(**limits);self.assertFalse(result['ok']);self.assertFalse(result['coverageComplete']);self.assertEqual(result['candidateGroups'],[])

    def test_region_nonprogress_and_guard_shared_execute_pages(self):
        memory=Memory();memory.regions[0]['size']=0
        self.assertFalse(observe(memory)['coverageComplete'])
        for field,value in [('protect',0x104),('protect',0x40),('type',0x1000000),('state',0x2000)]:
            memory=Memory();memory.regions[1][field]=value
            result=observe(memory);self.assertFalse(result['ok']);self.assertTrue(result['coverageComplete'])

    def test_raw_hits_and_list_order_do_not_override_public_names_or_identity(self):
        for mutate in [lambda m:m.q(0x1228,0x1818), lambda m:m.q(0x1808,0x2000),
            lambda m:m.q(0x3020,0x7000),lambda m:m.raw(0x3008,b'X'*16),
            lambda m:m.string(0x3000,'User effect',0x3900),lambda m:m.q(0x2680,0x2680),
            lambda m:m.q(0x2688,0x2680),lambda m:m.raw(0x2808,b'Y'*16),lambda m:m.q(0x2820,0x3000),
            lambda m:m.raw(0x2840,b'A'*32),lambda m:m.q(0x2908,5000)]:
            memory=Memory();mutate(memory)
            result=observe(memory);self.assertFalse(result['ok']);self.assertEqual(result['candidateGroups'],[])

    def test_guard_revalidation_and_clock_backwards_fail_closed(self):
        memory=Memory();memory.stale=True
        self.assertFalse(observe(memory)['ok'])
        memory=Memory();values=iter([100,99]);memory.now=lambda:next(values,99)
        result=observe(memory);self.assertFalse(result['ok']);self.assertFalse(result['coverageComplete'])

    def test_rejected_pf_layout_preserves_bounded_raw_diagnostics_not_success(self):
        memory=Memory();memory.raw(0x2840,b'X'*32)
        result=observe(memory)
        self.assertFalse(result['ok']);self.assertEqual(result['candidateGroups'],[])
        diagnostic=result['groupDiagnostics'][0]
        self.assertEqual(diagnostic['rootNamesInVectorOrder'],list(qa.EXPECTED_NAMES))
        self.assertEqual(diagnostic['observedChildren'][0]['pfRaw32Hex'],(b'X'*32).hex())
        self.assertFalse(result['parameterAssociationVerified']);self.assertFalse(result['runtimeEnabled'])

    def test_two_identical_name_correlated_groups_are_ambiguous_not_owned(self):
        memory=Memory();original=bytes(memory.data[0x1000:0x9000]);shift=0xf000
        duplicate=bytearray(original)
        for offset in range(0,len(duplicate),8):
            value=struct.unpack_from('<Q',duplicate,offset)[0]
            if 0x1000<=value<0x9000:struct.pack_into('<Q',duplicate,offset,value+shift)
        memory.data.extend(b'\0'*0x10000);memory.raw(0x10000,duplicate);memory.max_address=0x19fff
        memory.regions=[memory.regions[0],dict(base=0x1000,size=0x18000,state=0x1000,type=0x20000,protect=4),dict(base=0x19000,size=0x1000,state=0x10000,type=0,protect=1)]
        result=observe(memory)
        self.assertFalse(result['ok']);self.assertTrue(result['coverageComplete']);self.assertEqual(result['candidateGroups'],[])
        self.assertIn('unique',result['error'])
        capped=observe(memory,max_groups=1);self.assertFalse(capped['coverageComplete'])

    def test_name_correlated_alias_cannot_be_ignored_because_one_child_is_rejected(self):
        memory=Memory();original=bytes(memory.data[0x1000:0x9000]);shift=0xf000
        duplicate=bytearray(original)
        for offset in range(0,len(duplicate),8):
            value=struct.unpack_from('<Q',duplicate,offset)[0]
            if 0x1000<=value<0x9000:struct.pack_into('<Q',duplicate,offset,value+shift)
        memory.data.extend(b'\0'*0x10000);memory.raw(0x10000,duplicate);memory.max_address=0x19fff
        memory.regions=[memory.regions[0],dict(base=0x1000,size=0x18000,state=0x1000,type=0x20000,protect=4),dict(base=0x19000,size=0x1000,state=0x10000,type=0,protect=1)]
        memory.raw(0x2840+shift,b'\0'*32)
        result=observe(memory)
        self.assertFalse(result['ok']);self.assertTrue(result['coverageComplete']);self.assertEqual(result['candidateGroups'],[])
        self.assertIn('ambiguous',result['error'])

    def test_unexpected_reader_valueerror_cannot_be_swallowed_as_shape_rejection(self):
        memory=Memory();memory.data.extend(b'\0'*0x6000);memory.max_address=0xffff
        memory.regions=[memory.regions[0],dict(base=0x1000,size=0xf000,state=0x1000,type=0x20000,protect=4)]
        for root in [0xa000,0xb000,0xc000,0xd000]:
            memory.q(root,memory.base+qa.ROOT_VTABLE_RVA);memory.q(root+0x20,0x7000)
        original=memory.read
        def transient(address,size):
            if address==0x7220:raise ValueError('Unexpected backend read failure')
            return original(address,size)
        memory.read=transient
        result=observe(memory)
        self.assertFalse(result['ok']);self.assertFalse(result['coverageComplete']);self.assertEqual(result['candidateGroups'],[])

    def test_unaligned_vtable_matches_do_not_count_as_a_root(self):
        memory=Memory();memory.raw(0x7011,struct.pack('<Q',memory.base+qa.ROOT_VTABLE_RVA))
        self.assertTrue(observe(memory)['ok'])
        memory.raw(0x7000,struct.pack('<Q',memory.base+qa.ROOT_VTABLE_RVA));memory.q(0x7020,0x1000)
        self.assertFalse(observe(memory)['ok'])


if __name__=='__main__':
    unittest.main(verbosity=2)
