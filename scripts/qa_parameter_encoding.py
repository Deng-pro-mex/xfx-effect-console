"""Pure offline byte-encoding diagnostics and previews, NOT a host writer.

Portable lexicons stay UTF-8. The native PF name may use a different encoding
on an exact AE build. A matching original string is only evidence for that
observation, not a verified host ABI/profile or authorization to write memory.
No lossy conversion, best-fit substitution, silent truncation or codec guessing
is permitted when preparing a preview. No IO, Windows API, process or CLI here.
"""
CANDIDATES=('utf-8','cp936')


def _text(value):
    if not isinstance(value,str) or not value or any(ord(char)<32 or 0x7f<=ord(char)<=0x9f or 0xd800<=ord(char)<=0xdfff for char in value):
        raise ValueError('Empty, nontext, control or invalid Unicode label')
    return value


def inspect_encoding(original_raw32,display):
    if not isinstance(original_raw32,bytes) or len(original_raw32)!=32 or not 1<=original_raw32.find(b'\0')<=31:
        raise ValueError('Expected a nonempty NUL-terminated 32-byte PF name')
    _text(display)
    original=original_raw32.split(b'\0',1)[0]
    matches=[]
    for codec in CANDIDATES:
        try:
            decoded=original.decode(codec,errors='strict')
            if decoded==display and decoded.encode(codec,errors='strict')==original:matches.append(codec)
        except UnicodeError:
            pass
    return {'matchingCandidates':matches,'uniqueObservedEncoding':len(matches)==1,
        'originalRaw32Hex':original_raw32.hex(),'originalNativeByteCount':len(original),
        'candidateSetLimited':True,'runtimeEnabled':False,'hostVerified':False,'writeAuthorized':False}


def prepare_label_bytes(label,codec):
    """Return explicit-codec bytes for review. Never selects a host profile."""
    _text(label)
    if codec not in CANDIDATES:raise ValueError('Explicit supported codec required')
    portable=label.encode('utf-8',errors='strict')
    if len(portable)>31:raise ValueError('Portable translation exceeds 31 UTF8 bytes')
    try:
        native=label.encode(codec,errors='strict')
        if native.decode(codec,errors='strict')!=label:raise ValueError('Native encoding does not roundtrip exactly')
    except UnicodeError as error:
        raise ValueError('Native label is not representable without loss') from error
    if not 1<=len(native)<=31 or b'\0' in native:raise ValueError('Native label exceeds bounded PF name')
    return {'label':label,'codec':codec,'utf8ByteCount':len(portable),'nativeByteCount':len(native),
        'nativeRaw32Hex':(native+b'\0'*(32-len(native))).hex(),
        'previewOnly':True,'runtimeEnabled':False,'hostVerified':False,'writeAuthorized':False}
