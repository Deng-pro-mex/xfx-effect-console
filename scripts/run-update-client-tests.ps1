$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$outDir = Join-Path $root 'native\build\update-client-tests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
# Dedicated output/object directory; no AE SDK, no online file execution.
$compile = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W4 /WX /DUNICODE /D_UNICODE `"$root\native\tests\update_client_tests.cpp`" /Fe:tests.exe winhttp.lib bcrypt.lib advapi32.lib"
Push-Location $outDir
try {
    cmd.exe /c $compile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & .\tests.exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    # Compile the real (non-fixture) transport too; tests include the .cpp intentionally.
    cmd.exe /c "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W4 /WX /DUNICODE /D_UNICODE /c `"$root\native\src\update_client.cpp`" /Fo:update_client.obj"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }
