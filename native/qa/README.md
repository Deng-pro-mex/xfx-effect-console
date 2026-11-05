# Experimental parameter-localization QA (not production)

These sources are **not linked into the console AEX or installed by its installer**.
They do not implement parameter translation yet. The installed console remains
1.0.28. Do not treat an offline test, source report, or imported JSON as authority
to write a real project's parameter labels.

## Read-only host identity helper

`qa_host_identity.cpp` builds a separate `qa_host_identity.dll` for the documented
ExtendScript ExternalObject ABI. It reports the current AE process ID, creation
time, executable path/version, and SHA256 of the files backing the *already loaded*
FLT/TDB/U module paths. It rejects non-AE processes, missing modules, foreign
module directories, version/path mismatches, bad Unicode, and thread/lifecycle
misuse. It never loads Adobe DLLs, scans a project, writes files, uses the network,
or mutates AE. Test-only providers are excluded from the production DLL.

File hashes describe disk observations, **not executable-page attestation**.
Read-only file and directory handles remain pinned through identity collection;
normal write/rename replacement is refused during that observation. File sizes
and allocations are bounded, but synchronous file/version/CNG calls have **no
wall-clock timeout** and may block a host thread. The helper has now returned
matching identity in AE 25.4x86 and unloaded successfully during an isolated QA
baseline. That observation is not a timing guarantee or private ABI acceptance.
Do not add an unbounded pinned worker or terminate a user's host as an automatic
timeout workaround.

`build-qa-host-identity.ps1` compiles/runs offline ABI tests, including loading our
own helper into a non-AE test process and confirming it refuses that process. It
does **not** launch AE. Before each build/run it requires all AfterFX* processes
to be absent. Existing approvals and the user's saved/exit confirmation still
apply; do not remove the guard to run it beside a user project.

## Structural candidate core

`qa_parameter_inspect.cpp` is a pure-core test-included implementation with an
injected read-only Memory interface. It has no exports, DLL, live scanner,
OpenProcess/ReadProcessMemory implementation, private host function calls, or
writes. It compares supplied identity values to one disk-observed 25.4.0.86
candidate, bounds reads/regions/nodes/time and validates structural relationships.
The caller-supplied hashes are **not independently verified evidence**.

Reports explicitly retain runtimeEnabled=false, hostVerified=false,
publicIdentityVerified=false, structuralCandidatesOnly=true and
liveScannerAvailable=false. Raw GUIDs, pointers and PF name bytes are candidate
QA evidence only; do not put them in portable dictionaries. A common root vtable
and four attached roots do not prove those roots are the four public Slider
Control effects. The named-list order does not prove parameter index one. A
future binding must independently establish ownership and public identity before
any experimental write is considered.

## External read-only observation (offline tests and isolated QA evidence)

[qa_process_memory.py](../../scripts/qa_process_memory.py) is an internal Windows
transport with no CLI or import-time IO. It opens only query/read permissions
0x410, checks native x64 process identity, enumerates loaded modules and single
memory regions, and rejects incomplete reads. Its 18 tests include a real read
of a known buffer in the test's own Python process, **not AE**. It supplies no
project authorization, disk hash, write operation or private function call.

[qa_parameter_observer.py](../../scripts/qa_parameter_observer.py) consumes an
injected reader. It bounds eligible committed private RW discovery to 2GiB /
20 cooperative seconds, 8192 reads, 65536 regions, 256 hits, 64 groups and 4096
nodes. It must finish its declared scope, validates candidate parent/list/string
shapes and re-reads guards, and correlates root display names only as candidates.
Transport/budget failures cannot be swallowed as shape rejections. Its 14 tests
use fake memory only. Runtime/host/parameter-association verification flags stay
false. There is no process-opening or CLI action in this module either.

Snapshot-only requests may carry a 32-lowercase-hex sampleId for fresh immutable
receipts. Create/saveCopy cannot use it to bypass their one-shot output guard.
An imported receipt still cannot authorize AE writes. A trusted coordinator must
bind the new challenge to actual process/module identity and compare independent
before/after public snapshots; no atomic project-ownership proof is implied.

After an earlier user-work pause, the user confirmed AE had exited. The Lead
observed zero AfterFX processes and launched a NEW owned R6 QA process. Two
query/read-only observations completed their declared eligible scope (~1.84GB
per run). Fresh before/after public snapshots matched the fixture exactly. But
two different parades each correlated to the same four effect names, and their
first named children had zero PF-name bytes. No unique parameter association
was established. No label, value, keyframe or expression was written by the
observer; no private function was called. The actual observations are failure
evidence, not an enabled adapter.

The user explicitly requested risk avoidance: do not choose an arbitrary alias
or weaken layout guards to force a pass. All name-correlated groups participate
in ambiguity rejection, including a group whose child layout is rejected. Raw
failure diagnostics remain QA-only. The enclosing named container has disk
qword size-maintenance evidence: a u32-returning getter alone was NOT grounds
to change the structural size read. List iteration order is not public index.
Do not reuse historical Fixture/Reopened receipts after any project switch.

## Canonical topology and native-byte previews (R7)

[qa_parameter_topology.py](../../scripts/qa_parameter_topology.py) is an
injected-reader diagnostic with 7 FakeMemory tests. It joins canonical template
stream GUIDs to validated live ring-list children instead of equating ring order
with public indices. Canonical parents are not falsely required to be the live
root. It records all child PF bytes, including empty internal/group fields,
without interpreting those as valid writable labels. Every result is diagnostic
only; active ownership, public index mapping and parameter-write authorization
remain unverified.

An isolated, identity-bound R7 run read just 10,664 bytes across both previously
observed groups. Their before/after public snapshots matched exactly. Each root
had three canonical/live children versus two public properties. The observed
Slider label was in internal slot 1, not automatically publicIndex-1. This is a
single-fixture correlation, not a general index rule; both parades still exist.

[qa_parameter_encoding.py](../../scripts/qa_parameter_encoding.py) is pure offline
analysis/preview with 9 tests. All eight observed Slider PF buffers began
`bbacbfe9`, corresponding to the Unicode display label in CP936, not UTF-8. This
only distinguishes the module's limited UTF-8/CP936 candidate set, not all global
host encoding behavior. Portable lexicons stay UTF-8, while eventual native
field conversion requires a separately verified exact-build profile.

ASCII evidence is ambiguous; unknown codecs, loss, best-fit substitution,
truncation and nonroundtripping text are rejected. Full original 32-byte buffers,
including any nonzero tail, remain untouched in diagnostic data for exact future
restoration. New padded bytes are previews only. No function writes a target,
selects an active alias or turns a receipt/codec match into an enabled adapter.

## R8 delivery decision: public read-only reference, not a private writer

The user explicitly chose a **read-only Chinese reference and offline lexicon
manager**, leaving in-place localization disabled. The public metadata reader,
reference window and lexicon/store in `native/src/` are now included in the local
AEX build. The experimental sources in THIS `qa/` directory, external memory
observers and private ABI/writer code remain excluded. In R9 the user approved
installation plus isolated QA, followed by normal Windows UAC. Only the verified
AEX was replaced; the old AEX was backed up and personal/other installed files
remained byte-identical. Installed SHA256:
`85e6ec22af3532edcfcc6491a54eedb28aa68c72719ea0a803b2969763bc4dac`.

In a NEW isolated-profile AE 25.4x86 session, the real public reader and native
window collected 20 fixture rows (8 groups, 4 Sliders included). Saving a local
reference translation left all measured public names, matchNames, keys, eases,
expressions, evaluated 10/20/30 samples, IDs and dirty=false identical to baseline.
This accepts a bounded read-only fixture case, not in-place localization, popup
options, arbitrary third-party UI coverage or private ABI ownership.

The reader only collects names/matchNames/group metadata from one explicitly
selected layer after a UI request, queued to AE Idle. It does not read values or
expressions, change any host field, call private code, open/close/save projects,
perform file IO or use the network. File IO belongs only to the separate local
lexicon manager. See [safe-reference instructions](../../PARAMETER-REFERENCE.md).

The pure offline [recovery-history audit](../../scripts/qa_parameter_recovery.py)
adds 10 tests for retaining the CURRENT potentially partially failed operation,
reverse recovery review and exact original PF32/display verification. It is not
an executor, genuine journal, ownership proof or demonstrated host rollback.
All write and journal-deletion authorization remains false.

## Next boundary

The parameter QA factory and relay in `scripts/` have now run in isolated AE
25.4x86: create/save an empty-project fixture, obtain a second identical snapshot,
and Save As the owned copy. The four Slider effects and source's two keys return
10/20/30 via name, matchName and index expressions, with dirty=false. This is
baseline evidence only: no parameter label was changed.
The factory has no label writer, close/open/newProject action or general code
evaluation route. A filename `Reopened.aep` is not evidence that a reload happened.
Native parameter ABI, post-translation expressions/dirty/undo behavior,
save/reopen persistence, render equivalence, UI/group coverage and reliable
recovery remain unverified.

One lifecycle observation matters to an eventual coordinator: a newly launched
Windows `-r` session reported `app.exitAfterLaunchAndEval === true`. In our own
empty diagnostic process, setting this session property false kept AE running
following script completion. Never apply such lifecycle changes silently to a
user-owned session. `WaitForInputIdle` or the short relay's exit alone is not
proof that the target script ran; require its unique, identity-bound receipt.
ExtendScript also reported the temporary directory using a short `ADMINI~1`
path. Do not treat a long/short path alias as an independently proven file identity.

The reference localizer is MIT (Copyright (c) 2026 verse); adapted layout checks
retain its notice. The full notice is also in
`../third_party/After-Effects-Plugin-Localizer.LICENSE.txt`. This is not an Adobe
SDK license or a guarantee of supported private ABI behavior.
