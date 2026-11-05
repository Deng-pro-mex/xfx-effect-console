"""Offline topology tests; no process access, host launch or private calls."""
from pathlib import Path
import importlib.util
import struct
import sys
import unittest

sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
spec=importlib.util.spec_from_file_location('topology',Path(__file__).with_name('qa_parameter_topology.py'))
qa=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=qa
spec.loader.exec_module(qa)


class Memory:
    def __init__(self):
        self.data=bytearray(0x10000)
        self.calls=[]
        self.now=0
        self.partial=False
        self.stale=False
        self.parent=0x1000
        self.base=0x180000000
        self.roots=[0x2000+i*0x2000 for i in range(4)]
        self.q(0x1220,0x1800);self.q(0x1228,0x1820)
        for i,root in enumerate(self.roots):
            self.q(0x1800+8*i,root)
            self.q(root,self.base+809488);self.raw(root+8,b'R'*16);self.q(root+0x20,self.parent)
            self.text(root,['XFX Source','XFX By Name','XFX By Match','XFX By Index'][i],root+0x1800)
            vector,slots,template=root+0x1200,root+0x1240,root+0x1300
            self.q(root+0x210,vector);self.q(root+0x218,root+0x1600)
            self.q(vector,slots);self.q(vector+8,slots+16);self.q(vector+16,slots+16)
            for j,guid in enumerate((b'S'*16,b'G'*16)):
                self.q(slots+j*8,template+j*0x100);self.raw(template+j*0x100+8,guid)
                # canonical parents are intentionally NOT this live root.
                self.q(template+j*0x100+0x20,0xa0)
            head,node1,node2,child1,child2=root+0x500,root+0x600,root+0x640,root+0x800,root+0xa00
            self.q(root+0x228,head);self.q(root+0x230,2)
            self.q(head,node1);self.q(head+8,node2)
            # Ring order G then S, opposite canonical order S then G.
            for node,nxt,prev,child,guid,label,pf in [(node1,node2,head,child1,b'G'*16,'Internal Group',b''),(node2,head,node1,child2,b'S'*16,'滑块',b'Slider')]:
                self.q(node,nxt);self.q(node+8,prev);self.raw(node+16,guid);self.q(node+32,child)
                self.q(child,0x180100000);self.raw(child+8,guid);self.q(child+0x20,root)
                self.raw(child+0x40,pf+b'\0'*(32-len(pf)));self.text(child,label,root+0x1900)

    def q(self,at,value):self.raw(at,struct.pack('<Q',value))
    def raw(self,at,value):self.data[at:at+len(value)]=value
    def text(self,at,text,heap):
        encoded=text.encode('utf-16-le')+b'\0\0'
        if len(text)>7:self.q(at+0xf0,heap);self.raw(heap,encoded);capacity=len(text)
        else:self.raw(at+0xf0,encoded);capacity=7
        self.q(at+0x100,len(text));self.q(at+0x108,capacity)
    def read(self,at,size):
        self.calls.append((at,size));value=bytes(self.data[at:at+size])
        if self.partial:return value[:-1]
        if self.stale and self.calls.count((0x2210,16))>1:return b'\0'*size
        return value
    def clock(self):return self.now


def run(memory=None,**limits):
    memory=memory or Memory();before=bytes(memory.data)
    result=qa.inspect_topology(memory,memory.parent,memory.base,limits=qa.Limits(**limits),clock=memory.clock)
    assert bytes(memory.data)==before
    return result


class TopologyTests(unittest.TestCase):
    def test_guid_join_distinguishes_canonical_slots_from_ring_order(self):
        result=run();self.assertTrue(result['ok'],result.get('error'))
        self.assertFalse(result['runtimeEnabled']);self.assertFalse(result['hostVerified']);self.assertFalse(result['parameterAssociationVerified'])
        self.assertFalse(result['publicIndexMappingVerified']);self.assertFalse(result['activeOwnershipVerified'])
        self.assertEqual(len(result['roots']),4)
        root=result['roots'][0]
        self.assertEqual(root['canonicalSlots'][0]['rawGuidHex'],(b'S'*16).hex())
        self.assertEqual(root['ringChildren'][0]['rawGuidHex'],(b'G'*16).hex())
        self.assertEqual(root['canonicalSlots'][0]['matchedLiveChildPtr'],hex(0x2a00))
        self.assertEqual(root['ringChildren'][0]['pfRaw32Hex'],'0'*64)
        self.assertTrue(result['diagnosticOnly'])

    def test_canonical_template_parent_is_not_required_to_equal_live_root(self):
        self.assertTrue(run()['ok'])
        memory=Memory();memory.q(0x2820,0xa0)
        self.assertFalse(run(memory)['ok'])

    def test_empty_overflow_unaligned_and_large_vectors_fail_closed(self):
        for mutate in [lambda m:m.q(0x2210,0),lambda m:m.q(0x3208,0x3240),lambda m:m.q(0x3208,0x3249),
            lambda m:m.q(0x3210,0x3248),lambda m:m.q(0x3208,0x3240+1024),lambda m:m.q(0x3240,0x3301),
            lambda m:m.q(0x3240,0),lambda m:m.q(0x3210,0x800000000000)]:
            memory=Memory();mutate(memory);result=run(memory);self.assertFalse(result['ok']);self.assertEqual(result['roots'],[])

    def test_duplicate_missing_canonical_or_live_guids_are_not_silently_joined(self):
        for mutate in [lambda m:m.raw(0x3408,b'S'*16),lambda m:m.raw(0x3408,b'Z'*16),
            lambda m:m.raw(0x2640+16,b'G'*16),lambda m:m.raw(0x2a08,b'X'*16)]:
            memory=Memory();mutate(memory);self.assertFalse(run(memory)['ok'])

    def test_parent_membership_names_and_root_guid_must_match(self):
        for mutate in [lambda m:m.q(0x1808,0x2000),lambda m:m.q(0x4020,0x6000),lambda m:m.q(0x2000,1),
            lambda m:m.raw(0x4008,b'X'*16),lambda m:m.text(0x2000,'Foreign',0)]:
            memory=Memory();mutate(memory);self.assertFalse(run(memory)['ok'])

    def test_list_cycle_count_backlinks_alias_and_empty_guid_fail(self):
        for mutate in [lambda m:m.q(0x2230,17),lambda m:m.q(0x2600,0x2600),lambda m:m.q(0x2648,0x2500),
            lambda m:m.q(0x2660,0x2800),lambda m:m.raw(0x2610,b'\0'*16)]:
            memory=Memory();mutate(memory);self.assertFalse(run(memory)['ok'])

    def test_transport_stale_and_tight_budgets_fail_without_partial_result(self):
        memory=Memory();memory.partial=True
        self.assertFalse(run(memory)['ok'])
        memory=Memory();memory.stale=True
        self.assertFalse(run(memory)['ok'])
        for limits in [dict(max_bytes=1),dict(max_reads=1),dict(max_ms=0),dict(max_bytes=4*1024**3),dict(max_nodes=1)]:
            result=run(**limits);self.assertFalse(result['ok']);self.assertEqual(result['roots'],[])


if __name__=='__main__':unittest.main(verbosity=2)
