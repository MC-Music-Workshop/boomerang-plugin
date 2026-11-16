${NOTES}

## Installation

### Homebrew (macOS)

```bash
brew install --cask mc-music-workshop/tap/boomerang
```

Installs the VST3, AU and standalone app. `boomerang@beta` also gets betas and release candidates; `boomerang@alpha` gets every release, alphas included.

### macOS

Download and run `Boomerang-${VERSION}.pkg`. The installer and plugins are signed and notarized by Apple.

- VST3 → `/Library/Audio/Plug-Ins/VST3/`
- AU → `/Library/Audio/Plug-Ins/Components/`
- Standalone → `/Applications/`

`Boomerang-${VERSION}-macOS.zip` has the same bundles if you'd rather copy them yourself.

### Windows

Download `Boomerang-${VERSION}-Windows.zip`. Copy `VST3\Boomerang+.vst3` into `C:\Program Files\Common Files\VST3\`, or run the standalone app, `Standalone\Boomerang+.exe`. The Windows build isn't code-signed yet, so Windows may warn about it (#55).

### Linux

Download `Boomerang-${VERSION}-Linux.tar.gz`. Copy `VST3/Boomerang+.vst3` into `~/.vst3/`, or run the standalone app, `Standalone/Boomerang+`. A GPG signature (`.asc`) is included to verify the download.

## How to Help

Found a bug? Please [file an issue](https://github.com/MC-Music-Workshop/boomerang-plugin/issues/new) with:

- Steps to reproduce
- Expected vs actual behavior
- Loop length, direction, mode settings
- Host/DAW and buffer size
