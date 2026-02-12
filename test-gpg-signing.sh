#!/bin/bash
# Test GPG signing workflow locally
# This simulates the GitHub Actions workflow for Linux GPG signing

set -e

echo "=== GPG Signing Test Script ==="
echo ""

# Create test directory
TEST_DIR=$(mktemp -d)
echo "Test directory: $TEST_DIR"

# Create a test archive
cd "$TEST_DIR"
echo "This is a test file" > test.txt
tar -czf test-release.tar.gz test.txt
echo "✓ Created test archive: test-release.tar.gz"

# Check if GPG is available
if ! command -v gpg &> /dev/null; then
    echo "✗ GPG is not installed"
    exit 1
fi

echo "✓ GPG is available: $(gpg --version | head -1)"
echo ""

# Check if we have a GPG key
KEY_COUNT=$(gpg --list-secret-keys --keyid-format=long 2>/dev/null | grep -c "^sec" || echo "0")
if [ "$KEY_COUNT" -eq "0" ]; then
    echo "⚠ No GPG keys found. To test signing, you need a GPG key."
    echo ""
    echo "To generate a test key:"
    echo "  gpg --batch --gen-key <<EOF"
    echo "Key-Type: RSA"
    echo "Key-Length: 4096"
    echo "Name-Real: Test User"
    echo "Name-Email: test@example.com"
    echo "Expire-Date: 0"
    echo "%no-protection"
    echo "EOF"
    echo ""
    echo "Skipping signing test."
    rm -rf "$TEST_DIR"
    exit 0
fi

# Get the first key ID
KEY_ID=$(gpg --list-secret-keys --keyid-format=long 2>/dev/null | grep sec | awk '{print $2}' | cut -d'/' -f2 | head -1)
echo "Using GPG key: $KEY_ID"
echo ""

# Sign the archive (detached signature)
echo "Signing archive..."
gpg --batch --yes --armor --detach-sign --output test-release.tar.gz.asc test-release.tar.gz
echo "✓ Created signature: test-release.tar.gz.asc"
echo ""

# Verify the signature
echo "Verifying signature..."
if gpg --verify test-release.tar.gz.asc test-release.tar.gz 2>&1 | grep -q "Good signature"; then
    echo "✓ Signature verification PASSED"
    gpg --verify test-release.tar.gz.asc test-release.tar.gz 2>&1 | grep "Good signature"
else
    echo "✗ Signature verification FAILED"
    gpg --verify test-release.tar.gz.asc test-release.tar.gz
    rm -rf "$TEST_DIR"
    exit 1
fi

echo ""
echo "=== All tests passed! ==="
echo ""
echo "Signature file content (first 5 lines):"
head -5 test-release.tar.gz.asc
echo ""

# Cleanup
rm -rf "$TEST_DIR"
echo "Cleaned up test directory"
