[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
function Assert-NoAfterEffects {
    $running = @(Get-Process -Name 'AfterFX*' -ErrorAction SilentlyContinue)
    if ($running.Count -ne 0) {
        $names = ($running | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ', '
        throw "After Effects is running ($names). Save and exit AE yourself; this script never closes it."
    }
    Write-Host 'AE process guard: 0 AfterFX* processes.'
}
Assert-NoAfterEffects
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$msvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$compiler = Join-Path $msvc 'bin\Hostx64\x64\cl.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'Existing MSVC compiler missing; no dependencies will be installed.' }
$output = Join-Path $ProjectRoot 'native\build\qa-host-identity'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$dll = Join-Path $output 'qa_host_identity.dll'
$tests = Join-Path $output 'qa-host-identity-tests.exe'
$common = @('/nologo','/utf-8','/std:c++20','/EHsc','/W4','/WX','/MT','/DUNICODE','/D_UNICODE',
    "/I$msvc\include", "/I$kit\Include\$version\ucrt", "/I$kit\Include\$version\shared", "/I$kit\Include\$version\um")
$link = @('/link','/MANIFESTUAC:NO', "/LIBPATH:$msvc\lib\x64", "/LIBPATH:$kit\Lib\$version\um\x64", "/LIBPATH:$kit\Lib\$version\ucrt\x64",'bcrypt.lib','version.lib')
Push-Location $output
try {
    Assert-NoAfterEffects
    & $compiler @common '/LD' (Join-Path $ProjectRoot 'native\qa\qa_host_identity.cpp') "/Fe$dll" @link
    if ($LASTEXITCODE -ne 0) { throw "QA identity DLL build failed: $LASTEXITCODE" }
    Assert-NoAfterEffects
    & $compiler @common (Join-Path $ProjectRoot 'native\qa\qa_host_identity_tests.cpp') "/Fe$tests" @link
    if ($LASTEXITCODE -ne 0) { throw "QA identity test build failed: $LASTEXITCODE" }
    $expectedHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-NoAfterEffects
    & $tests $dll $expectedHash
    if ($LASTEXITCODE -ne 0) { throw "QA identity tests failed: $LASTEXITCODE" }
    Write-Host "Read-only QA DLL built (not installed): $dll"
} finally { Pop-Location }
