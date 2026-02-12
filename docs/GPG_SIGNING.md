# GPG Signing for Linux Releases

This document describes how GPG signing is implemented for Linux releases and how to verify signatures.

## Overview

All Linux releases (`.tar.gz` archives) are signed with GPG to ensure authenticity and integrity. Each release includes:
- The release archive: `Boomerang-<version>-Linux.tar.gz`
- A detached GPG signature: `Boomerang-<version>-Linux.tar.gz.asc`

## For Users: Verifying Releases

### Step 1: Import the Public Key

First time only, import the MC Music Workshop public GPG key:

```bash
# Option A: From a keyserver
gpg --keyserver keys.openpgp.org --recv-keys <KEY_ID>

# Option B: Import from a file (if provided)
gpg --import mc-music-workshop.asc
```

The key ID and fingerprint will be published in release notes and on the project website.

### Step 2: Verify the Signature

Download both the archive and its signature file, then verify:

```bash
gpg --verify Boomerang-2.0.0-Linux.tar.gz.asc Boomerang-2.0.0-Linux.tar.gz
```

### Step 3: Interpret the Results

**Good signature:**
```
gpg: Signature made Wed 12 Feb 2026 04:52:00 AM UTC
gpg:                using RSA key XXXXXXXXXXXXXXXX
gpg: Good signature from "MC Music Workshop <contact@example.com>"
```

✅ The archive is authentic and hasn't been tampered with.

**Bad signature:**
```
gpg: BAD signature from "MC Music Workshop <contact@example.com>"
```

⛔ **DO NOT USE** this archive. Report the issue immediately.

**Trust warnings:**
```
gpg: WARNING: This key is not certified with a trusted signature!
```

This warning is normal if you haven't explicitly trusted the key. The important part is seeing "Good signature". To eliminate the warning, you can mark the key as trusted:

```bash
# Trust the key (optional)
gpg --edit-key <KEY_ID>
gpg> trust
# Select trust level (usually 5 = ultimate)
gpg> quit
```

## For Maintainers: Signing Releases

### Initial Setup

1. **Generate a GPG key** (if you don't have one):
   ```bash
   gpg --full-generate-key
   # Choose: RSA and RSA (default), 4096 bits, no expiration
   # Use your real name and project email
   ```

2. **Export the private key**:
   ```bash
   gpg --export-secret-keys --armor <KEY_ID> > private-key.asc
   ```

3. **Store in GitHub Secrets**:
   - `GPG_PRIVATE_KEY`: Content of `private-key.asc`
   - `GPG_PASSPHRASE`: The passphrase for the key

4. **Publish the public key**:
   ```bash
   # Export public key
   gpg --export --armor <KEY_ID> > mc-music-workshop.asc
   
   # Upload to keyserver
   gpg --keyserver keys.openpgp.org --send-keys <KEY_ID>
   ```

5. **Document the key fingerprint** in release notes and README.

### GitHub Actions Integration

The signing process is automated in `.github/workflows/build.yml`:

1. **Import GPG Key** step: Imports the private key from secrets
2. **Package Linux** step: Creates the tarball and signs it with `gpg --detach-sign`
3. The signature file is automatically uploaded with the release

### Manual Signing (for testing)

To sign a release locally:

```bash
# Sign the archive
gpg --armor --detach-sign --output Boomerang-2.0.0-Linux.tar.gz.asc \
    Boomerang-2.0.0-Linux.tar.gz

# Verify your signature
gpg --verify Boomerang-2.0.0-Linux.tar.gz.asc Boomerang-2.0.0-Linux.tar.gz
```

## Best Practices

1. **Key Management**:
   - Use a strong passphrase for the GPG key
   - Back up the private key securely (encrypted)
   - Consider using a dedicated signing subkey
   - Set an expiration date and plan for key rotation

2. **Key Publication**:
   - Upload public key to multiple keyservers (keys.openpgp.org, keyserver.ubuntu.com)
   - Include the key fingerprint in README and release notes
   - Publish the public key file in the repository or on the project website

3. **Verification**:
   - Always verify signatures after generating them
   - Test the verification process from a clean environment
   - Document the verification process for users

4. **Security**:
   - Never commit private keys to version control
   - Rotate keys periodically (every 2-3 years)
   - Revoke compromised keys immediately

## Key Rotation

When rotating keys:

1. Generate a new key with updated expiration
2. Sign the new key with the old key (establishes trust chain)
3. Update GitHub secrets with new key
4. Publish transition notice in release notes
5. Keep old key available for verifying historical releases
6. After transition period, revoke old key

## Troubleshooting

### "gpg: Can't check signature: No public key"
Import the public key first (see Step 1 above).

### "gpg: WARNING: This key is not certified"
This is normal. You can optionally trust the key (see Step 3 above).

### "gpg: BAD signature"
The archive has been tampered with or corrupted. Do not use it.

### Signature file not found
Ensure you downloaded both the `.tar.gz` and `.tar.gz.asc` files.

## References

- [GNU Privacy Guard](https://gnupg.org/)
- [Debian: Creating Signed GitHub Releases](https://wiki.debian.org/Creating%20signed%20GitHub%20releases)
- [GitHub Actions: Import GPG](https://github.com/marketplace/actions/import-gpg)
- [OpenPGP Best Practices](https://riseup.net/en/security/message-security/openpgp/best-practices)
