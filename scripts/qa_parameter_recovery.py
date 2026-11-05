"""Pure offline audit of per-object label-write/restore event histories.

This is NOT a journal executor, native bridge, writer, recovery implementation,
project authorization or proof that events occurred. No IO, CLI, processes,
backend callbacks or private calls. A trusted future executor must separately
establish ownership and persist begin_write BEFORE the first target mutation.

Crucial invariant: the CURRENT attempted operation is possibly modified even
when it fails or never returns. It is not omitted from reverse recovery review.
Full original PF32 and display must both match before restoration is recorded.
Audit results never authorize writes or deletion of originals/journal evidence.
"""
import re


def _fields(value,names):
    if type(value) is not dict or set(value)!=set(names):raise ValueError('Unexpected or missing recovery-history fields')


def _text(value,limit):
    if type(value) is not str or not value or len(value)>limit or '\0' in value or any(0xd800<=ord(char)<=0xdfff for char in value):
        raise ValueError('Invalid recovery-history text')
    return len(value.encode('utf-8',errors='strict'))


def _raw(value):
    if type(value) is not str or not re.fullmatch('[a-f0-9]{64}',value):raise ValueError('Expected exact lowercase PF32 bytes')
    raw=bytes.fromhex(value)
    if not 1<=raw.find(b'\0')<=31:raise ValueError('Expected captured nonempty bounded PF label')
    return value


def inspect_recovery_history(snapshots,events):
    result={'ok':False,'dataOnly':True,'historyComplete':False,'states':{},'recoveryOrder':[],
        'allAttemptedObjectsVerifiedRestored':False,'retainedOriginalsRequired':True,
        'writeAuthorized':False,'journalDeletionAuthorized':False,'runtimeEnabled':False,'hostVerified':False}
    try:
        if type(snapshots) is not list or not 1<=len(snapshots)<=64 or type(events) is not list or len(events)>1024:
            raise ValueError('Recovery history count limit')
        originals={};states={};attempts=[];total=0
        for snapshot in snapshots:
            _fields(snapshot,['objectId','originalPfRaw32Hex','originalDisplay','appliedPfRaw32Hex','appliedDisplay'])
            name=snapshot['objectId'];total+=_text(name,128)
            if name in originals:raise ValueError('Duplicate recovery object identity')
            for key in ['originalPfRaw32Hex','appliedPfRaw32Hex']:total+=len(_raw(snapshot[key]))
            for key in ['originalDisplay','appliedDisplay']:total+=_text(snapshot[key],4096)
            if total>1024**2:raise ValueError('Recovery history text-byte limit')
            originals[name]=dict(snapshot);states[name]='captured'
        for event in events:
            if type(event) is not dict:raise ValueError('Invalid recovery event')
            kind=event.get('kind')
            if kind not in ('begin_write','write_verified','write_failed','begin_restore','restore_verified','restore_failed'):
                raise ValueError('Unknown recovery event')
            verified=kind in ('write_verified','restore_verified')
            _fields(event,['kind','objectId']+(['pfRaw32Hex','display'] if verified else []))
            name=event['objectId'];total+=_text(name,128)+len(kind)
            if name not in states:raise ValueError('Recovery event has no per-object original capture')
            state=states[name];original=originals[name]
            if verified:
                total+=len(_raw(event['pfRaw32Hex']))+_text(event['display'],4096)
            if total>1024**2:raise ValueError('Recovery history text-byte limit')
            if kind=='begin_write':
                if state!='captured':raise ValueError('Repeated or out-of-order write begin')
                # Conservatively mark CURRENT item before any return is logged.
                states[name]='write_unverified';attempts.append(name)
            elif kind=='write_verified':
                if state!='write_unverified':raise ValueError('Write readback without a pending write')
                if event['pfRaw32Hex']!=original['appliedPfRaw32Hex'] or event['display']!=original['appliedDisplay']:
                    raise ValueError('Applied PF32/display readback differs')
                states[name]='applied'
            elif kind=='write_failed':
                if state!='write_unverified':raise ValueError('Write failure without a pending write')
                states[name]='unknown'
            elif kind=='begin_restore':
                if state not in ('write_unverified','applied','unknown'):raise ValueError('Restore begin without a possibly changed object')
                states[name]='restore_unverified'
            elif kind=='restore_verified':
                if state!='restore_unverified':raise ValueError('Restore readback without pending restore')
                if event['pfRaw32Hex']!=original['originalPfRaw32Hex'] or event['display']!=original['originalDisplay']:
                    raise ValueError('Original PF32/display not restored exactly')
                states[name]='restored'
            elif kind=='restore_failed':
                if state!='restore_unverified':raise ValueError('Restore failure without pending restore')
                states[name]='unknown'
        result['states']=states
        result['recoveryOrder']=[name for name in reversed(attempts) if states[name]!='restored']
        result['allAttemptedObjectsVerifiedRestored']=bool(attempts) and all(states[name]=='restored' for name in attempts)
        result['historyComplete']=True;result['ok']=True
        # Originals/journal deletion is NEVER authorized by this data-only audit,
        # including when all supplied events claim exact restoration.
    except (ValueError,TypeError,UnicodeError) as error:
        result['error']=str(error)[:512]
    return result
