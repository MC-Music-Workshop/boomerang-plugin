# Release Skill for Boomerang+

Create and manage releases for the Boomerang+ audio looper plugin.

## When to Use

This skill applies when the user asks to:
- Create a release
- Write release notes
- Prepare for a new version
- Tag a release
- Bump the version

## Information Gathering

1. **Get current version** from `CMakeLists.txt` line ~3: `project(Boomerang VERSION x.x.x.x)`
2. **Get installer version** from `build-installer.sh` line ~7: `VERSION="x.x.x-alpha-N"`
3. **Get commits since last tag**:
   ```bash
   git log $(git describe --tags --abbrev=0)..HEAD --oneline
   ```
4. **Check open issues** for Known Issues section: `gh issue list --state open`
5. **Review previous release notes** in `releases/` for style consistency

## Release Notes Format

Follow this exact structure (reference: `releases/v2.0.0-alpha-3/release-notes.md`):

```markdown
# vX.X.X-alpha-N (JUCE port)

**⚠️ Alpha Release** - This is an early alpha release with known issues. Please test thoroughly and report any problems.

## What's New

### [Category]
- **Bold feature name**: Description of change

## Installation

### macOS
Download and run the `.pkg` installer, or manually install:
- VST3 → `/Library/Audio/Plug-Ins/VST3/`
- AU → `/Library/Audio/Plug-Ins/Components/`
- Standalone → `/Applications/`

### Windows/Linux
VST3 builds for other platforms coming soon.

## Known Issues

- Issue description (#XX)

## How to Help

Found a bug? Please [file an issue](https://github.com/mcascone/boomerang-plugin/issues/new) with:
- Steps to reproduce
- Expected vs actual behavior
- Loop length, direction, mode settings
- Host/DAW and buffer size

---

**Full Changelog**: https://github.com/mcascone/boomerang-plugin/compare/vPREVIOUS...vNEW
```

## File Locations

| File | Purpose |
|------|---------|
| `releases/vX.X.X-alpha-N/release-notes.md` | Release notes |
| `CMakeLists.txt` line 3 | Project version |
| `build-installer.sh` line 7 | Installer version |

## Commit Categorization

Group commits into these categories:
- **Bug Fixes & Stability** - Crash fixes, race conditions, buffer issues
- **UI Improvements** - Layout, buttons, visual changes
- **Build Improvements** - CMake, installer, signing, CI
- **Architecture** - Refactoring, code cleanup, patterns

## Pre-Completion Checklist

- [ ] Tests pass: CI's Build run for the release commit is green (it runs `BoomerangTests` on every platform), and so does a local `./build.sh`
- [ ] Version numbers match in `CMakeLists.txt` and `build-installer.sh`
- [ ] Release notes saved to `releases/vX.X.X-alpha-N/release-notes.md`
- [ ] Known issues reference current open GitHub issues
- [ ] Changelog link uses correct tag names

## Building the Installer

After release notes are ready and version numbers updated:

1. **Build the plugin** (if not already built):
   ```bash
   ./build.sh
   ```

2. **Build the installer package**:
   ```bash
   ./build-installer.sh
   ```
   This creates `build/installer/Boomerang-{VERSION}.pkg`

3. **Verify the output**:
   ```bash
   ls -la build/installer/Boomerang-*.pkg
   ```

## Creating GitHub Release & Uploading Assets

After user confirms release notes and build:

1. **Create and push the git tag**:
   ```bash
   git tag -a vX.X.X-alpha-N -m "Release vX.X.X-alpha-N"
   git push origin vX.X.X-alpha-N
   ```

2. **Create GitHub release with asset**:
   ```bash
   gh release create vX.X.X-alpha-N \
     --title "vX.X.X-alpha-N (JUCE port)" \
     --notes-file releases/vX.X.X-alpha-N/release-notes.md \
     build/installer/Boomerang-X.X.X-alpha-N.pkg
   ```

   Or create release and upload separately:
   ```bash
   gh release create vX.X.X-alpha-N --title "..." --notes-file ...
   gh release upload vX.X.X-alpha-N build/installer/Boomerang-*.pkg
   ```

3. **Optionally create a zip of raw artifacts**:
   ```bash
   cd build/Boomerang_artefacts
   zip -r ../../releases/vX.X.X-alpha-N/Boomerang-vX.X.X-alpha-N-macOS.zip VST3 AU Standalone
   gh release upload vX.X.X-alpha-N releases/vX.X.X-alpha-N/Boomerang-vX.X.X-alpha-N-macOS.zip
   ```

## Requires User Confirmation

**Never automatically:**
- Create git tags
- Push to remote
- Create GitHub releases
- Run build scripts

Always ask the user before these operations.
