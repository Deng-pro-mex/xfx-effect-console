"""Pure event-history tests. No target writes, IO or process operations."""
from pathlib import Path
import importlib.util
import sys
import unittest
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('recovery',Path(__file__).with_name('qa_parameter_recovery.py'))
qa=importlib.util.module_from_spec(spec);sys.modules[spec.name]=qa;spec.loader.exec_module(qa)


def row(name):
    return {'objectId':name,'originalPfRaw32Hex':(b'Old\0'+b'X'*28).hex(),'originalDisplay':'Original',
        'appliedPfRaw32Hex':(b'New\0'+b'\0'*28).hex(),'appliedDisplay':'Translated'}


def event(kind,name='a',**values):return dict(kind=kind,objectId=name,**values)

def verified(kind,original=False,name='a'):
    captured=row(name)
    return event(kind,name,pfRaw32Hex=captured['originalPfRaw32Hex' if original else 'appliedPfRaw32Hex'],
        display=captured['originalDisplay' if original else 'appliedDisplay'])


def audit(events,rows=None):return qa.inspect_recovery_history(rows or [row('a')],events)


class RecoveryTests(unittest.TestCase):
    def test_begin_without_reply_still_requires_current_object_recovery(self):
        result=audit([event('begin_write')])
        self.assertTrue(result['ok']);self.assertEqual(result['recoveryOrder'],['a'])
        self.assertEqual(result['states']['a'],'write_unverified')
        self.assertFalse(result['allAttemptedObjectsVerifiedRestored'])
        self.assertFalse(result['writeAuthorized']);self.assertFalse(result['journalDeletionAuthorized'])

    def test_failed_current_setter_is_in_reverse_order_not_only_previous_successes(self):
        events=[event('begin_write','a'),verified('write_verified',name='a'),event('begin_write','b'),event('write_failed','b')]
        result=audit(events,[row('a'),row('b')])
        self.assertTrue(result['ok']);self.assertEqual(result['recoveryOrder'],['b','a'])
        self.assertEqual(result['states']['b'],'unknown')

    def test_complete_restore_requires_exact_pf_all32bytes_and_display(self):
        events=[event('begin_write'),event('write_failed'),event('begin_restore'),verified('restore_verified',True)]
        result=audit(events)
        self.assertTrue(result['ok']);self.assertTrue(result['allAttemptedObjectsVerifiedRestored']);self.assertEqual(result['recoveryOrder'],[])
        self.assertFalse(result['journalDeletionAuthorized'])

    def test_restored_visible_label_with_changed_pf_tail_is_not_exact_restore(self):
        bad=verified('restore_verified',True);bad['pfRaw32Hex']=(b'Old\0'+b'\0'*28).hex()
        result=audit([event('begin_write'),event('begin_restore'),bad])
        self.assertFalse(result['ok']);self.assertFalse(result['allAttemptedObjectsVerifiedRestored'])
        self.assertFalse(result['journalDeletionAuthorized'])

    def test_pf_only_restore_does_not_hide_display_setter_half_success(self):
        bad=verified('restore_verified',True);bad['display']='Translated'
        result=audit([event('begin_write'),event('write_failed'),event('begin_restore'),bad])
        self.assertFalse(result['ok']);self.assertFalse(result['allAttemptedObjectsVerifiedRestored'])

    def test_failed_or_unanswered_restore_stays_unknown_or_unverified(self):
        for tail in [[],[event('restore_failed')]]:
            result=audit([event('begin_write'),verified('write_verified'),event('begin_restore')]+tail)
            self.assertTrue(result['ok']);self.assertEqual(result['recoveryOrder'],['a']);self.assertFalse(result['allAttemptedObjectsVerifiedRestored'])

    def test_original_capture_alone_cannot_be_called_a_restored_write(self):
        result=audit([])
        self.assertTrue(result['ok']);self.assertFalse(result['allAttemptedObjectsVerifiedRestored']);self.assertEqual(result['recoveryOrder'],[])

    def test_duplicate_missing_and_out_of_order_operations_are_rejected(self):
        for events in [[verified('write_verified')],[event('write_failed')],[event('begin_restore')],
            [event('begin_write'),event('begin_write')],[event('begin_write','missing')],
            [event('begin_write'),event('begin_restore'),verified('restore_verified',True),event('begin_write')]]:
            self.assertFalse(audit(events)['ok'])
        self.assertFalse(audit([],[row('a'),row('a')])['ok'])

    def test_verified_applied_readback_must_match_both_planned_fields(self):
        bad=verified('write_verified');bad['pfRaw32Hex']='00'*32
        self.assertFalse(audit([event('begin_write'),bad])['ok'])
        bad=verified('write_verified');bad['display']='Wrong'
        self.assertFalse(audit([event('begin_write'),bad])['ok'])

    def test_strict_shape_text_bytes_limits_and_no_input_mutation(self):
        import copy
        events=[event('begin_write')];rows=[row('a')];before=copy.deepcopy((rows,events))
        self.assertTrue(audit(events,rows)['ok']);self.assertEqual((rows,events),before)
        for mutate in [lambda r:r.update(extra=True),lambda r:r.update(originalPfRaw32Hex='0'*63),
            lambda r:r.update(originalDisplay='\ud800'),lambda r:r.update(objectId='')]:
            value=row('a');mutate(value);self.assertFalse(audit([],[value])['ok'])
        self.assertFalse(audit([event('begin_write',extra=True)])['ok'])
        self.assertFalse(audit([event('begin_write')]*1025)['ok'])
        self.assertFalse(audit([], [row(str(i)) for i in range(65)])['ok'])
        oversized=[dict(row(str(i)),originalDisplay='中'*4096,appliedDisplay='文'*4096) for i in range(64)]
        self.assertFalse(audit([],oversized)['ok'],'snapshot-only history must obey byte cap too')


if __name__=='__main__':unittest.main(verbosity=2)
