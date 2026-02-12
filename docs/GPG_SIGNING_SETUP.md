# Linux GPG Signing - Setup Instructions for Maintainers

This document provides step-by-step instructions for setting up GPG signing for Linux releases.

## Quick Start

The Linux GPG signing implementation is complete and ready to use. To activate it, you need to:

1. Generate a GPG key pair
2. Add the private key to GitHub Secrets
3. Publish the public key for users

## Step 1: Generate a GPG Key

Run the following command to generate a GPG key:

```bash
gpg --full-generate-key
```

**Recommended settings:**
- Key type: **RSA and RSA** (default)
- Key size: **4096 bits** (maximum security)
- Expiration: **2 years** (balance security and convenience)
- Name: **MC Music Workshop**
- Email: Use your project or organization email

**Alternative: Quick generation for testing**
```bash
# Generate a test key without prompts (NOT for production)
cat > /tmp/gpg-key-params <<EOF
Key-Type: RSA
Key-Length: 4096
Name-Real: MC Music Workshop
Name-Email: contact@example.com
Expire-Date: 2y
%no-protection
%commit
EOF

gpg --batch --gen-key /tmp/gpg-key-params
```

## Step 2: Export the Private Key

```bash
# Get your key ID
gpg --list-secret-keys --keyid-format=long

# Export the private key (ASCII armored)
gpg --export-secret-keys --armor YOUR_KEY_ID > mc-music-workshop-private.asc

# IMPORTANT: Back up this file securely!
```

## Step 3: Add to GitHub Secrets

Go to your repository settings → Secrets and Variables → Actions:

1. **Add `GPG_PRIVATE_KEY`:**
   - Name: `GPG_PRIVATE_KEY`
   - Value: Paste the entire contents of `mc-music-workshop-private.asc`
   - ⚠️ This includes the `-----BEGIN PGP PRIVATE KEY BLOCK-----` header and footer

2. **Add `GPG_PASSPHRASE`:**
   - Name: `GPG_PASSPHRASE`
   - Value: The passphrase for your GPG key
   - ⚠️ If you used `%no-protection` when generating the key, leave this secret empty

> **Note:** These secrets are already referenced in `.github/workflows/build.yml`. Once added, signing will work automatically on the next build.

## Step 4: Publish the Public Key

### 4a. Export the public key

```bash
# Export public key
gpg --export --armor YOUR_KEY_ID > mc-music-workshop-public.asc

# Get the fingerprint
gpg --fingerprint YOUR_KEY_ID
```

### 4b. Upload to keyservers

```bash
# Upload to multiple keyservers for redundancy
gpg --keyserver keys.openpgp.org --send-keys YOUR_KEY_ID
gpg --keyserver keyserver.ubuntu.com --send-keys YOUR_KEY_ID
gpg --keyserver pgp.mit.edu --send-keys YOUR_KEY_ID
```

### 4c. Document the key

Add this information to your repository:

1. **In README.md** (already has a placeholder `<KEY_ID>`):
   - Replace `<KEY_ID>` with your actual key ID
   - Add the key fingerprint

2. **In release notes:**
   - Include the key fingerprint
   - Link to the public key file

3. **Optional: Commit the public key file:**
   ```bash
   cp mc-music-workshop-public.asc docs/
   git add docs/mc-music-workshop-public.asc
   git commit -m "Add GPG public key for release verification"
   ```

## Step 5: Test the Setup

### Test in CI

1. Push a commit to trigger the build workflow
2. Check the GitHub Actions logs for the Linux build
3. Look for:
   ```
   ✓ GPG key imported (Key ID: XXXXXXXXXXXX)
   Signing Linux release...
   ✓ Signature verified successfully
   ```
4. Download the artifacts and verify locally:
   ```bash
   gpg --verify Boomerang-*-Linux.tar.gz.asc Boomerang-*-Linux.tar.gz
   ```

### Test locally

Use the provided test script:

```bash
./test-gpg-signing.sh
```

This script will:
- Create a test archive
- Sign it with your GPG key
- Verify the signature
- Report success or failure

## Troubleshooting

### "No GPG key configured" warning in CI

The secrets are not set or are empty. Check:
1. Secrets are added to the correct repository
2. Secret names match exactly: `GPG_PRIVATE_KEY` and `GPG_PASSPHRASE`
3. Private key includes the full PGP block (header and footer)

### "Failed to import GPG key" warning

The private key format is incorrect. Ensure:
1. You exported with `--armor` flag (ASCII format)
2. The entire PGP block is included
3. No extra whitespace or line breaks were added

### Signature verification fails

1. Check if the passphrase is correct (if using one)
2. Verify the key wasn't corrupted during export/import
3. Check the verify.log output in CI logs

### Key expiration

When your key expires:
1. Extend the expiration: `gpg --edit-key YOUR_KEY_ID` → `expire`
2. Or generate a new key and follow this guide again
3. Update GitHub secrets with the new key
4. Announce the key change in release notes

## Security Best Practices

1. **Backup your key securely:**
   - Store the private key file in an encrypted location
   - Use a password manager for the passphrase
   - Never commit the private key to version control

2. **Key rotation:**
   - Plan to rotate keys every 2-3 years
   - Sign the new key with the old key to establish trust
   - Keep the old key available to verify historical releases

3. **Access control:**
   - Limit who has access to GitHub secrets
   - Use organization-level secrets if managing multiple repos
   - Monitor access logs for suspicious activity

4. **Revocation:**
   - Generate a revocation certificate: `gpg --gen-revoke YOUR_KEY_ID > revoke.asc`
   - Store it securely in case you need to revoke the key
   - If compromised, revoke immediately and update all secrets

## What Happens When Signing is Active

Once the secrets are configured:

1. **Every Linux build** will be signed automatically
2. **Build artifacts** will include both `.tar.gz` and `.tar.gz.asc` files
3. **Release pages** will show the signature files
4. **Users** can verify authenticity using `gpg --verify`
5. **GitHub Actions summary** will confirm signing status

## References

- Full documentation: `docs/GPG_SIGNING.md`
- User verification guide: `README.md` → "Installation & Security"
- Test script: `test-gpg-signing.sh`

## Support

For questions or issues with GPG signing:
1. Check the troubleshooting section above
2. Review CI logs for specific error messages
3. Test locally with `test-gpg-signing.sh`
4. Refer to `docs/GPG_SIGNING.md` for detailed information
