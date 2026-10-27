# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param([string]$Installer = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $Installer) { $Installer = Join-Path $PSScriptRoot '..\..\install.ps1' }
$Installer = (Resolve-Path -LiteralPath $Installer).Path
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('8bitpad-bootstrap-test-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($testRoot)
$encoding = [Text.UTF8Encoding]::new($false)
$testCount = 0

function Assert-That([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
}
function New-Fixture([string]$Name, [string]$ReleaseVersion = '1.2.3') {
    $directory = Join-Path $testRoot $Name
    [void][IO.Directory]::CreateDirectory($directory)
    $asset = "8bitPad-$ReleaseVersion-windows-x64.msi"
    [IO.File]::WriteAllText((Join-Path $directory 'release-tag.txt'), "v$ReleaseVersion`n", $encoding)
    [IO.File]::WriteAllText((Join-Path $directory $asset), 'Local checksum fixture only; not a real MSI.', $encoding)
    $hash = (Get-FileHash -LiteralPath (Join-Path $directory $asset) -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $directory 'SHA256SUMS.txt'), "$hash  $asset`n", $encoding)
    return $directory
}
function Invoke-TestInstaller([string]$Shell, [string]$Name, [string[]]$Arguments, [bool]$Success) {
    $stdout = Join-Path $testRoot ($Name + '.out.txt')
    $stderr = Join-Path $testRoot ($Name + '.err.txt')
    $tokens = @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $Installer) + $Arguments
    $quoted = ($tokens | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
    $process = Start-Process -FilePath $Shell -ArgumentList $quoted -RedirectStandardOutput $stdout -RedirectStandardError $stderr -Wait -PassThru -WindowStyle Hidden
    $output = [IO.File]::ReadAllText($stdout) + [IO.File]::ReadAllText($stderr)
    if ($Success) { Assert-That ($process.ExitCode -eq 0) "$Name should succeed: $output" }
    else { Assert-That ($process.ExitCode -ne 0) "$Name must fail closed: $output" }
    $script:testCount++
}

try {
    $shells = @((Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'))
    $pwsh = Get-Command pwsh -ErrorAction SilentlyContinue
    if ($pwsh -and $shells -notcontains $pwsh.Source) { $shells += $pwsh.Source }
    foreach ($shell in $shells) {
        $shellName = [IO.Path]::GetFileNameWithoutExtension($shell)
        $fixture = New-Fixture ($shellName + '-valid')
        $destination = Join-Path $testRoot ($shellName + '-downloads with spaces')
        $base = @('-Repository', 'fixture-owner/8bitpad', '-FixtureDirectory', $fixture)
        Invoke-TestInstaller $shell ($shellName + '-download') ($base + @('-Version', 'v1.2.3', '-DownloadOnly', '-DownloadDirectory', $destination)) $true
        Assert-That (Test-Path -LiteralPath (Join-Path $destination '8bitPad-1.2.3-windows-x64.msi')) 'Verified fixture should be copied.'
        Invoke-TestInstaller $shell ($shellName + '-latest') ($base + @('-DownloadOnly', '-DownloadDirectory', $destination)) $true
        Invoke-TestInstaller $shell ($shellName + '-fixture-install-refused') $base $false
        Invoke-TestInstaller $shell ($shellName + '-version-traversal') ($base + @('-Version', '../../escape', '-DownloadOnly')) $false
        Invoke-TestInstaller $shell ($shellName + '-version-mismatch') ($base + @('-Version', '1.2.4', '-DownloadOnly')) $false
        Invoke-TestInstaller $shell ($shellName + '-placeholder') @('-Repository', 'OWNER/8bitpad', '-DownloadOnly', '-FixtureDirectory', $fixture) $false
        Invoke-TestInstaller $shell ($shellName + '-bad-repository') @('-Repository', 'owner/repo/extra', '-DownloadOnly', '-FixtureDirectory', $fixture) $false
        $whatIfDestination = Join-Path $testRoot ($shellName + '-whatif-does-not-exist')
        Invoke-TestInstaller $shell ($shellName + '-whatif') ($base + @('-WhatIf', '-DownloadOnly', '-DownloadDirectory', $whatIfDestination)) $true
        Assert-That (-not (Test-Path -LiteralPath $whatIfDestination)) 'WhatIf must not create destination directories.'

        $badHash = New-Fixture ($shellName + '-bad-hash')
        [IO.File]::AppendAllText((Join-Path $badHash '8bitPad-1.2.3-windows-x64.msi'), 'tampered', $encoding)
        $badDestination = Join-Path $testRoot ($shellName + '-bad-output')
        Invoke-TestInstaller $shell ($shellName + '-hash-rejected') @('-Repository', 'fixture-owner/8bitpad', '-FixtureDirectory', $badHash, '-DownloadOnly', '-DownloadDirectory', $badDestination) $false
        Assert-That (-not (Test-Path -LiteralPath $badDestination)) 'Hash failure must not create a destination.'

        $duplicate = New-Fixture ($shellName + '-duplicate')
        $sumPath = Join-Path $duplicate 'SHA256SUMS.txt'
        $line = [IO.File]::ReadAllText($sumPath)
        [IO.File]::WriteAllText($sumPath, $line + $line, $encoding)
        Invoke-TestInstaller $shell ($shellName + '-duplicate-rejected') @('-Repository', 'fixture-owner/8bitpad', '-FixtureDirectory', $duplicate, '-DownloadOnly', '-DownloadDirectory', $badDestination) $false

        $wrongName = New-Fixture ($shellName + '-wrong-name')
        $sumPath = Join-Path $wrongName 'SHA256SUMS.txt'
        [IO.File]::WriteAllText($sumPath, [IO.File]::ReadAllText($sumPath).Replace('.msi', '.msi.extra'), $encoding)
        Invoke-TestInstaller $shell ($shellName + '-exact-name-required') @('-Repository', 'fixture-owner/8bitpad', '-FixtureDirectory', $wrongName, '-DownloadOnly', '-DownloadDirectory', $badDestination) $false

        $missing = New-Fixture ($shellName + '-missing')
        Remove-Item -LiteralPath (Join-Path $missing '8bitPad-1.2.3-windows-x64.msi')
        Invoke-TestInstaller $shell ($shellName + '-missing-asset') @('-Repository', 'fixture-owner/8bitpad', '-FixtureDirectory', $missing, '-DownloadOnly', '-DownloadDirectory', $badDestination) $false

        $target = Join-Path $destination '8bitPad-1.2.3-windows-x64.msi'
        [IO.File]::WriteAllText($target, 'Existing user file; preserve me.', $encoding)
        Invoke-TestInstaller $shell ($shellName + '-existing-preserved') ($base + @('-DownloadOnly', '-DownloadDirectory', $destination)) $false
        Assert-That ([IO.File]::ReadAllText($target) -ceq 'Existing user file; preserve me.') 'Different existing downloads must remain unchanged.'
    }
    Write-Output "PASS: $testCount Windows bootstrap cases; no MSI installation or real application launch performed."
}
finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $expected = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolved.StartsWith($expected, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolved) -match '^8bitpad-bootstrap-test-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
