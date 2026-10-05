# jtty-sidecar - one-command release.
#
#   deploy.bat            next patch version  (0.1.0 -> 0.1.1)
#   deploy.bat minor      next minor version  (0.1.1 -> 0.2.0)
#   deploy.bat major      next major version  (0.2.0 -> 1.0.0)
#   deploy.bat 1.4.2      exactly that version
#
# What it does, stopping at the first failure:
#   1. bumps the version in CMakeLists.txt
#   2. compile.bat, then run-tests.bat - a release that does not build and pass is never tagged
#   3. Authenticode-signs dist\jtty-sidecar.exe with the publisher's certificate (signtool; the
#      hardware token asks for its password) and writes its SHA-256
#   4. commits everything in the working tree as "Release vX.Y.Z", tags vX.Y.Z, pushes main and the tag
#   5. creates the GitHub Release for the tag and uploads the SIGNED jtty-sidecar.exe and
#      jtty-sidecar.exe.sha256 - the two files a program fetching just the engine downloads
# GitHub Actions then builds the same tag and adds the source-built zip and SHA256SUMS.txt to that
# release (.github/workflows/build.yml). The zip is unsigned; the bare exe is the signed one.
#
# Needs: the MSYS2 toolchain (setup-toolchain.bat), signtool from the Windows SDK with the
# publisher's certificate available to it, and the GitHub CLI logged in (gh auth login).

param([string]$Bump = "patch")

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# Signing: the certificate is picked by subject name, so nothing secret lives in this script.
$SignSubject   = "STATION MASTER GROUP LTD"
$TimestampUrl  = "http://timestamp.digicert.com"

function Fail($msg) { Write-Host ""; Write-Host "DEPLOY STOPPED: $msg" -ForegroundColor Red; exit 1 }

function Find-SignTool {
    $kits = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    if (-not (Test-Path $kits)) { return $null }
    Get-ChildItem $kits -Directory | Where-Object { $_.Name -match '^\d+\.' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "x64\signtool.exe" } |
        Where-Object { Test-Path $_ } | Select-Object -First 1
}

# --- prerequisites ------------------------------------------------------------
if (-not (Test-Path .git)) { Fail "not a git repository" }
$signtool = Find-SignTool
if (-not $signtool) { Fail "signtool.exe not found under the Windows Kits folder; install the Windows SDK" }
$gh = Get-Command gh -ErrorAction SilentlyContinue
if (-not $gh) {
    $ghPath = "$env:ProgramFiles\GitHub CLI\gh.exe"
    if (Test-Path $ghPath) { $gh = Get-Item $ghPath } else { Fail "GitHub CLI not found: winget install GitHub.cli, then gh auth login" }
}
$ghExe = $gh.Source
if ($null -eq $ghExe) { $ghExe = $gh.FullName }
& $ghExe auth status 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0 -and $env:GH_TOKEN) {
    # A GH_TOKEN in the environment overrides the stored login; if it is stale, ignore it for this run.
    Write-Host "GH_TOKEN in the environment was rejected; using the GitHub CLI's stored login instead."
    $env:GH_TOKEN = $null
    & $ghExe auth status 2>&1 | Out-Null
}
if ($LASTEXITCODE -ne 0) { Fail "GitHub CLI is not logged in: run  gh auth login" }

$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne "main") { Fail "on branch '$branch'; releases are cut from main" }
git fetch origin --tags --quiet
$behind = (git rev-list --count "HEAD..origin/main").Trim()
if ($behind -ne "0") { Fail "origin/main has $behind commit(s) you do not have; git pull first" }

# --- version ------------------------------------------------------------------
$cmake = Get-Content CMakeLists.txt -Raw
$m = [regex]::Match($cmake, 'project \(jtty_sidecar VERSION (\d+)\.(\d+)\.(\d+)')
if (-not $m.Success) { Fail "could not find the version in CMakeLists.txt" }
$cur = [int[]]@($m.Groups[1].Value, $m.Groups[2].Value, $m.Groups[3].Value)
switch -Regex ($Bump) {
    '^patch$'            { $new = @($cur[0], $cur[1], $cur[2] + 1) }
    '^minor$'            { $new = @($cur[0], $cur[1] + 1, 0) }
    '^major$'            { $new = @($cur[0] + 1, 0, 0) }
    '^\d+\.\d+\.\d+$'    { $new = [int[]]($Bump -split '\.') }
    default              { Fail "'$Bump' is not patch, minor, major or X.Y.Z" }
}
$version = "$($new[0]).$($new[1]).$($new[2])"
$tag = "v$version"
if (git tag -l $tag) { Fail "tag $tag already exists" }
Write-Host "Releasing $tag (was $($cur -join '.'))" -ForegroundColor Cyan

$cmake = $cmake -replace 'project \(jtty_sidecar VERSION \d+\.\d+\.\d+', "project (jtty_sidecar VERSION $version"
[IO.File]::WriteAllText("$PSScriptRoot\CMakeLists.txt", $cmake)

# --- build and test -----------------------------------------------------------
Write-Host "--- compile.bat" -ForegroundColor Cyan
cmd /c compile.bat
if ($LASTEXITCODE -ne 0) { git checkout -- CMakeLists.txt; Fail "build failed (version bump reverted)" }
Write-Host "--- run-tests.bat" -ForegroundColor Cyan
cmd /c run-tests.bat
if ($LASTEXITCODE -ne 0) { git checkout -- CMakeLists.txt; Fail "tests failed (version bump reverted)" }

# --- sign ---------------------------------------------------------------------
Write-Host "--- signing dist\jtty-sidecar.exe (the token will ask for its password)" -ForegroundColor Cyan
& $signtool sign /n $SignSubject /fd sha256 /td sha256 /tr $TimestampUrl /a "dist\jtty-sidecar.exe"
if ($LASTEXITCODE -ne 0) { git checkout -- CMakeLists.txt; Fail "signing failed (version bump reverted)" }
& $signtool verify /pa /q "dist\jtty-sidecar.exe"
if ($LASTEXITCODE -ne 0) { git checkout -- CMakeLists.txt; Fail "the signature did not verify (version bump reverted)" }
$sha = (Get-FileHash "dist\jtty-sidecar.exe" -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText("$PSScriptRoot\dist\jtty-sidecar.exe.sha256", $sha)   # bare hex, no newline
Write-Host "signed, sha256 $sha"

# --- commit, tag, push --------------------------------------------------------
git add -A
git diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    git -c core.safecrlf=false commit -q -m "Release $tag"
    if ($LASTEXITCODE -ne 0) { Fail "commit failed" }
} else {
    Write-Host "Nothing new to commit; tagging the current commit."   # e.g. deploy X.Y.Z at the version already set
}
git tag -a $tag -m "jtty-sidecar $version"
if ($LASTEXITCODE -ne 0) { Fail "tag failed" }
git push origin main
if ($LASTEXITCODE -ne 0) { Fail "push of main failed (the tag was created locally but not pushed)" }
git push origin $tag
if ($LASTEXITCODE -ne 0) { Fail "push of $tag failed" }

# --- release with the signed engine ------------------------------------------
# Created here, now, so the signed files are the first thing on the release page; the Actions build
# for this tag adds the zip and SHA256SUMS.txt to the same release when it finishes.
Write-Host "--- creating the GitHub release" -ForegroundColor Cyan
$notes = "jtty-sidecar $tag for Windows x64.`n`n" +
         "jtty-sidecar.exe is Authenticode-signed by the publisher; jtty-sidecar.exe.sha256 is its SHA-256. " +
         "The zip (added by the build workflow) holds the same program built from this tag on GitHub Actions, " +
         "unsigned, with README.md, LICENSE and UPSTREAM.md. GPLv3."
& $ghExe release create $tag "dist\jtty-sidecar.exe" "dist\jtty-sidecar.exe.sha256" --title "jtty-sidecar $tag" --notes $notes
if ($LASTEXITCODE -ne 0) { Fail "release creation failed; the tag is pushed - create the release by hand or re-run: gh release create $tag dist\jtty-sidecar.exe dist\jtty-sidecar.exe.sha256" }

$remote = (git remote get-url origin).Trim() -replace '^git@github\.com:', 'https://github.com/' -replace '\.git$', ''
Write-Host ""
Write-Host "Released $tag." -ForegroundColor Green
Write-Host "Build progress (adds the zip): $remote/actions"
Write-Host "Release page:                  $remote/releases/tag/$tag"
Write-Host "Engine download URL:           $remote/releases/latest/download/jtty-sidecar.exe"
