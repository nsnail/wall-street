param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("patch", "minor", "major")]
    [string]$Level,

    [switch]$NoPush
)

$ErrorActionPreference = "Stop"

function Get-LatestVersionTag {
    $tags = git tag --list "v*" --sort=-v:refname
    foreach ($tag in $tags) {
        if ($tag -match '^v(\d+)\.(\d+)\.(\d+)$') {
            return [pscustomobject]@{
                Tag = $tag
                Major = [int]$Matches[1]
                Minor = [int]$Matches[2]
                Patch = [int]$Matches[3]
            }
        }
    }

    return $null
}

function Get-NextTag($latest, [string]$level) {
    if ($null -eq $latest) {
        switch ($level) {
            "patch" { return "v0.0.1" }
            "minor" { return "v0.1.0" }
            "major" { return "v1.0.0" }
        }
    }

    $major = $latest.Major
    $minor = $latest.Minor
    $patch = $latest.Patch

    switch ($level) {
        "patch" { $patch += 1 }
        "minor" { $minor += 1; $patch = 0 }
        "major" { $major += 1; $minor = 0; $patch = 0 }
    }

    return "v$major.$minor.$patch"
}

$repoRoot = git rev-parse --show-toplevel
Set-Location $repoRoot

$branch = git branch --show-current
if ($branch -ne "main") {
    throw "Release must run from main. Current branch: $branch"
}

$status = git status --short
if ($status) {
    Write-Host "Working tree has changes:"
    $status | ForEach-Object { Write-Host $_ }
    throw "Commit or stash changes before release."
}

if (Get-Command mingw32-make -ErrorAction SilentlyContinue) {
    mingw32-make
} elseif (Get-Command make -ErrorAction SilentlyContinue) {
    make
} else {
    throw "Neither mingw32-make nor make was found. Install MinGW/MSYS2 and add it to PATH."
}

if (-not (Test-Path "build\WallStreetTicker.exe")) {
    throw "Build output missing: build\WallStreetTicker.exe"
}

if (-not (Test-Path "build\appsettings.json")) {
    throw "Build output missing: build\appsettings.json"
}

$latest = Get-LatestVersionTag
$nextTag = Get-NextTag $latest $Level

git rev-parse --verify --quiet "refs/tags/$nextTag" *> $null
if ($LASTEXITCODE -eq 0) {
    throw "Tag already exists: $nextTag"
}

git tag -a $nextTag -m "Release $nextTag"
Write-Host "Created tag $nextTag"

if (-not $NoPush) {
    git push -u origin main
    git push origin $nextTag
    Write-Host "Pushed main and $nextTag. GitHub Actions will create the release."
} else {
    Write-Host "NoPush set. Push manually with:"
    Write-Host "git push -u origin main"
    Write-Host "git push origin $nextTag"
}
