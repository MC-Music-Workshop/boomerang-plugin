# Repository Guidelines

You are an expert software engineer with guru-like knowledge of C++ audio plugin development using JUCE.

This document outlines the structure, build process, coding style, testing, and contribution guidelines for the Boomerang audio looper plugin repository.

Read the README.md first for an overview of the project, including detailed behavioral requirements, then refer to this document for development-specific information.

**Also read `.session-log.md` for recent session progress and open items.**

Also review the CODE_REVIEW_FINDINGS.md for a summary and details on known issues in the codebase. The JUCE implementation is now on the main branch.

Refer to JUCE_TIPS_AND_TRICKS.md for JUCE-specific best practices, debugging techniques, and common pitfalls.

## Project Structure & Module Organization

- `Source/`: core C++ code (PluginProcessor, PluginEditor, LooperEngine).
- `build/`: generated build artifacts and Makefile.
- `JUCE/`: JUCE framework (git submodule).
- `archive/plug-n-script/`: archived legacy Plug n Script implementation.
- `Presets/`, `midi/`, `img/`: assets and examples.

## Build, Test, and Development Commands

- Full build: `./build.sh` (cleans, configures, and builds; installs via COPY_PLUGIN_AFTER_BUILD).
- Incremental build from build dir: `cd build && make -j8` (builds and auto-installs changed targets).
- After committing: `cd build && make -j8` (git hash updates automatically at build time; only PluginEditor.cpp recompiles).
- Note: Plugins auto-install to system directories when rebuilt via COPY_PLUGIN_AFTER_BUILD.
- Build installer: `./build-installer.sh` (creates macOS .pkg installer with VST3, AU, and Standalone).
- Tests: `pluginval --validate build/Boomerang_artefacts/VST3/Boomerang.vst3` (run VST3 compliance tests).
- Version is set in `CMakeLists.txt` via `VERSION` and `BOOMERANG_VERSION_SUFFIX` variables.

## Coding Style & Naming Conventions

- C++17 JUCE code; prefer straightforward JUCE patterns.
- Indent with 2 spaces; brace on same line for functions/methods.
- Use `snake_case` for locals/parameters, `CamelCase` for types, `PascalCase` for JUCE components.
- Avoid shadowing class members; keep signed/unsigned consistent for buffer indices.
- Add brief comments when intent isn’t obvious.
- Prefer simpler code over clever constructs. Easy-to-maintain code is better than complex optimizations.
- Re-use existing patterns in the codebase for ease of understanding and consistency. Re-use existing JUCE functionality where possible. Don't reinvent the wheel. Do not introduce new dependencies unless absolutely necessary.
- Follow DRY and YAGNI principles.

## Agent Best Practices

- **Always use absolute paths** in terminal commands (e.g., `/Users/maximiliancascone/github/boomerang-plugin/CMakeLists.txt` not `CMakeLists.txt`). Terminal sessions may be in unexpected directories like `build/`.
- When running `git add`, use full paths or `cd` to repo root first: `cd /Users/maximiliancascone/github/boomerang-plugin && git add file.txt`
- Check paths and filenames for typos and existence before running commands that depend on them.
- Don't chain commands that depend on each other without checking exit status of the first command.
- When asked to "quit and restart GP" or similar, run: `osascript -e 'quit app "GigPerformer5"' && sleep 2 && open -a "GigPerformer5"`
- When asked to "restart standalone" or similar, run: `osascript -e 'quit app "Boomerang+"' && sleep 2 && open -a $BOOM_STANDALONE_PATH` (the env var is exported in my `.alias` file).

## GitHub Actions Best Practices

- Always use the latest major version tag for GitHub Actions (e.g., `actions/checkout@v6`, not `@v4`) unless pinned for a specific reason.
- Check action repos for current versions before adding new actions to workflows.

## CI/CD & Release Process

The repository uses GitHub Actions for CI/CD (`.github/workflows/build.yml`):

1. **Build job** (runs on push/PR): Builds for macOS (universal), Windows, and Linux in parallel. Uploads packaged artifacts (zip/tarball).
2. **Validate job** (runs after build): Downloads build artifacts, extracts VST3, runs `pluginval` for compliance testing. Does NOT rebuild.
3. **Release job** (runs on `v*` tags only): Downloads all artifacts, creates a draft GitHub Release with attached binaries.

To create a release:
1. Update version in `CMakeLists.txt` (`VERSION` and `BOOMERANG_VERSION_SUFFIX`).
2. Commit and push to main.
3. Tag the commit: `git tag v2.0.0-beta-2 && git push --tags`
4. CI builds, validates, and creates a draft release at https://github.com/mcascone/boomerang-plugin/releases
5. Edit the draft release notes, then publish.

Local testing with `act` (requires Docker):
- `act -l` — list available jobs
- `act -n push` — dry run the workflow
- `act -n push --matrix os:ubuntu-latest` — dry run specific matrix entry

## Testing Guidelines

- No automated test suite; validate via plugin host or JUCE Standalone build (`Boomerang_artefacts/Standalone`).
- When reproducing issues, note loop length, direction, Once/Stack states, and host buffer size.

## Commit & Pull Request Guidelines

- Keep commits scoped and descriptive (e.g., `Fix reverse playhead wrap`).
- Reference GitHub issues when applicable.
- PRs: include a short summary, reproduction steps, expected/actual behavior, and screenshots/GIFs if UI-visible.
- Call out audio behavior changes and any new warnings/errors.

## Known Pitfalls (Agent Notes)

- Issue #36 (VST3 signing warnings): macOS code signing + notarization now works in CI. Secrets are org-level (MC-Music-Workshop). Local builds are unsigned.
- Issue #55 (Windows Authenticode signing): Tracked but low priority. Windows SmartScreen warnings expected for unsigned builds.
- **Linux GPG signing**: ✅ **COMPLETE** - Implemented and operational in CI as of 2026-02-16. Linux releases are signed with GPG detached signatures (`.asc` files). Key ID: `241C529DF2E79E51`. Requires `GPG_PRIVATE_KEY` and `GPG_PASSPHRASE` secrets. See `docs/GPG_SIGNING.md` for details and `GPG_SIGNING_SUCCESS.md` for verification results.
- Issue #44 (volume controls entire signal) is a known bug being investigated.
- Issue #43 (REC LED when stacking) is an enhancement request.
- Current release: v2.0.0-beta-1 (JUCE port). Beta quality - core functionality stable, polishing phase. Builds shipped: VST3, AU (macOS), Standalone app (macOS); Windows/Linux use VST3. Installation paths: `~/Library/Audio/Plug-Ins/Components`, `~/Library/Audio/Plug-Ins/VST3`. Please file new issues for any problems.
- releases/ folder is git-ignored; use GitHub Releases for distribution instead.
- Release notes style preference: concise Markdown with top-level title like `v2.0.0-beta-1 (JUCE port)`, sections for What's new, Installation (macOS and Windows/Linux), Known issues, and How to help (link to new issue form). Tone should be direct and instructional.

## Tech Debt

- **Reusable signing action**: The macOS code signing + notarization logic is duplicated between `boomerang-plugin` and `midi-captain-max`. Extract into a reusable composite action or shared workflow. See `midi-captain-max/.github/workflows/ci.yml` for the canonical pattern. Priority: medium (pay this down before adding a third repo).

## Template Substitution

**Principle: Always do the simple thing until you can't.**

For installer templates (`installer-resources/*.template`), we use `sed` for variable substitution:

```bash
sed -e "s/{{VERSION}}/$VERSION/g" -e "s/{{IDENTIFIER}}/$IDENTIFIER/g" template.xml > output.xml
```

| Tool | When to use |
|------|-------------|
| `sed` | Simple cases, 2-3 variables, values have no special chars |
| `envsubst` | Complex templates, many variables, or values with regex metacharacters. Requires `brew install gettext` (macOS) or `apt-get install gettext-base` (CI). Uses `${VAR}` syntax. |
| `perl -pe` | When `sed` escaping becomes painful |

Upgrade to `envsubst` if templates grow complex or substitution values contain `/`, `&`, or other sed metacharacters.

## Aligned Operators and Symbols

I prefer code to be aligned like this:

```cpp
thruMuteButton.onClick = [this]() { audioProcessor.getLooperEngine()->onThruMuteButtonPressed(); };
recordButton.onClick   = [this]() { audioProcessor.getLooperEngine()->onRecordButtonPressed(); };
playButton.onClick     = [this]() { audioProcessor.getLooperEngine()->onPlayButtonPressed(); };
onceButton.onClick     = [this]() { audioProcessor.getLooperEngine()->onOnceButtonPressed(); };
reverseButton.onClick  = [this]() { audioProcessor.getLooperEngine()->onReverseButtonPressed(); };
```

Notice how the `=` signs are vertically aligned. This makes it easier to scan and compare similar lines of code.

Incorrect:

```cpp
thruMuteButton.onClick = [this]() { audioProcessor.getLooperEngine()->onThruMuteButtonPressed(); };
recordButton.onClick = [this]() { audioProcessor.getLooperEngine()->onRecordButtonPressed(); };
playButton.onClick = [this]() { audioProcessor.getLooperEngine()->onPlayButtonPressed(); };
onceButton.onClick = [this]() { audioProcessor.getLooperEngine()->onOnceButtonPressed(); };
reverseButton.onClick = [this]() { audioProcessor.getLooperEngine()->onReverseButtonPressed(); };
```
