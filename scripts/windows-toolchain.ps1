# Resolve this workstation's tools without relying on a preconfigured PATH.
function Resolve-BitPadWindowsToolchain {
    param([string]$Repo, [string]$QtRoot, [string]$QtCompatRoot)
    if ($env:OS -ne 'Windows_NT') { throw 'Run the Windows build in Windows PowerShell, not inside WSL.' }
    $localPath = Join-Path $Repo 'tooling/windows.local.json'
    $local = $null
    if (Test-Path -LiteralPath $localPath) {
        $local = Get-Content -LiteralPath $localPath -Raw | ConvertFrom-Json
    }
    $explicitQt = $QtRoot
    if (!$QtRoot) { $QtRoot = $env:BIT8PAD_QT_ROOT }
    if (!$QtRoot -and $local) { $QtRoot = $local.QtRoot }
    if (!$QtRoot) {
        $candidates = @(
            (Join-Path $Repo 'tooling/qt-sdk'),
            (Join-Path (Split-Path -Parent $Repo) '8bitShell/build/tooling/qt-sdk')
        )
        foreach ($candidate in $candidates) {
            if (Test-Path -LiteralPath (Join-Path $candidate 'lib/cmake/Qt6/Qt6Config.cmake')) {
                $QtRoot = $candidate
                break
            }
        }
    }
    if (!$QtRoot) {
        foreach ($root in @('C:/Qt', 'D:/Qt')) {
            $sdk = Get-ChildItem -Path "$root/*/msvc*_64" -Directory -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | Select-Object -First 1
            if ($sdk) { $QtRoot = $sdk.FullName; break }
        }
    }
    if (!$QtRoot -or !(Test-Path -LiteralPath (Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'))) {
        throw "Qt MSVC SDK not found. Pass -QtRoot, set BIT8PAD_QT_ROOT, or edit $localPath. Do not infer absence from PATH alone. Requested SDK: $explicitQt"
    }
    $QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
    if (!$QtCompatRoot) { $QtCompatRoot = $env:BIT8PAD_QT_COMPAT_ROOT }
    # An explicit SDK override must not inherit another SDK's local overlay.
    if (!$QtCompatRoot -and $local -and $local.QtRoot -and
        [IO.Path]::GetFullPath($local.QtRoot).TrimEnd('\') -eq $QtRoot.TrimEnd('\')) {
        $QtCompatRoot = $local.QtCompatRoot
    }
    if (!$QtCompatRoot -and !(Test-Path -LiteralPath (Join-Path $QtRoot 'lib/cmake/Qt6Core5Compat/Qt6Core5CompatConfig.cmake'))) {
        $candidate = Join-Path $Repo 'tooling/qt-compat'
        if (Test-Path -LiteralPath $candidate) { $QtCompatRoot = $candidate }
    }
    $compatSdk = $QtRoot
    if ($QtCompatRoot) {
        $QtCompatRoot = (Resolve-Path -LiteralPath $QtCompatRoot).Path
        $compatSdk = $QtCompatRoot
    }
    foreach ($relative in @('lib/cmake/Qt6Core5Compat/Qt6Core5CompatConfig.cmake', 'bin/Qt6Core5Compat.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $compatSdk $relative))) {
            throw "Matching Qt Core5Compat is missing: $compatSdk/$relative. Set -QtCompatRoot or tooling/windows.local.json."
        }
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe was not found.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'Visual Studio C++ Build Tools were not found.' }
    $cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    $ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
    foreach ($name in @('cmake', 'ninja')) {
        $path = Get-Variable -Name $name -ValueOnly
        if (!(Test-Path -LiteralPath $path)) {
            $command = Get-Command "$name.exe" -ErrorAction SilentlyContinue
            if (!$command) { throw "$name.exe was not found in Visual Studio or PATH. Install the C++ CMake tools component." }
            Set-Variable -Name $name -Value $command.Source
        }
    }
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    if (!(Test-Path -LiteralPath $ctest)) { throw "ctest.exe was not found beside $cmake" }
    $qmake = Join-Path $QtRoot 'bin/qmake6.exe'
    if (!(Test-Path -LiteralPath $qmake)) { $qmake = Join-Path $QtRoot 'bin/qmake.exe' }
    [pscustomobject]@{ QtRoot = $QtRoot; QtCompatRoot = $QtCompatRoot; VisualStudio = $vs; CMake = $cmake; CTest = $ctest; Ninja = $ninja; QMake = $qmake; LocalConfig = $localPath }
}
