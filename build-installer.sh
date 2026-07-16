#!/bin/bash
# Turrama Installer Build Script
# Creates a customizable macOS .pkg installer for all plugin formats
# Supports optional code signing and notarization with Apple Developer certificates

set -e

# Get version from git (single source of truth)
# Check if version was passed via environment variable (from CI)
if [ -n "$BOOMERANG_VERSION" ]; then
    VERSION="$BOOMERANG_VERSION"
    echo "Using version from environment: $VERSION"
else
    # Extract version from git describe
    # At a tag:    v2.0.0-beta-3 -> 2.0.0-beta-3
    # After a tag: v2.0.0-beta-3-5-g1234abc -> 2.0.0-beta-3+5.g1234abc
    GIT_DESCRIBE=$(git describe --tags --always 2>/dev/null || echo "")
    if [ -n "$GIT_DESCRIBE" ]; then
        # Strip 'v' prefix if present
        VERSION="${GIT_DESCRIBE#v}"
        # Convert git format (tag-N-gHASH) to semver metadata (tag+N.gHASH)
        VERSION=$(echo "$VERSION" | sed -E 's/^(.+)-([0-9]+)-(g[0-9a-f]+)$/\1+\2.\3/')
        echo "Using version from git: $VERSION"
    else
        VERSION="dev"
        echo "Warning: No git info found, using fallback: $VERSION"
    fi
fi

# Signing configuration
# Set these environment variables for signed builds:
#   DEVELOPER_ID_APPLICATION - "Developer ID Application: Name (TEAMID)"
#   DEVELOPER_ID_INSTALLER   - "Developer ID Installer: Name (TEAMID)"
#   APPLE_ID                 - Apple ID email for notarization
#   APPLE_APP_PASSWORD       - App-specific password
#   APPLE_TEAM_ID            - Team ID (e.g., 9WNXKEF4SM)

# Check for Developer ID Application certificate
if security find-identity -v -p codesigning | grep -q "Developer ID Application"; then
    DEV_APP_IDENTITY=$(security find-identity -v -p codesigning | grep "Developer ID Application" | head -1 | sed 's/.*"\(.*\)".*/\1/')
    echo "Found Developer ID Application: $DEV_APP_IDENTITY"
    CAN_SIGN_APP=true
else
    echo "⚠ No Developer ID Application certificate found - binaries will not be signed"
    CAN_SIGN_APP=false
fi

# Check for Developer ID Installer certificate
if security find-identity -v | grep -q "Developer ID Installer"; then
    DEV_INSTALLER_IDENTITY=$(security find-identity -v | grep "Developer ID Installer" | head -1 | sed 's/.*"\(.*\)".*/\1/')
    echo "Found Developer ID Installer: $DEV_INSTALLER_IDENTITY"
    CAN_SIGN_PKG=true
else
    echo "⚠ No Developer ID Installer certificate found - package will not be signed"
    CAN_SIGN_PKG=false
fi

# Check for notarization credentials
if [ -n "$APPLE_ID" ] && [ -n "$APPLE_APP_PASSWORD" ] && [ -n "$APPLE_TEAM_ID" ]; then
    echo "Notarization credentials available"
    CAN_NOTARIZE=true
else
    echo "⚠ Notarization credentials not set - package will not be notarized"
    CAN_NOTARIZE=false
fi

IDENTIFIER="com.MCMusicWorkshop.Turrama"
BUILD_DIR="build/Boomerang_artefacts"
OUTPUT_DIR="build/installer"
COMPONENT_PKGS="$OUTPUT_DIR/components"
RESOURCES_DIR="$(dirname "$0")/installer-resources"

echo "Building Turrama Installer v${VERSION}"
echo "============================================"

# Ensure build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build artifacts not found. Run ./build.sh first."
    exit 1
fi

# Detect artifact subdirectory (Release/ for multi-config, empty for single-config)
if [ -d "$BUILD_DIR/Release/VST3" ]; then
    ARTIFACT_SUBDIR="Release/"
    echo "Detected multi-config build (Release subdirectory)"
elif [ -d "$BUILD_DIR/VST3" ]; then
    ARTIFACT_SUBDIR=""
    echo "Detected single-config build"
else
    echo "Error: Could not find VST3 artifacts in $BUILD_DIR"
    exit 1
fi

# Function to sign a bundle with Developer ID Application
sign_bundle() {
    local BUNDLE_PATH="$1"
    local BUNDLE_NAME=$(basename "$BUNDLE_PATH")
    
    if [ "$CAN_SIGN_APP" = true ]; then
        echo "Signing $BUNDLE_NAME with hardened runtime..."
        codesign --force --deep --options runtime --timestamp \
            --sign "$DEV_APP_IDENTITY" \
            "$BUNDLE_PATH"
        
        # Verify signature
        if codesign --verify --deep --strict "$BUNDLE_PATH" 2>/dev/null; then
            echo "  ✓ $BUNDLE_NAME signed and verified"
        else
            echo "  ✗ $BUNDLE_NAME signature verification failed"
            exit 1
        fi
    else
        echo "  ⚠ Skipping signing for $BUNDLE_NAME (no certificate)"
    fi
}

# Sign all bundles before packaging
echo ""
echo "Signing binaries..."
echo "==================="

sign_bundle "$BUILD_DIR/${ARTIFACT_SUBDIR}VST3/Turrama.vst3"
sign_bundle "$BUILD_DIR/${ARTIFACT_SUBDIR}AU/Turrama.component"
sign_bundle "$BUILD_DIR/${ARTIFACT_SUBDIR}Standalone/Turrama.app"

# Clean and create directories
echo "Setting up installer directories..."
rm -rf "$OUTPUT_DIR"
mkdir -p "$COMPONENT_PKGS"

# Build VST3 component package
echo "Building VST3 component..."
VST3_ROOT="$OUTPUT_DIR/vst3-root"
mkdir -p "$VST3_ROOT/Library/Audio/Plug-Ins/VST3"
cp -R "$BUILD_DIR/${ARTIFACT_SUBDIR}VST3/Turrama.vst3" "$VST3_ROOT/Library/Audio/Plug-Ins/VST3/"

pkgbuild --root "$VST3_ROOT" \
         --identifier "${IDENTIFIER}.vst3" \
         --version "$VERSION" \
         --install-location "/" \
         "$COMPONENT_PKGS/VST3.pkg"

# Build AU component package
echo "Building AU component..."
AU_ROOT="$OUTPUT_DIR/au-root"
mkdir -p "$AU_ROOT/Library/Audio/Plug-Ins/Components"
cp -R "$BUILD_DIR/${ARTIFACT_SUBDIR}AU/Turrama.component" "$AU_ROOT/Library/Audio/Plug-Ins/Components/"

pkgbuild --root "$AU_ROOT" \
         --identifier "${IDENTIFIER}.au" \
         --version "$VERSION" \
         --install-location "/" \
         "$COMPONENT_PKGS/AU.pkg"

# Build Standalone component package
echo "Building Standalone component..."
STANDALONE_ROOT="$OUTPUT_DIR/standalone-root"
mkdir -p "$STANDALONE_ROOT/Applications"
cp -R "$BUILD_DIR/${ARTIFACT_SUBDIR}Standalone/Turrama.app" "$STANDALONE_ROOT/Applications/"
cp uninstall.sh "$STANDALONE_ROOT/Applications/Uninstall Turrama.command"
chmod +x "$STANDALONE_ROOT/Applications/Uninstall Turrama.command"

# Create component plist to prevent bundle relocation
cp "$RESOURCES_DIR/component.plist" "$OUTPUT_DIR/component.plist"
COMP_PLIST="$OUTPUT_DIR/component.plist"

pkgbuild --root "$STANDALONE_ROOT" \
         --identifier "${IDENTIFIER}.standalone" \
         --version "$VERSION" \
         --install-location "/" \
         --component-plist "$COMP_PLIST" \
         "$COMPONENT_PKGS/Standalone.pkg"

# Create distribution XML for custom installer
echo "Creating distribution definition..."
sed -e "s/{{VERSION}}/$VERSION/g" -e "s/{{IDENTIFIER}}/$IDENTIFIER/g" \
    "$RESOURCES_DIR/distribution.xml.template" > "$OUTPUT_DIR/distribution.xml"

# Create welcome HTML
sed "s/{{VERSION}}/$VERSION/g" \
    "$RESOURCES_DIR/welcome.html.template" > "$OUTPUT_DIR/welcome.html"

# Build the product package
echo "Building product installer..."
productbuild --distribution "$OUTPUT_DIR/distribution.xml" \
             --package-path "$COMPONENT_PKGS" \
             --resources "$OUTPUT_DIR" \
             "$OUTPUT_DIR/Turrama-${VERSION}-unsigned.pkg"

# Sign the package
FINAL_PKG="$OUTPUT_DIR/Turrama-${VERSION}.pkg"
if [ "$CAN_SIGN_PKG" = true ]; then
    echo ""
    echo "Signing installer package..."
    productsign --sign "$DEV_INSTALLER_IDENTITY" \
        "$OUTPUT_DIR/Turrama-${VERSION}-unsigned.pkg" \
        "$FINAL_PKG"
    rm "$OUTPUT_DIR/Turrama-${VERSION}-unsigned.pkg"
    
    # Verify package signature
    if pkgutil --check-signature "$FINAL_PKG" | grep -q "Developer ID Installer"; then
        echo "  ✓ Package signed successfully"
    else
        echo "  ✗ Package signature verification failed"
        exit 1
    fi
else
    mv "$OUTPUT_DIR/Turrama-${VERSION}-unsigned.pkg" "$FINAL_PKG"
    echo "⚠ Package not signed (no Developer ID Installer certificate)"
fi

# Notarize the package
if [ "$CAN_SIGN_PKG" = true ] && [ "$CAN_NOTARIZE" = true ]; then
    echo ""
    echo "Submitting for notarization..."
    echo "(This may take a few minutes)"
    
    if xcrun notarytool submit "$FINAL_PKG" \
        --apple-id "$APPLE_ID" \
        --password "$APPLE_APP_PASSWORD" \
        --team-id "$APPLE_TEAM_ID" \
        --wait; then
        
        echo "  ✓ Notarization successful"
        
        # Staple the ticket
        echo "Stapling notarization ticket..."
        if xcrun stapler staple "$FINAL_PKG"; then
            echo "  ✓ Ticket stapled"
        else
            echo "  ✗ Failed to staple ticket"
        fi

        # Also staple the standalone app in the build dir: the release zip is
        # created from these artifacts, and a zipped app can't inherit the
        # pkg's staple. The pkg submission covers the app's cdhash, so a
        # ticket exists for it. (VST3/AU bundles can't be stapled — hosts
        # rely on Gatekeeper's online check for those.)
        if xcrun stapler staple "$BUILD_DIR/${ARTIFACT_SUBDIR}Standalone/Turrama.app"; then
            echo "  ✓ Standalone app stapled"
        else
            echo "  ⚠ Could not staple standalone app — zipped app will need internet for Gatekeeper"
        fi
    else
        echo "  ✗ Notarization failed"
        echo "Check logs with: xcrun notarytool log <submission-id> --apple-id $APPLE_ID --team-id $APPLE_TEAM_ID"
    fi
fi

echo ""
echo "============================================"
echo "✓ Installer created: $FINAL_PKG"
echo ""
if [ "$CAN_SIGN_PKG" = true ]; then
    echo "  ✓ Signed with: $DEV_INSTALLER_IDENTITY"
else
    echo "  ⚠ Not signed (no certificate)"
fi
if [ "$CAN_NOTARIZE" = true ] && [ "$CAN_SIGN_PKG" = true ]; then
    echo "  ✓ Notarized and stapled"
else
    echo "  ⚠ Not notarized"
fi
echo ""
echo "The installer allows users to choose:"
echo "  ☑ VST3 Plugin (default: enabled)"
echo "  ☑ AU Plugin (default: enabled)"
echo "  ☑ Standalone App (default: enabled)"
echo ""
echo "File size:"
du -h "$FINAL_PKG"
