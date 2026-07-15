#!/bin/bash
# Turrama Uninstaller
# Removes all installed components of Turrama,
# including any leftovers from when the plugin was named Boomerang+.

set -e

echo "Turrama Uninstaller"
echo "==================="
echo ""
echo "This will remove:"
echo "  - VST3 plugin from /Library/Audio/Plug-Ins/VST3/"
echo "  - AU plugin from /Library/Audio/Plug-Ins/Components/"
echo "  - Standalone app from /Applications/"
echo "  - Any legacy Boomerang+ versions of the above"
echo ""
read -p "Continue with uninstallation? (y/N): " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Uninstallation cancelled."
    exit 0
fi

echo ""
echo "Uninstalling Turrama..."

# remove_bundle <label> <path>
remove_bundle() {
    local label="$1" path="$2"
    if [ -d "$path" ]; then
        echo "Removing $label..."
        rm -rf "$path"
        echo "  ✓ $label removed"
    else
        echo "  - $label not found (already removed or not installed)"
    fi
}

remove_bundle "VST3 plugin"    "/Library/Audio/Plug-Ins/VST3/Turrama.vst3"
remove_bundle "AU plugin"      "/Library/Audio/Plug-Ins/Components/Turrama.component"
remove_bundle "Standalone app" "/Applications/Turrama.app"

# Legacy Boomerang+ era installs (pre-rebrand)
remove_bundle "legacy VST3 plugin"    "/Library/Audio/Plug-Ins/VST3/Boomerang+.vst3"
remove_bundle "legacy AU plugin"      "/Library/Audio/Plug-Ins/Components/Boomerang+.component"
remove_bundle "legacy Standalone app" "/Applications/Boomerang+.app"

# Clean up package receipts (current and legacy identifiers)
echo "Cleaning up installer receipts..."
for id in com.MCMusicWorkshop.Turrama com.MCMusicWorkshop.Boomerang; do
    pkgutil --forget "$id.vst3" 2>/dev/null && echo "  ✓ $id.vst3 receipt removed" || true
    pkgutil --forget "$id.au" 2>/dev/null && echo "  ✓ $id.au receipt removed" || true
    pkgutil --forget "$id.standalone" 2>/dev/null && echo "  ✓ $id.standalone receipt removed" || true
done

echo ""
echo "✓ Turrama has been uninstalled successfully!"
echo ""
echo "Note: User presets and settings are preserved in:"
echo "  ~/Library/Audio/Presets/ (if any)"
echo ""
echo "To remove presets as well, delete them manually."
