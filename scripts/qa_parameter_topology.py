"""Read-only canonical GUID topology diagnostics for one injected QA group.

No CLI, process-opening, target writes, private calls or import-time IO. This
function cannot establish active ownership, public index mapping or safe writes.
A successful result means only internally consistent bounded observations.

Layout source: local exact-build disk evidence named-container-r6 and the MIT
After-Effects-Plugin-Localizer dc5a591c78d67ba7b351c99e9f8222af6f160e4c,
Copyright (c) 2026 verse. Full MIT notice is distributed with the source in
native/third_party/After-Effects-Plugin-Localizer.LICENSE.txt.
"""
from dataclasses import dataclass
import struct
import time
from qa_parameter_observer import Budget, Limits as ObservationLimits, EXPECTED_NAMES, ROOT_VTABLE_RVA, pointer


@dataclass(frozen=True)
class Limits(ObservationLimits):
    max_bytes: int = 1024**2
    max_ms: int = 1000
    max_reads: int = 4096
    max_nodes: int = 64


def _guid(budget, address):
    value=budget.read(address,16)
    if not any(value):raise ValueError('Empty raw GUID')
    return value


def _canonical(budget, root):
    # Read/revalidate the shared_ptr pointee and control pointer without touching
    # refcounts. Canonical objects are templates, not this root's live children.
    shared=budget.read(root+0x210,16)
    vector,control=struct.unpack('<QQ',shared)
    pointer(vector);pointer(control)
    begin,end,capacity=struct.unpack('<QQQ',budget.read(vector,24))
    pointer(begin);pointer(end);pointer(capacity)
    if end<=begin or (end-begin)%8 or capacity<end or capacity-begin>16*8 or (capacity-begin)%8:
        raise ValueError('Unsupported bounded canonical vector')
    count=(end-begin)//8
    slots=struct.unpack('<'+'Q'*count,budget.read(begin,end-begin))
    if len(set(slots))!=len(slots):raise ValueError('Canonical pointer aliases')
    result=[];seen=set()
    for index,template in enumerate(slots):
        pointer(template)
        guid=_guid(budget,template+8)
        if guid in seen:raise ValueError('Duplicate canonical raw GUID')
        seen.add(guid)
        result.append({'internalCanonicalSlot':index,'templatePtr':hex(template),'rawGuidHex':guid.hex()})
    return result


def _children(budget,root,seen_children,seen_nodes):
    head=pointer(budget.u64(root+0x228))
    count=budget.u64(root+0x230)
    if not 1<=count<=16:raise ValueError('Unsupported named-list count')
    node,tail=struct.unpack('<QQ',budget.read(head,16))
    pointer(node);pointer(tail)
    previous=head;rows=[];seen_guids=set()
    for _ in range(count):
        budget.nodes+=1
        if budget.nodes>budget.limits.max_nodes:raise ValueError('Topology node budget exhausted')
        pointer(node)
        if node==head or node in seen_nodes:raise ValueError('Named-list cycle or node alias')
        seen_nodes.add(node)
        raw=budget.read(node,40)
        next_node,prev=struct.unpack_from('<QQ',raw)
        pointer(next_node);pointer(prev)
        child=pointer(struct.unpack_from('<Q',raw,32)[0]);guid=raw[16:32]
        if prev!=previous or child in seen_children or not any(guid) or guid in seen_guids:
            raise ValueError('Named-list backlink child alias or duplicate GUID')
        seen_children.add(child);seen_guids.add(guid)
        if budget.u64(child+0x20)!=root or _guid(budget,child+8)!=guid:
            raise ValueError('Live-child GUID or parent mismatch')
        pf=budget.read(child+0x40,32)
        rows.append({'liveChildPtr':hex(child),'rawGuidHex':guid.hex(),'vtablePtr':hex(budget.u64(child)),
            'pfRaw32Hex':pf.hex(),'hasBoundedNonemptyPfBytes':1<=pf.find(b'\0')<=31,
            'displayUtf16':budget.string(child)})
        previous,node=node,next_node
    if node!=head or tail!=previous:raise ValueError('Named-list sentinel count mismatch')
    return rows


def inspect_topology(reader,parent,flt_base,limits=None,clock=time.monotonic):
    result={'ok':False,'diagnosticOnly':True,'runtimeEnabled':False,'hostVerified':False,
        'parameterAssociationVerified':False,'publicIndexMappingVerified':False,'activeOwnershipVerified':False,
        'qaOnlyPointersNotLexiconData':True,'roots':[]}
    budget=None
    try:
        pointer(parent);pointer(flt_base)
        limits=limits or Limits();ceil=Limits()
        for key,value in vars(limits).items():
            if type(value) is not int or value<=0 or value>getattr(ceil,key):raise ValueError('Invalid or raised topology budget')
        budget=Budget(reader,limits,clock)
        begin,end=budget.u64(parent+0x220),budget.u64(parent+0x228)
        pointer(begin);pointer(end)
        if end-begin!=32:raise ValueError('Expected exactly four attached root slots')
        roots=struct.unpack('<QQQQ',budget.read(begin,32))
        if len(set(roots))!=4:raise ValueError('Duplicate root slot')
        names=[];rows=[];root_guid=None;seen_children=set();seen_nodes=set()
        for root in roots:
            pointer(root)
            if budget.u64(root)!=flt_base+ROOT_VTABLE_RVA or budget.u64(root+0x20)!=parent:
                raise ValueError('Root vtable or parent mismatch')
            guid=_guid(budget,root+8)
            if root_guid is None:root_guid=guid
            if guid!=root_guid:raise ValueError('Root GUID differs')
            name=budget.string(root);names.append(name)
            canonical=_canonical(budget,root)
            children=_children(budget,root,seen_children,seen_nodes)
            by_guid={row['rawGuidHex']:row for row in children}
            for slot in canonical:
                if slot['rawGuidHex'] not in by_guid:raise ValueError('Canonical raw GUID has no unique live match')
                slot['matchedLiveChildPtr']=by_guid[slot['rawGuidHex']]['liveChildPtr']
            rows.append({'rootPtr':hex(root),'displayUtf16':name,'canonicalSlots':canonical,'ringChildren':children})
        if tuple(names)!=EXPECTED_NAMES:raise ValueError('Root names do not correlate with owned QA fixture')
        budget.verify()
        result['roots']=rows;result['ok']=True
    except (ValueError,OSError,UnicodeError,KeyError,struct.error) as error:
        result['error']=str(error)[:512]
    if budget is not None:
        result['statistics']={'readBytes':budget.read_bytes,'readCalls':budget.read_calls,'nodes':budget.nodes}
    return result
