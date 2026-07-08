---
name: wall-street-release
description: Release workflow for this WallStreetTicker repository. Use when the user asks to 发版, release, publish, tag a version, or push a GitHub release for this project; the skill asks for release level and computes the next semantic tag.
---

# WallStreetTicker Release

Use this skill only from the WallStreetTicker repository root.

## Required User Choice

Before tagging, ask the user for the release level unless they already specified it:

- `patch`: bug fix or small change, bump `vX.Y.Z` to `vX.Y.(Z+1)`; this corresponds to `+0.01`.
- `minor`: user-visible feature or behavior improvement, bump `vX.Y.Z` to `vX.(Y+1).0`; this corresponds to `+0.1`.
- `major`: breaking change or large release, bump `vX.Y.Z` to `v(X+1).0.0`; this corresponds to `+1`.

If there is no existing tag, start from `v0.1.0` for `minor`, `v0.0.1` for `patch`, or `v1.0.0` for `major`.

## Workflow

1. Check the working tree with `git status --short --branch`.
2. If there are uncommitted changes, summarize them and commit before tagging if the user wants the current state released.
3. Run `dotnet build -c Release`.
4. Run `dotnet publish -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true /p:EnableCompressionInSingleFile=true`.
5. Determine the latest tag with `git tag --list "v*" --sort=-v:refname`.
6. Compute the next tag from the requested level.
7. Create an annotated tag: `git tag -a <tag> -m "Release <tag>"`.
8. Push main and the tag:

```powershell
git push -u origin main
git push origin <tag>
```

The GitHub Action `.github/workflows/release.yml` builds the Windows x64 package and creates the GitHub Release when the tag is pushed.

## Script

Prefer using `scripts/release.ps1` for deterministic version calculation and local validation:

```powershell
.codex\skills\wall-street-release\scripts\release.ps1 -Level patch
.codex\skills\wall-street-release\scripts\release.ps1 -Level minor
.codex\skills\wall-street-release\scripts\release.ps1 -Level major
```

Add `-NoPush` to create the commit/tag locally without pushing.
