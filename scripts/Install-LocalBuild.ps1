# SPDX-License-Identifier: GPL-2.0-or-later
[CmdletBinding()]
param(
    [string]$BuildDirectory = '',
    [string]$InstallRoot = (Join-Path $env:ProgramData 'obs-studio\plugins')
)

$ErrorActionPreference = 'Stop'
if (-not $BuildDirectory) {
    $BuildDirectory = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'obs-auto-resize-output')) {
        $PSScriptRoot
    } else {
        Join-Path $PSScriptRoot '..\artifacts\audio-setup-check'
    }
}
if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
    throw 'Close OBS before installing: Windows locks the loaded plugin DLL.'
}

$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$pluginName = 'obs-auto-resize-output'
$sourceRoot = Join-Path $buildRoot $pluginName
$targetRoot = Join-Path ([IO.Path]::GetFullPath($InstallRoot)) $pluginName
$relativeFiles = @('bin\64bit\obs-auto-resize-output.dll', 'data\locale\en-US.ini')
foreach ($relativeFile in $relativeFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $relativeFile) -PathType Leaf)) {
        throw "Missing build file: $relativeFile"
    }
}

$backupRoot = Join-Path $buildRoot ('backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$replaced = @()
try {
    foreach ($relativeFile in $relativeFiles) {
        if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
            throw 'OBS started during installation. Close it before retrying.'
        }
        $sourceFile = Join-Path $sourceRoot $relativeFile
        $targetFile = Join-Path $targetRoot $relativeFile
        $backupFile = Join-Path $backupRoot $relativeFile
        $existed = Test-Path -LiteralPath $targetFile -PathType Leaf
        if ($existed) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $backupFile) -Force | Out-Null
            Copy-Item -LiteralPath $targetFile -Destination $backupFile
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $targetFile) -Force | Out-Null
        $replaced += [pscustomobject]@{ Target = $targetFile; Backup = $backupFile; Existed = $existed }
        Copy-Item -LiteralPath $sourceFile -Destination $targetFile -Force
        if ((Get-FileHash -LiteralPath $sourceFile).Hash -ne (Get-FileHash -LiteralPath $targetFile).Hash) {
            throw "Installed file verification failed: $relativeFile"
        }
    }
} catch {
    $installError = $_
    foreach ($replacement in $replaced) {
        if ($replacement.Existed) {
            Copy-Item -LiteralPath $replacement.Backup -Destination $replacement.Target -Force
        } elseif (Test-Path -LiteralPath $replacement.Target -PathType Leaf) {
            Remove-Item -LiteralPath $replacement.Target
        }
    }
    throw $installError
}

Write-Output 'Installed and verified Scene Output Control. Start OBS to load the new build.'
Write-Output "Previous files, if present, are backed up in: $backupRoot"
