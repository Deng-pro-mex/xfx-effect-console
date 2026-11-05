"""Internal QA query/read backend. No CLI, import-time IO, scanning or AE automation.

The owned-fixture frontend MUST independently authorize PID+creation/image/modules
before reads and recheck them afterwards. This generic internal transport grants
no project ownership, ABI, hash, parameter association or write certificate.
Only a caller's explicit construction opens a query/read-only process handle.
No DLLs except Windows kernel32 are loaded, and only during construction.
Module file hashing/canonical leases are deliberately outside this backend.
SDK AEGP_StreamRefH is opaque: no public private-TDB pointer conversion is known.
"""
import ctypes as C
import sys

DWORD = C.c_uint32
WORD = C.c_uint16
SIZE_T = C.c_size_t
HANDLE = C.c_void_p


class FILETIME(C.Structure):
    _fields_ = [('dwLowDateTime', DWORD), ('dwHighDateTime', DWORD)]


class MODULEINFO(C.Structure):
    _fields_ = [('lpBaseOfDll', HANDLE), ('SizeOfImage', DWORD), ('EntryPoint', HANDLE)]


class MEMORY_BASIC_INFORMATION64(C.Structure):
    _fields_ = [('BaseAddress', HANDLE), ('AllocationBase', HANDLE), ('AllocationProtect', DWORD),
                ('PartitionId', WORD), ('RegionSize', SIZE_T), ('State', DWORD), ('Protect', DWORD), ('Type', DWORD)]


class SYSTEM_INFO64(C.Structure):
    _fields_ = [('dwOemId', DWORD), ('dwPageSize', DWORD), ('lpMinimumApplicationAddress', HANDLE),
                ('lpMaximumApplicationAddress', HANDLE), ('dwActiveProcessorMask', SIZE_T),
                ('dwNumberOfProcessors', DWORD), ('dwProcessorType', DWORD), ('dwAllocationGranularity', DWORD),
                ('wProcessorLevel', WORD), ('wProcessorRevision', WORD)]


PROCESS_QUERY_INFORMATION_VM_READ = 0x410
MAX_READ = 1024 * 1024
MAX_MODULES = 4096
MAX_PATH_CHARS = 32768
MAX_ADDRESS = 0x00007fffffffffff


def _integer(value, label, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError('Invalid ' + label)
    return value


def _span(address, size, *, allow_zero=False):
    _integer(address, 'address', 0 if allow_zero else 1, MAX_ADDRESS)
    _integer(size, 'span size', 1, MAX_ADDRESS)
    if size - 1 > MAX_ADDRESS - address:
        raise ValueError('Noncanonical or overflowing memory span')


def _layout():
    if C.sizeof(SYSTEM_INFO64) != 48 or SYSTEM_INFO64.lpMaximumApplicationAddress.offset != 16 or \
            C.sizeof(HANDLE) != 8 or C.sizeof(SIZE_T) != 8 or C.sizeof(FILETIME) != 8 or C.sizeof(MODULEINFO) != 24 or \
            MODULEINFO.SizeOfImage.offset != 8 or MODULEINFO.EntryPoint.offset != 16 or \
            C.sizeof(MEMORY_BASIC_INFORMATION64) != 48 or MEMORY_BASIC_INFORMATION64.RegionSize.offset != 24 or \
            MEMORY_BASIC_INFORMATION64.Type.offset != 40:
        raise ValueError('Expected native x64 Windows ABI layouts')


def _path(buffer, length, label):
    if not 0 < length < MAX_PATH_CHARS:
        raise ValueError('Missing or truncated ' + label)
    text = buffer[:length]
    if '\0' in text or len(text) != length:
        raise ValueError('Malformed ' + label)
    return text


class _WindowsKernel:
    """Lazy system-only bindings. Merely importing this module does not load DLLs."""
    def __init__(self):
        if sys.platform != 'win32' or C.sizeof(C.c_wchar) != 2:
            raise OSError('Windows x64 backend required')
        self._library = C.WinDLL('kernel32', use_last_error=True)
        boolean = C.c_int32
        declarations = {
            'GetNativeSystemInfo': (None, [C.POINTER(SYSTEM_INFO64)]),
            'OpenProcess': (HANDLE, [DWORD, boolean, DWORD]),
            'CloseHandle': (boolean, [HANDLE]),
            'GetProcessId': (DWORD, [HANDLE]),
            'GetProcessTimes': (boolean, [HANDLE] + [C.POINTER(FILETIME)] * 4),
            'QueryFullProcessImageNameW': (boolean, [HANDLE, DWORD, C.c_wchar_p, C.POINTER(DWORD)]),
            'IsWow64Process2': (boolean, [HANDLE, C.POINTER(WORD), C.POINTER(WORD)]),
            'K32EnumProcessModulesEx': (boolean, [HANDLE, C.POINTER(HANDLE), DWORD, C.POINTER(DWORD), DWORD]),
            'K32GetModuleFileNameExW': (DWORD, [HANDLE, HANDLE, C.c_wchar_p, DWORD]),
            'K32GetModuleInformation': (boolean, [HANDLE, HANDLE, C.POINTER(MODULEINFO), DWORD]),
            'VirtualQueryEx': (SIZE_T, [HANDLE, HANDLE, C.POINTER(MEMORY_BASIC_INFORMATION64), SIZE_T]),
            'ReadProcessMemory': (boolean, [HANDLE, HANDLE, HANDLE, SIZE_T, C.POINTER(SIZE_T)]),
        }
        for name, (result, arguments) in declarations.items():
            function = getattr(self._library, name)
            function.restype, function.argtypes = result, arguments
            setattr(self, name, function)
        self.last_error = C.get_last_error


class WindowsReadOnlyProcess:
    """Internal transport, not a PID authorization boundary. Caller owns budgets.

    No region traversal, retries, fallback module path, hash verification, project
    access, process suspension, message pumping, or background work occurs here.
    Each method is synchronous: a caller deadline cannot forcibly interrupt a
    Windows API call. Close with a context manager or explicit close().
    """
    def __init__(self, pid, *, _api=None):
        _integer(pid, 'PID', 1, 0xffffffff)
        _layout()
        self.pid = pid
        self._handle = None
        self._api = _api if _api is not None else _WindowsKernel()
        handle = self._api.OpenProcess(PROCESS_QUERY_INFORMATION_VM_READ, False, pid)
        if not handle:
            self._error('OpenProcess(query/read only)')
        self._handle = handle
        try:
            self.identity()
            system = SYSTEM_INFO64()
            self._api.GetNativeSystemInfo(C.byref(system))
            self.max_address = int(system.lpMaximumApplicationAddress or 0)
            _integer(self.max_address, 'native maximum user address', 0x10000, MAX_ADDRESS)
        except BaseException as error:
            self._close_preserving(error)
            raise

    def _error(self, operation):
        raise OSError(int(self._api.last_error()), operation + ' failed')

    def _open_handle(self):
        if self._handle is None:
            raise ValueError('Read-only process handle is closed')
        return self._handle

    def close(self):
        if self._handle is not None:
            if not self._api.CloseHandle(self._handle):
                self._error('CloseHandle')
            self._handle = None

    def __enter__(self):
        self._open_handle()
        return self

    def _close_preserving(self, original):
        try:
            self.close()
        except OSError as cleanup_error:
            if original is None:
                raise
            original.add_note('Read-only process CloseHandle also failed: ' + str(cleanup_error))

    def __exit__(self, error_type, error, traceback):
        self._close_preserving(error)
        return False

    def identity(self):
        handle = self._open_handle()
        actual_pid = int(self._api.GetProcessId(handle))
        if not actual_pid:
            self._error('GetProcessId')
        if actual_pid != self.pid:
            raise ValueError('Process handle PID mismatch')
        created, exited, kernel, user = (FILETIME() for _ in range(4))
        if not self._api.GetProcessTimes(handle, C.byref(created), C.byref(exited), C.byref(kernel), C.byref(user)):
            self._error('GetProcessTimes')
        creation = (int(created.dwHighDateTime) << 32) | int(created.dwLowDateTime)
        if not creation or exited.dwHighDateTime or exited.dwLowDateTime:
            raise ValueError('Process is exited or lacks creation identity')
        process_machine, native_machine = WORD(), WORD()
        if not self._api.IsWow64Process2(handle, C.byref(process_machine), C.byref(native_machine)):
            self._error('IsWow64Process2')
        if process_machine.value != 0 or native_machine.value != 0x8664:
            raise ValueError('Expected native AMD64 target, not WOW64/ARM64')
        image, capacity = C.create_unicode_buffer(MAX_PATH_CHARS), DWORD(MAX_PATH_CHARS)
        if not self._api.QueryFullProcessImageNameW(handle, 0, image, C.byref(capacity)):
            self._error('QueryFullProcessImageNameW')
        return {'pid': actual_pid, 'creationTime': format(creation, '016x'),
                'image': _path(image, int(capacity.value), 'process image'),
                'processMachine': int(process_machine.value), 'nativeMachine': int(native_machine.value)}

    def modules(self):
        handle = self._open_handle()
        entries, needed = (HANDLE * MAX_MODULES)(), DWORD()
        capacity = C.sizeof(entries)
        if not self._api.K32EnumProcessModulesEx(handle, entries, capacity, C.byref(needed), 3):
            self._error('EnumProcessModulesEx')
        if not needed.value or needed.value > capacity or needed.value % C.sizeof(HANDLE):
            raise ValueError('Missing, partial or oversized loaded module list')
        result, seen = [], set()
        for module in entries[:needed.value // C.sizeof(HANDLE)]:
            if not module or module in seen:
                raise ValueError('Null or duplicate loaded module')
            seen.add(module)
            image = C.create_unicode_buffer(MAX_PATH_CHARS)
            length = int(self._api.K32GetModuleFileNameExW(handle, module, image, MAX_PATH_CHARS))
            if not length:
                self._error('GetModuleFileNameExW')
            path = _path(image, length, 'loaded module path')
            info = MODULEINFO()
            if not self._api.K32GetModuleInformation(handle, module, C.byref(info), C.sizeof(info)):
                self._error('GetModuleInformation')
            base, size = int(info.lpBaseOfDll or 0), int(info.SizeOfImage)
            _span(base, size)
            if base != module:
                raise ValueError('Loaded module base identity changed')
            result.append({'base': base, 'size': size, 'path': path})
        return result

    def region(self, address):
        handle = self._open_handle()
        _integer(address, 'query address', 0, MAX_ADDRESS)
        info = MEMORY_BASIC_INFORMATION64()
        actual = int(self._api.VirtualQueryEx(handle, HANDLE(address), C.byref(info), C.sizeof(info)))
        if not actual:
            self._error('VirtualQueryEx')
        if actual != C.sizeof(info):
            raise ValueError('Incomplete VirtualQueryEx record')
        base, size = int(info.BaseAddress or 0), int(info.RegionSize)
        _span(base, size, allow_zero=True)
        if not base <= address < base + size:
            raise ValueError('VirtualQueryEx returned a noncontaining region')
        return {'base': base, 'size': size, 'allocationBase': int(info.AllocationBase or 0),
                'state': int(info.State), 'protect': int(info.Protect), 'type': int(info.Type)}

    def read(self, address, size):
        handle = self._open_handle()
        _integer(size, 'read size', 1, MAX_READ)
        _span(address, size)
        storage, got = (C.c_ubyte * size)(), SIZE_T()
        okay = self._api.ReadProcessMemory(handle, HANDLE(address), storage, size, C.byref(got))
        if not okay or got.value != size:
            self._error('ReadProcessMemory exact bounded span')
        return bytes(storage)
