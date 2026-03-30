# Release Notes Template

Use this template when creating release notes for new versions.

---

## v{VERSION} - {Release Title}

**Release Date:** {DATE}

### What's New

{List major features, improvements, and fixes}

- Feature: {Description}
- Fix: {Description}
- Improvement: {Description}

### Installation

#### macOS

**Installer Package (Recommended):**
1. Download `Boomerang-{VERSION}-macOS.pkg`
2. Double-click to install
3. The installer is **code signed and notarized** by Apple - no security warnings!
4. Plugins will be installed to:
   - VST3: `~/Library/Audio/Plug-Ins/VST3/`
   - AU: `~/Library/Audio/Plug-Ins/Components/`
   - Standalone: `/Applications/`

**Manual Installation:**
1. Download `Boomerang-{VERSION}-macOS.zip`
2. Extract and copy plugins to the locations above

#### Windows

1. Download `Boomerang-{VERSION}-Windows.zip`
2. Extract the archive
3. Copy `Boomerang+.vst3` to your VST3 folder:
   - Default: `C:\Program Files\Common Files\VST3\`
4. Copy `Boomerang+.exe` (Standalone) to any location

**Note:** Windows SmartScreen may show a warning (unsigned release). This is expected. We're working on code signing for Windows.

#### Linux

**Download and Verify:**
1. Download `Boomerang-{VERSION}-Linux.tar.gz`
2. Download `Boomerang-{VERSION}-Linux.tar.gz.asc` (GPG signature)

**Verify Authenticity (Recommended):**
```bash
# Import the MC Music Workshop GPG key (first time only)
gpg --keyserver keys.openpgp.org --recv-keys 241C529DF2E79E51

# Verify the signature
gpg --verify Boomerang-{VERSION}-Linux.tar.gz.asc Boomerang-{VERSION}-Linux.tar.gz
```

You should see: `gpg: Good signature from "MC Music Workshop"`

**Install:**
```bash
# Extract
tar -xzf Boomerang-{VERSION}-Linux.tar.gz

# Copy VST3 plugin
cp -r VST3/Boomerang+.vst3 ~/.vst3/

# Copy Standalone (optional)
cp Standalone/Boomerang+ ~/bin/  # or any location in your PATH
```

### Security & Code Signing

- ✅ **macOS**: Fully code signed and notarized by Apple
- ✅ **Linux**: GPG signed for authenticity verification
- ⚠️ **Windows**: Currently unsigned (code signing in progress)

**GPG Key Information:**
- Key ID: `241C529DF2E79E51`
- Fingerprint: `0E9C 74CD 0BE6 5EB3 8296  46FC 241C 529D F2E7 9E51`
- Available on: keys.openpgp.org, keyserver.ubuntu.com

For detailed verification instructions, see: [Installation & Security](README.md#installation--security)

### Known Issues

{List any known issues or limitations}

- Issue: {Description and workaround}

### Changelog

**Full Changelog:** https://github.com/MC-Music-Workshop/boomerang-plugin/compare/v{PREV_VERSION}...v{VERSION}

### How to Help

Found a bug or have a feature request?

- **Report an issue:** https://github.com/MC-Music-Workshop/boomerang-plugin/issues/new
- **Documentation:** Check the [README](README.md) and [AGENTS](AGENTS.md) guides
- **Discord:** Join our community [link if available]

Thank you for using Boomerang! 🎵

---

## Example (Filled In)

## v2.0.0-beta-5 - GPG Signing for Linux

**Release Date:** February 12, 2026

### What's New

- **Linux GPG Signing:** All Linux releases are now GPG signed for security and authenticity verification
- **Security Documentation:** Added comprehensive guides for verifying release signatures
- **Improved Release Process:** Automated signing and notarization for all platforms (macOS complete, Linux complete, Windows in progress)

### Installation

#### macOS

**Installer Package (Recommended):**
1. Download `Boomerang-2.0.0-beta-5-macOS.pkg`
2. Double-click to install
3. The installer is **code signed and notarized** by Apple - no security warnings!
4. Plugins will be installed to:
   - VST3: `~/Library/Audio/Plug-Ins/VST3/`
   - AU: `~/Library/Audio/Plug-Ins/Components/`
   - Standalone: `/Applications/`

**Manual Installation:**
1. Download `Boomerang-2.0.0-beta-5-macOS.zip`
2. Extract and copy plugins to the locations above

#### Windows

1. Download `Boomerang-2.0.0-beta-5-Windows.zip`
2. Extract the archive
3. Copy `Boomerang+.vst3` to your VST3 folder:
   - Default: `C:\Program Files\Common Files\VST3\`
4. Copy `Boomerang+.exe` (Standalone) to any location

**Note:** Windows SmartScreen may show a warning (unsigned release). This is expected. We're working on code signing for Windows.

#### Linux

**Download and Verify:**
1. Download `Boomerang-2.0.0-beta-5-Linux.tar.gz`
2. Download `Boomerang-2.0.0-beta-5-Linux.tar.gz.asc` (GPG signature)

**Verify Authenticity (Recommended):**
```bash
# Import the MC Music Workshop GPG key (first time only)
gpg --keyserver keys.openpgp.org --recv-keys XXXXXXXXXXXXXXXX

# Verify the signature
gpg --verify Boomerang-2.0.0-beta-5-Linux.tar.gz.asc Boomerang-2.0.0-beta-5-Linux.tar.gz
```

You should see: `gpg: Good signature from "MC Music Workshop <contact@example.com>"`

**Install:**
```bash
# Extract
tar -xzf Boomerang-2.0.0-beta-5-Linux.tar.gz

# Copy VST3 plugin
cp -r VST3/Boomerang+.vst3 ~/.vst3/

# Copy Standalone (optional)
cp Standalone/Boomerang+ ~/bin/  # or any location in your PATH
```

### Security & Code Signing

- ✅ **macOS**: Fully code signed and notarized by Apple
- ✅ **Linux**: GPG signed for authenticity verification
- ⚠️ **Windows**: Currently unsigned (code signing in progress)

**GPG Key Information:**
- Key ID: `241C529DF2E79E51`
- Fingerprint: `0E9C 74CD 0BE6 5EB3 8296  46FC 241C 529D F2E7 9E51`
- Available on: keys.openpgp.org, keyserver.ubuntu.com

For detailed verification instructions, see: [Installation & Security](README.md#installation--security)

### Known Issues

- Volume control affects entire signal (Issue #44)
- REC LED behavior when stacking could be improved (Issue #43)
- Windows releases show SmartScreen warning (Issue #55)

### Changelog

**Full Changelog:** https://github.com/MC-Music-Workshop/boomerang-plugin/compare/v2.0.0-beta-4...v2.0.0-beta-5

### How to Help

Found a bug or have a feature request?

- **Report an issue:** https://github.com/MC-Music-Workshop/boomerang-plugin/issues/new
- **Documentation:** Check the [README](README.md) and [AGENTS](AGENTS.md) guides

Thank you for using Boomerang! 🎵
