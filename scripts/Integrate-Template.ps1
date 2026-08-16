# SPDX-License-Identifier: GPL-2.0-or-later
#
# Integrate-Template.ps1
#
# Pulls the official obs-plugintemplate build infrastructure (cmake/ helpers,
# CMakePresets.json, buildspec.json, GitHub Actions, clang-format) INTO this
# repository, then patches it for this plugin:
#   * buildspec.json   -> our name / displayName / version / author
#   * CMakeLists.txt   -> enables ENABLE_QT + ENABLE_FRONTEND_API and lists the
#                         plugin's C++ sources instead of template plugin-main.c
#
# After running this you have a fully buildable, template-based plugin. The
# template's buildspec then auto-downloads OBS + Qt6 + deps at configure time;
# you do NOT need to install Qt6 or build OBS yourself.
#
# Usage (from the repo root):
#   pwsh ./scripts/Integrate-Template.ps1
#
# Requires: git on PATH.

[CmdletBinding()]
param(
    [string]$TemplateRepo = "https://github.com/obsproject/obs-plugintemplate.git",
    [string]$PluginName   = "obs-auto-resize-output",
    [string]$DisplayName  = "Scene Output Control",
    [string]$Version      = "1.1.1",
    [string]$Author       = "snowyukitty"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Write-Host "Repo root: $repoRoot"

# --- 1. Clone the template into a temp dir --------------------------------
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("obs-plugintemplate-" + [guid]::NewGuid().ToString("N").Substring(0,8))
Write-Host "Cloning template into $tmp ..."
git clone --depth 1 $TemplateRepo $tmp | Out-Null

try {
    # --- 2. Copy build infrastructure into our repo (NOT src/ or data/) ---
    $infra = @(
        "cmake", ".github", "build-aux", "CMakePresets.json", "buildspec.json",
        ".clang-format", ".clang-tidy", ".cmake-format.yaml", ".gitignore",
        ".editorconfig", "CMakeLists.txt"
    )
    foreach ($item in $infra) {
        $srcPath = Join-Path $tmp $item
        if (-not (Test-Path $srcPath)) {
            Write-Host "  (skip, not in template) $item"
            continue
        }
        $dstPath = Join-Path $repoRoot $item

        # Preserve our standalone CMakeLists.txt before overwriting it.
        if ($item -eq "CMakeLists.txt" -and (Test-Path $dstPath) -and
            -not (Test-Path (Join-Path $repoRoot "CMakeLists.standalone.cmake"))) {
            Copy-Item $dstPath (Join-Path $repoRoot "CMakeLists.standalone.cmake")
        }
        # Merge .gitignore instead of clobbering ours: keep ours, append theirs.
        if ($item -eq ".gitignore" -and (Test-Path $dstPath)) {
            Write-Host "  (keep existing .gitignore)"
            continue
        }

        if (Test-Path $srcPath -PathType Container) {
            Copy-Item $srcPath $dstPath -Recurse -Force
        } else {
            Copy-Item $srcPath $dstPath -Force
        }
        Write-Host "  copied $item"
    }

    # --- 3. Patch buildspec.json metadata ---------------------------------
    $bsPath = Join-Path $repoRoot "buildspec.json"
    if (Test-Path $bsPath) {
        $bs = Get-Content $bsPath -Raw | ConvertFrom-Json
        $bs.name        = $PluginName
        $bs.displayName = $DisplayName
        $bs.version     = $Version
        if ($bs.PSObject.Properties.Name -contains "author") { $bs.author = $Author }
        ($bs | ConvertTo-Json -Depth 100) | Set-Content $bsPath -Encoding UTF8
        Write-Host "Patched buildspec.json (name=$PluginName version=$Version)"
    }

    # --- 3b. Enable Qt + frontend API in CMakePresets.json ----------------
    # The base preset's cacheVariables set these to false, which overrides the
    # option() defaults in CMakeLists.txt. Flip them to true.
    $cpPath = Join-Path $repoRoot "CMakePresets.json"
    if (Test-Path $cpPath) {
        $cp = Get-Content $cpPath -Raw
        $cp = $cp -replace '"ENABLE_FRONTEND_API"\s*:\s*false', '"ENABLE_FRONTEND_API": true'
        $cp = $cp -replace '"ENABLE_QT"\s*:\s*false', '"ENABLE_QT": true'
        Set-Content $cpPath $cp -Encoding UTF8
        Write-Host "Patched CMakePresets.json (ENABLE_QT/ENABLE_FRONTEND_API = true)"
    }

    # --- 4. Patch the template CMakeLists.txt -----------------------------
    $cmPath = Join-Path $repoRoot "CMakeLists.txt"
    $cm = Get-Content $cmPath -Raw

    # Enable Qt + frontend API (flip the default OFF -> ON on the option lines).
    $cm = [regex]::Replace($cm,
        'option\(\s*ENABLE_QT[^\)]*\bOFF\s*\)',
        'option(ENABLE_QT "Use Qt functionality" ON)')
    $cm = [regex]::Replace($cm,
        'option\(\s*ENABLE_FRONTEND_API[^\)]*\bOFF\s*\)',
        'option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)')

    # Replace the single-source line with our C++ sources. Use a MatchEvaluator
    # so the replacement text is inserted literally (no $-group substitution).
    $ourSources = @"
target_sources(
  `${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/ScenePreset.cpp
    src/PresetValidation.cpp
    src/AudioSessionState.cpp
    src/ApplyPreset.cpp
    src/PresetDock.cpp)
"@
    $srcPattern = 'target_sources\(\s*\$\{CMAKE_PROJECT_NAME\}\s+PRIVATE\s+src/plugin-main\.c\s*\)'
    $srcEval = { param($m) $ourSources }.GetNewClosure()
    $cm = [regex]::Replace($cm, $srcPattern, [System.Text.RegularExpressions.MatchEvaluator]$srcEval)

    # The template CMake does not define PLUGIN_VERSION (used by our logging);
    # inject it from the buildspec version variable if not already present.
    # Use literal String.Replace so ${...} is not treated as a regex group ref.
    if ($cm -notmatch 'PLUGIN_VERSION') {
        $defLine = 'target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PLUGIN_VERSION="${_version}")'
        $cm = $cm.Replace(
            'set_target_properties_plugin',
            ($defLine + [Environment]::NewLine + [Environment]::NewLine + 'set_target_properties_plugin'))
    }

    Set-Content $cmPath $cm -Encoding UTF8

    $qtOn = $cm -match 'ENABLE_QT[^\n]*ON'
    $feOn = $cm -match 'ENABLE_FRONTEND_API[^\n]*ON'
    $srcOk = $cm -match 'PresetDock\.cpp'
    Write-Host "Patched CMakeLists.txt: ENABLE_QT=$qtOn ENABLE_FRONTEND_API=$feOn sources=$srcOk"
    if (-not ($qtOn -and $feOn -and $srcOk)) {
        Write-Warning "One or more CMakeLists patches did not match. Open CMakeLists.txt and verify:"
        Write-Warning "  * option(ENABLE_QT ...) and option(ENABLE_FRONTEND_API ...) are set to ON"
        Write-Warning "  * target_sources(...) lists the current src/*.cpp files"
    }

    # --- 5. Restore exec bits / symlinks lost during the Windows copy -----
    # build-aux/run-* are symlinks to .run-format.zsh, and several CI shell
    # scripts must be executable. Copy-Item drops both, which breaks the
    # Linux/macOS builds and the format checks (exit 126). Fix them in the git
    # index (safe: core.fileMode/symlinks are false on Windows, so a later
    # `git add` will not revert these). Requires an initialized git repo.
    $inRepo = $false
    try { $inRepo = (git rev-parse --is-inside-work-tree 2>$null) -eq "true" } catch {}
    if ($inRepo) {
        git add build-aux .github/scripts 2>$null | Out-Null
        $execFiles = @(
            "build-aux/.run-format.zsh",
            ".github/scripts/build-macos", ".github/scripts/build-ubuntu",
            ".github/scripts/package-macos", ".github/scripts/package-ubuntu"
        )
        foreach ($f in $execFiles) {
            if (Test-Path (Join-Path $repoRoot $f)) { git update-index --chmod=+x $f 2>$null }
        }
        foreach ($f in @("build-aux/run-clang-format", "build-aux/run-gersemi", "build-aux/run-swift-format")) {
            $blob = (git rev-parse ":$f" 2>$null)
            if ($blob) { git update-index --cacheinfo "120000,$blob,$f" 2>$null }
        }
        Write-Host "Restored exec bits / symlinks for CI scripts in the git index"
    } else {
        Write-Warning "Not a git repo yet. Run 'git init' FIRST, then re-run this script (or apply the CI-script mode fixes manually) so Linux/macOS CI and format checks pass."
    }

    Write-Host ""
    Write-Host "Integration complete. Next:" -ForegroundColor Green
    Write-Host "  cmake --preset windows-x64"
    Write-Host "  cmake --build --preset windows-x64 --config RelWithDebInfo"
    Write-Host "Or push to GitHub and let Actions build the .dll artifact."
}
finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
