#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$VcpkgRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows) { throw 'Build this application on Windows x64 with Visual Studio 2022.' }
if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne [System.Runtime.InteropServices.Architecture]::X64) {
    throw 'Use an x64 Windows host: this Flutter version selects the Windows target architecture from its host.'
}
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
$VcpkgRoot = (Resolve-Path $VcpkgRoot).Path
$installed = Join-Path $root 'build/windows-vcpkg'
$native = Join-Path $root 'build/windows-native'
$bundle = Join-Path $root 'app/build/windows/x64/runner/Release'
$artifacts = Join-Path $root 'artifacts'
Invoke-Checked python @('scripts/generate_luts.py')
Invoke-Checked (Join-Path $VcpkgRoot 'vcpkg.exe') @('install', '--triplet=x64-windows', "--x-manifest-root=$root/platform/windows", "--x-install-root=$installed")
Invoke-Checked cmake @('-S', '.', '-B', $native, '-G', 'Visual Studio 17 2022', '-A', 'x64', "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake", '-DVCPKG_MANIFEST_MODE=OFF', "-DVCPKG_INSTALLED_DIR=$installed", '-DVCPKG_TARGET_TRIPLET=x64-windows', '-DPE_BUILD_TESTS=OFF', '-DPE_BUILD_CLI=OFF')
Invoke-Checked cmake @('--build', $native, '--config', 'Release', '--target', 'photo_core', '--parallel', '4')
Push-Location app
try {
    Invoke-Checked flutter @('pub', 'get')
    # Flutter 3.47.7 selects Windows x64 on an x64 host; it has no --target-platform option.
    Invoke-Checked flutter @('build', 'windows', '--release', '--no-pub')
} finally { Pop-Location }
# Install rules in app/windows copy the core and all shared codec dependencies.
foreach ($required in @('photo_enhancement.exe', 'photo_core.dll', 'flutter_windows.dll', 'data/flutter_assets')) {
    if (-not (Test-Path (Join-Path $bundle $required))) { throw "Missing application file: $required" }
}
# App-local CRT deployment also supports machines without a preinstalled VC runtime.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Visual Studio C++ tools not found' }
$crt = Get-ChildItem "$vs/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT" -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw 'Visual C++ redistributable DLL directory not found' }
Copy-Item (Join-Path $crt.FullName '*.dll') $bundle -Force
foreach ($dll in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    if (-not (Test-Path (Join-Path $bundle $dll))) { throw "Missing Visual C++ runtime: $dll" }
}
Invoke-Checked python @('scripts/verify_windows_bundle.py', $bundle)
$licenses = Join-Path $bundle 'third-party-licenses'
New-Item $licenses -ItemType Directory -Force | Out-Null
Get-ChildItem "$installed/x64-windows/share/*/copyright" | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $licenses ($_.Directory.Name + '.txt')) -Force
}
Copy-Item 'third_party/LibRaw/LICENSE.CDDL' (Join-Path $licenses 'LibRaw-CDDL.txt') -Force
Copy-Item 'third_party/LibRaw/LICENSE.LGPL' (Join-Path $licenses 'LibRaw-LGPL.txt') -Force
Copy-Item 'platform/windows/vcpkg.json' (Join-Path $licenses 'vcpkg-manifest.json') -Force
$dependencyCommit = & git -C $VcpkgRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record vcpkg source commit' }
$dependencyCommit | Set-Content (Join-Path $licenses 'vcpkg-commit.txt') -Encoding utf8
Copy-Item 'docs/第三方依赖.md' (Join-Path $licenses 'dependency-sources.md') -Force
Copy-Item 'platform/windows/NOTICE.txt' (Join-Path $bundle 'NOTICE.txt') -Force
# Flutter includes its package license registry in flutter_assets/NOTICES.Z.
New-Item $artifacts -ItemType Directory -Force | Out-Null
$versionLine = Get-Content 'app/pubspec.yaml' | Where-Object { $_ -match '^version:\s*' }
if ($versionLine -notmatch '^version:\s*(\d+)\.(\d+)\.(\d+)') { throw 'Invalid pubspec application version' }
$version = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
if ([int]$Matches[1] -gt 255 -or [int]$Matches[2] -gt 255 -or [int]$Matches[3] -gt 65535) {
    throw 'Application version exceeds Windows Installer version limits'
}
$cmakeBundle = $bundle.Replace('\', '/')
Invoke-Checked cmake @('-S', 'platform/windows', '-B', 'build/windows-installer', "-DPE_APP_DIR:PATH=$cmakeBundle", "-DPE_APP_VERSION=$version")
try {
    Invoke-Checked cpack @('--config', 'build/windows-installer/CPackConfig.cmake', '-G', 'WIX', '-B', $artifacts)
} catch {
    # CPack's summary omits the actual WiX compiler/linker error.
    Get-ChildItem (Join-Path $artifacts '_CPack_Packages') -Filter 'wix.log' -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object {
            Write-Host "::group::WiX diagnostics: $($_.FullName)"
            Get-Content -LiteralPath $_.FullName -ErrorAction Continue | Out-Host
            Write-Host '::endgroup::'
        }
    throw
}
Compress-Archive -Path "$bundle/*" -DestinationPath "$artifacts/photo-enhancement-windows-x64.zip" -Force
Write-Host "Windows application and MSI: $artifacts"
