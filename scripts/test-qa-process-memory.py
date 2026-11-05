"""Pure FakeKernel regressions plus explicitly bounded CURRENT Python PID reads only."""
from pathlib import Path
import ctypes as C
import importlib.util
import os
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location('qa_process_memory', Path(__file__).with_name('qa_process_memory.py'))
m = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = m
SPEC.loader.exec_module(m)


def write(pointer, typ, value):
    C.cast(pointer, C.POINTER(typ))[0] = value


class FakeKernel:
    def __init__(self):
        self.calls = []
        self.pid = 77
        self.closed = 0
        self.open_fail = self.read_fail = self.short_read = self.identity_fail = self.wow_fail = False
        self.pid_mismatch = self.module_fail = self.path_fail = self.info_fail = self.region_fail = False
        self.process_machine = 0
        self.native_machine = 0x8664
        self.module_needed = 8
        self.module_base = 0x180000000
        self.module_size = 0x100000
        self.region_base = 0x10000
        self.region_size = 0x1000
        self.region_result = C.sizeof(m.MEMORY_BASIC_INFORMATION64)
        self.image = r'C:\Python\python.exe'
        self.last_error = lambda: 5
        self.close_fail = False
        self.exited = self.no_creation = self.image_truncated = False
        self.maximum_address = 0x7ffffffeffff

    def OpenProcess(self, access, inherit, pid):
        self.calls.append(('OpenProcess', access, inherit, pid))
        return 0 if self.open_fail else 99

    def CloseHandle(self, handle):
        self.calls.append(('CloseHandle', handle))
        if self.close_fail: return 0
        self.closed += 1; return 1

    def GetNativeSystemInfo(self, pointer):
        item=m.SYSTEM_INFO64(); item.lpMinimumApplicationAddress=0x10000
        item.lpMaximumApplicationAddress=self.maximum_address
        write(pointer,m.SYSTEM_INFO64,item)

    def GetProcessId(self, handle):
        return self.pid + int(self.pid_mismatch)

    def GetProcessTimes(self, handle, created, exited, kernel, user):
        if self.identity_fail: return 0
        write(created, m.FILETIME, m.FILETIME(0x89abcdef, 0x01234567))
        for pointer in (exited, kernel, user): write(pointer,m.FILETIME,m.FILETIME(0,0))
        if self.exited: write(exited,m.FILETIME,m.FILETIME(1,0))
        if self.no_creation: write(created,m.FILETIME,m.FILETIME(0,0))
        return 1

    def QueryFullProcessImageNameW(self, handle, flags, target, size):
        if self.identity_fail: return 0
        target.value = self.image
        write(size, m.DWORD, 32768 if self.image_truncated else len(self.image)); return 1

    def IsWow64Process2(self, handle, process, native):
        if self.wow_fail: return 0
        write(process,m.WORD,self.process_machine); write(native,m.WORD,self.native_machine); return 1

    def K32EnumProcessModulesEx(self, handle, buffer, capacity, needed, flags):
        self.calls.append(('Enum', capacity, flags))
        if self.module_fail: return 0
        write(needed,m.DWORD,self.module_needed)
        if self.module_needed <= capacity and self.module_needed >= 8: buffer[0] = self.module_base
        return 1

    def K32GetModuleFileNameExW(self, handle, module, buffer, capacity):
        if self.path_fail: return capacity
        buffer.value = r'C:\Python\python.exe'; return len(buffer.value)

    def K32GetModuleInformation(self, handle, module, pointer, size):
        if self.info_fail: return 0
        write(pointer,m.MODULEINFO,m.MODULEINFO(self.module_base,self.module_size,self.module_base + 16)); return 1

    def VirtualQueryEx(self, handle, address, pointer, size):
        self.calls.append(('VirtualQueryEx', address.value))
        if self.region_fail: return 0
        item = m.MEMORY_BASIC_INFORMATION64()
        item.BaseAddress = self.region_base; item.AllocationBase = self.region_base
        item.RegionSize = self.region_size; item.State = 0x1000; item.Protect = 4; item.Type = 0x20000
        write(pointer,m.MEMORY_BASIC_INFORMATION64,item)
        return self.region_result

    def ReadProcessMemory(self, handle, address, output, size, got):
        self.calls.append(('ReadProcessMemory',address.value,size))
        length = size - int(self.short_read)
        C.memmove(output, b'abcdefgh'[:size], length)
        write(got,m.SIZE_T,length)
        return int(not self.read_fail)


class BackendTests(unittest.TestCase):
    def test_x64_struct_layouts_and_creation_lowercase(self):
        self.assertEqual(C.sizeof(m.SYSTEM_INFO64),48)
        self.assertEqual(m.SYSTEM_INFO64.lpMaximumApplicationAddress.offset,16)
        self.assertEqual(C.sizeof(m.FILETIME),8)
        self.assertEqual(C.sizeof(m.MODULEINFO),24)
        self.assertEqual(m.MODULEINFO.EntryPoint.offset,16)
        self.assertEqual(C.sizeof(m.MEMORY_BASIC_INFORMATION64),48)
        self.assertEqual(m.MEMORY_BASIC_INFORMATION64.RegionSize.offset,24)
        api=FakeKernel()
        with m.WindowsReadOnlyProcess(77,_api=api) as process:
            identity=process.identity()
            self.assertEqual(identity,{'pid':77,'creationTime':'0123456789abcdef','image':api.image,
                'processMachine':0,'nativeMachine':0x8664})
        self.assertEqual(api.closed,1)
        self.assertEqual(api.calls[0],('OpenProcess',0x410,False,77))

    def test_argument_failures_never_open_any_process(self):
        for pid in (0,-1,2**32,True,1.0,None,'77'):
            api=FakeKernel()
            with self.assertRaises((ValueError,TypeError)): m.WindowsReadOnlyProcess(pid,_api=api)
            self.assertEqual(api.calls,[])

    def test_open_failure_and_constructor_identity_failure_release_owned_handles(self):
        api=FakeKernel(); api.open_fail=True
        with self.assertRaises(OSError):m.WindowsReadOnlyProcess(77,_api=api)
        self.assertEqual(api.closed,0)
        for option in ('identity_fail','wow_fail','pid_mismatch'):
            api=FakeKernel(); setattr(api,option,True)
            with self.assertRaises((OSError,ValueError)):m.WindowsReadOnlyProcess(77,_api=api)
            self.assertEqual(api.closed,1)

    def test_wow64_arm64_and_unknown_machine_fail_native_x64_guard(self):
        for process,native in ((0x14c,0x8664),(0,0xaa64),(0,0),(0x8664,0x8664)):
            api=FakeKernel();api.process_machine=process;api.native_machine=native
            with self.assertRaises(ValueError):m.WindowsReadOnlyProcess(77,_api=api)
            self.assertEqual(api.closed,1)

    def test_context_exception_and_double_close_are_safe(self):
        api=FakeKernel()
        with self.assertRaises(RuntimeError):
            with m.WindowsReadOnlyProcess(77,_api=api):raise RuntimeError('caller failed')
        self.assertEqual(api.closed,1)
        process=m.WindowsReadOnlyProcess(77,_api=FakeKernel()); kernel=process._api
        process.close();process.close();self.assertEqual(kernel.closed,1)
        for action in (process.identity,process.modules,lambda:process.region(0x10000),lambda:process.read(0x10000,8)):
            with self.assertRaises(ValueError):action()

    def test_module_observation_has_bounded_loaded_base_size_and_path(self):
        api=FakeKernel()
        with m.WindowsReadOnlyProcess(77,_api=api) as process:
            self.assertEqual(process.modules(),[{'base':api.module_base,'size':api.module_size,'path':r'C:\Python\python.exe'}])
        self.assertIn(('Enum',4096*8,3),api.calls)

    def test_module_count_zero_partial_and_capacity_overflow_fail_closed(self):
        for count in (0,1,7,4096*8+8,0xffffffff):
            api=FakeKernel();api.module_needed=count
            with m.WindowsReadOnlyProcess(77,_api=api) as process:
                with self.assertRaises(ValueError):process.modules()
            self.assertEqual(api.closed,1)

    def test_module_api_path_info_failure_invalid_base_and_size_fail_closed(self):
        for option in ('module_fail','path_fail','info_fail'):
            api=FakeKernel();setattr(api,option,True)
            with m.WindowsReadOnlyProcess(77,_api=api) as process:
                with self.assertRaises((OSError,ValueError)):process.modules()
        for base,size in ((0,0x1000),(0x180000000,0),(0x7ffffffffff0,0x1000)):
            api=FakeKernel();api.module_base=base;api.module_size=size
            with m.WindowsReadOnlyProcess(77,_api=api) as process:
                with self.assertRaises(ValueError):process.modules()

    def test_region_reports_one_current_region_without_scan(self):
        api=FakeKernel()
        with m.WindowsReadOnlyProcess(77,_api=api) as process:
            self.assertEqual(process.region(0x10001),{'base':0x10000,'size':0x1000,'allocationBase':0x10000,
                'state':0x1000,'protect':4,'type':0x20000})
        self.assertEqual(sum(call[0]=='VirtualQueryEx' for call in api.calls),1)

    def test_region_api_incomplete_noncontaining_zero_and_overflow_fail_closed(self):
        for change in (lambda a:setattr(a,'region_fail',True),lambda a:setattr(a,'region_result',47),
            lambda a:setattr(a,'region_size',0),lambda a:setattr(a,'region_base',0x20000),
            lambda a:(setattr(a,'region_base',0x7ffffffffff0),setattr(a,'region_size',0x1000))):
            api=FakeKernel();change(api)
            with m.WindowsReadOnlyProcess(77,_api=api) as process:
                with self.assertRaises((OSError,ValueError)):process.region(0x10001)

    def test_exact_read_allows_unaligned_bytes_but_never_partial_or_bool_failure(self):
        api=FakeKernel()
        with m.WindowsReadOnlyProcess(77,_api=api) as process:
            self.assertEqual(process.read(0x10001,8),b'abcdefgh')
            for option in ('read_fail','short_read'):
                setattr(api,option,True)
                with self.assertRaises(OSError):process.read(0x10001,8)
                setattr(api,option,False)

    def test_read_pointer_and_size_caps_reject_before_kernel_call(self):
        api=FakeKernel()
        with m.WindowsReadOnlyProcess(77,_api=api) as process:
            for address,size in ((0,1),(-1,1),(True,1),(0x800000000000,1),(0x7fffffffffff,2),
                (0x10000,0),(0x10000,-1),(0x10000,True),(0x10000,1024*1024+1)):
                with self.assertRaises((TypeError,ValueError)):process.read(address,size)
        self.assertFalse(any(call[0]=='ReadProcessMemory' for call in api.calls))

    def test_native_maximum_and_zero_free_region_are_observed_without_a_scan(self):
        api=FakeKernel()
        with m.WindowsReadOnlyProcess(77,_api=api) as process:
            self.assertEqual(process.max_address,api.maximum_address)
            api.region_base=0; api.region_size=0x10000
            self.assertEqual(process.region(0)['base'],0)
        api=FakeKernel();api.maximum_address=0
        with self.assertRaises(ValueError):m.WindowsReadOnlyProcess(77,_api=api)
        self.assertEqual(api.closed,1)

    def test_close_failure_is_explicit_retryable_and_never_masks_original_exception(self):
        api=FakeKernel(); process=m.WindowsReadOnlyProcess(77,_api=api)
        api.close_fail=True
        with self.assertRaises(OSError):process.close()
        with self.assertRaisesRegex(RuntimeError,'original') as raised:
            with process:raise RuntimeError('original')
        self.assertTrue(any('CloseHandle' in note for note in raised.exception.__notes__))
        api.close_fail=False; process.close(); process.close(); self.assertEqual(api.closed,1)

    def test_import_does_not_load_system_or_adobe_libraries_or_open_process(self):
        with patch.object(C,'WinDLL',side_effect=AssertionError('import attempted library load'),create=True):
            fresh=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(fresh)
        self.assertTrue(hasattr(fresh,'WindowsReadOnlyProcess'))
        self.assertFalse(hasattr(fresh,'main'))

    def test_exited_creationless_empty_or_truncated_image_constructor_closes(self):
        for option in ('exited','no_creation','image_truncated'):
            api=FakeKernel();setattr(api,option,True)
            with self.assertRaises(ValueError):m.WindowsReadOnlyProcess(77,_api=api)
            self.assertEqual(api.closed,1)
        api=FakeKernel();api.image=''
        with self.assertRaises(ValueError):m.WindowsReadOnlyProcess(77,_api=api)
        self.assertEqual(api.closed,1)

    def test_duplicate_and_null_module_handles_are_rejected_not_filled_from_disk(self):
        for null in (False,True):
            api=FakeKernel()
            def enumerate_duplicate(handle,buffer,capacity,needed,flags):
                write(needed,m.DWORD,16);buffer[0]=api.module_base;buffer[1]=None if null else api.module_base;return 1
            api.K32EnumProcessModulesEx=enumerate_duplicate
            with m.WindowsReadOnlyProcess(77,_api=api) as process:
                with self.assertRaises(ValueError):process.modules()

    @unittest.skipUnless(sys.platform == 'win32','actual-self check is Windows-only')
    def test_actual_current_python_process_only_identity_modules_region_and_known_buffer(self):
        # Explicitly authorized selftest; target is THIS interpreter, never AE/another PID.
        target=C.create_string_buffer(b'XFX readonly current-python buffer\x00\x81\xff')
        address=C.addressof(target)
        with m.WindowsReadOnlyProcess(os.getpid()) as process:
            observed=process.identity()
            self.assertEqual(observed['pid'],os.getpid());self.assertRegex(observed['creationTime'],r'^[a-f0-9]{16}$')
            self.assertEqual(Path(observed['image']).name.lower(),Path(sys.executable).name.lower())
            self.assertEqual(observed['nativeMachine'],0x8664)
            self.assertLessEqual(address,process.max_address)
            self.assertGreater(process.max_address,0x10000)
            modules=process.modules();self.assertGreater(len(modules),0);self.assertLessEqual(len(modules),4096)
            self.assertTrue(any(Path(item['path']).name.lower()==Path(sys.executable).name.lower() for item in modules))
            region=process.region(address);self.assertLessEqual(region['base'],address)
            self.assertGreaterEqual(region['base']+region['size'],address+C.sizeof(target))
            self.assertEqual(process.read(address,C.sizeof(target)),target.raw)


if __name__ == '__main__':
    unittest.main(verbosity=2)
