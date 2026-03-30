# Boomerang+ Audio Looper Plugin - Copilot Instructions

## Overview

**Always read AGENTS.md first when starting a session** - you are expected to do this.

Boomerang+ is a live looping and time-based audio effect built for instant capture, stutter, overdub, and reverse playback. It's a JUCE-based audio plugin that emulates the legendary Boomerang+ Phrase Sampler hardware looper, designed for performance-oriented use on drums, guitars, synths, and full mixes.

**Current Status:** v2.0.0-beta-4 - Beta quality, universal binary for Intel and Apple Silicon, all major bugs addressed.

## Quick Reference Documentation

Read these files in this order when you need context:
1. **AGENTS.md** - Repository guidelines, project structure, coding style (READ THIS FIRST)
2. **README.md** - Product overview, feature descriptions, behavioral requirements
3. **JUCE_TIPS_AND_TRICKS.md** - JUCE-specific best practices, debugging, common pitfalls
4. **CODE_REVIEW_FINDINGS.md** - Known issues, resolved issues, architecture notes
5. **.session-log.md** (if exists) - Recent session progress and open items

## Tech Stack

- **Language:** C++17
- **Framework:** JUCE 8.x (git submodule)
- **Build System:** CMake 3.21+
- **Formats:** VST3, AU (macOS), Standalone
- **Platforms:** macOS (universal), Windows, Linux
- **CI/CD:** GitHub Actions
- **Testing:** pluginval (VST3 compliance)

## Project Structure

```
Source/           → Core plugin code (PluginProcessor, PluginEditor, LooperEngine)
JUCE/             → JUCE framework (git submodule)
build/            → Build artifacts (git-ignored)
cmake/            → CMake modules and utilities
installer-resources/ → macOS installer templates
.github/workflows/ → CI/CD build and release workflows
```

## Coding Guidelines

### General Rules

- **Never claim code compiles without verification** - Always build and test
- Prioritize code correctness and functionality over style
- Prefer explicit, readable code over clever solutions
- Follow DRY (Don't Repeat Yourself) and YAGNI (You Aren't Gonna Need It)
- Re-use existing JUCE functionality - don't reinvent the wheel
- Avoid adding new dependencies unless absolutely necessary

### C++ Style

- **Indentation:** 2 spaces (no tabs)
- **Braces:** Same line for functions/methods
- **Naming:**
  - `snake_case` for local variables and parameters
  - `CamelCase` for types and classes
  - `PascalCase` for JUCE components
- **Comments:** Brief comments when intent isn't obvious
- **Alignment:** Prefer aligned operators for readability:
  ```cpp
  recordButton.onClick  = [this]() { ... };
  playButton.onClick    = [this]() { ... };
  onceButton.onClick    = [this]() { ... };
  ```

### JUCE-Specific Rules

- **Thread Safety:** Use `std::atomic<>` for state shared between audio and UI threads
- **No allocation in audio thread:** Avoid `DBG()`, `new`, `malloc`, or string operations in `processBlock()`
- **Parameter callbacks:** Treat `parameterChanged()` as if it's on the audio thread
- **LookAndFeels:** Declare before components that use them (destruction order matters)
- **Testing:** Always validate with pluginval before releases

## Build, Test, and Development

### Building

```bash
# Full clean build (recommended after CMake changes)
./build.sh

# Incremental build (faster for code-only changes)
cd build && make -j8

# Build macOS installer package
./build-installer.sh
```

**Note:** Plugins auto-install to system directories when rebuilt (COPY_PLUGIN_AFTER_BUILD is enabled).

### Testing

```bash
# Run VST3 compliance tests
pluginval --validate build/Boomerang_artefacts/VST3/Boomerang.vst3

# Manual testing: use plugin host or standalone
open build/Boomerang_artefacts/Standalone/Boomerang+.app
```

**Testing Guidelines:**
- No automated unit test suite - validate via plugin host or Standalone build
- When reproducing issues, note: loop length, direction, Once/Stack states, host buffer size
- Use GigPerformer or AudioPluginHost for quick iteration

### Version Management

Version is controlled in two places (must match):
- `CMakeLists.txt` line 3: `project(Boomerang VERSION x.x.x.x)`
- `build-installer.sh` line 7: `VERSION="x.x.x-beta-N"`

## Workflow and CI/CD

### Git Workflow

- **Branches:** Feature branches from `main`
- **Commits:** Descriptive, scoped messages (e.g., "Fix reverse playhead wrap")
- **Reference issues:** Use `#XX` in commits when applicable

### GitHub Actions CI/CD

Workflow file: `.github/workflows/build.yml`

1. **Build job** (on push/PR): Parallel builds for macOS/Windows/Linux, uploads artifacts
2. **Validate job** (after build): Runs pluginval on all platforms
3. **Release job** (on `v*` tags): Creates draft GitHub Release with binaries

**To create a release:**
1. Update version in `CMakeLists.txt` and `build-installer.sh`
2. Commit and push to main
3. Tag: `git tag v2.0.0-beta-X && git push --tags`
4. CI builds, validates, creates draft release
5. Edit draft release notes, then publish

### GitHub Actions Best Practices

- Always use latest major version tags (e.g., `actions/checkout@v6`)
- Check action repos for current versions before adding new actions

## Known Issues and Pitfalls

### Resolved Issues ✅

- Issue #38: Thread safety violations (fixed with atomics)
- Issue #32: Reverse playback position wrapping (fixed)
- Issue #33: Once mode reliability (fixed with request flags)
- Issue #34-35: Compiler warnings (all cleaned up)
- Issue #44: Volume control bug (fixed in beta-2)

### Current Known Issues ⚠️

- Issue #36: macOS code signing works in CI, local builds unsigned
- Issue #55: Windows Authenticode signing (low priority - expected SmartScreen warnings)
- Issue #43: REC LED when stacking (enhancement request)

### Agent Best Practices

- **Always use absolute paths** in terminal commands
  - Good: `/home/runner/work/boomerang-plugin/boomerang-plugin/CMakeLists.txt`
  - Bad: `CMakeLists.txt` (you might be in `build/` directory)
- When running `git add`, cd to repo root first or use full paths
- Check file existence before running commands that depend on them
- Don't chain dependent commands without checking exit status

### macOS-Specific Commands

```bash
# Restart GigPerformer
osascript -e 'quit app "GigPerformer5"' && sleep 2 && open -a "GigPerformer5"

# Restart Standalone app
osascript -e 'quit app "Boomerang+"' && sleep 2 && open -a /Applications/Boomerang+.app
```

## Template Substitution Philosophy

**Principle: Always do the simple thing until you can't.**

For installer templates, use `sed` for simple substitution:
```bash
sed -e "s/{{VERSION}}/$VERSION/g" template.xml > output.xml
```

Upgrade to `envsubst` if:
- Many variables (5+)
- Values contain regex metacharacters (`/`, `&`, etc.)
- Template becomes complex

## Tech Debt

- **Reusable signing action:** macOS code signing logic duplicated between this repo and `midi-captain-max`. Should extract into reusable composite action. Priority: medium.

## Release Notes Style

Concise Markdown with structure:
- Top-level title: `vX.X.X-beta-N (JUCE port)`
- Sections: What's New, Installation (macOS and Windows/Linux), Known Issues, How to Help
- Tone: Direct and instructional
- Link to new issue form in "How to Help"

See `.github/skills/create-release/SKILL.md` for detailed release process.

## Security and Code Quality

- **Always run codeql_checker** before finalizing changes
- Fix vulnerabilities related to your changes
- Use ThreadSanitizer (TSan) for thread safety validation
- No secrets in source code
- Statically link Windows runtime

## Additional Resources

- [JUCE Forum](https://forum.juce.com/) - Community examples and troubleshooting
- [JUCE Tutorials](https://juce.com/learn/tutorials) - Framework concepts
- [pluginval Guide](https://github.com/Tracktion/pluginval) - Plugin validation
- Repository issues: https://github.com/mcascone/boomerang-plugin/issues


