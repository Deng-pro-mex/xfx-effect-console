[CmdletBinding()]
param([string]$ProjectRoot, [string]$OutputDirectory)
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
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$msvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$compiler = Join-Path $msvc 'bin\Hostx64\x64\cl.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'Existing compiler missing; no dependencies will be installed.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $ProjectRoot 'native\build\qa-parameter-inspect-purecore' }
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$tests = Join-Path $output 'qa-parameter-inspect-tests.exe'
$common = @('/nologo','/utf-8','/std:c++20','/EHsc','/W4','/WX','/MT',
    "/I$msvc\include", "/I$kit\Include\$version\ucrt", "/I$kit\Include\$version\shared", "/I$kit\Include\$version\um")
$link = @('/link','/MANIFESTUAC:NO', "/LIBPATH:$msvc\lib\x64", "/LIBPATH:$kit\Lib\$version\um\x64", "/LIBPATH:$kit\Lib\$version\ucrt\x64")
Push-Location $output
try {
    Assert-NoAfterEffects
    & $compiler @common (Join-Path $ProjectRoot 'native\qa\qa_parameter_inspect_tests.cpp') "/Fe$tests" @link
    if ($LASTEXITCODE -ne 0) { throw "QA pure-core test build failed: $LASTEXITCODE" }
    Assert-NoAfterEffects
    & $tests
    if ($LASTEXITCODE -ne 0) { throw "QA pure-core tests failed: $LASTEXITCODE" }
    Write-Host 'Pure-core FakeMemory only: no DLL, exports, Windows memory adapter, AE access, installation or enabling artifact.'
} finally { Pop-Location }
