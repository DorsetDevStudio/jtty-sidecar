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
#   3. commits everything in the working tree as "Release vX.Y.Z"
#   4. tags vX.Y.Z and pushes main and the tag
# GitHub Actions then builds the tagged source and publishes the release zip
# (see .github/workflows/build.yml and the Downloads section of README.md).

param([string]$Bump = "patch")

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Fail($msg) { Write-Host ""; Write-Host "DEPLOY STOPPED: $msg" -ForegroundColor Red; exit 1 }

# --- repository sanity ---------------------------------------------------------
if (-not (Test-Path .git)) { Fail "not a git repository" }
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

# --- commit, tag, push --------------------------------------------------------
git add -A
git -c core.safecrlf=false commit -q -m "Release $tag"
if ($LASTEXITCODE -ne 0) { Fail "commit failed" }
git tag -a $tag -m "jtty-sidecar $version"
if ($LASTEXITCODE -ne 0) { Fail "tag failed" }
git push origin main
if ($LASTEXITCODE -ne 0) { Fail "push of main failed (the tag was created locally but not pushed)" }
git push origin $tag
if ($LASTEXITCODE -ne 0) { Fail "push of $tag failed" }

$remote = (git remote get-url origin).Trim() -replace '^git@github\.com:', 'https://github.com/' -replace '\.git$', ''
Write-Host ""
Write-Host "Released $tag." -ForegroundColor Green
Write-Host "Build and release progress: $remote/actions"
Write-Host "Release page when done:     $remote/releases/tag/$tag"
Write-Host "Latest download URL:        $remote/releases/latest/download/jtty-sidecar-win64.zip"
