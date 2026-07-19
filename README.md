# AE Patch Verifier and LSPatch Workshop

This repository hosts the verifier UI and the short-lived GitHub Actions builders used by
[verify-ae-modmenu.vercel.app](https://verify-ae-modmenu.vercel.app). It supports Another Eden and
the ARM64 OnceWorld APKPure release while keeping their build inputs, release tags, and signing
identities separate.

## Build flow

1. The Vercel API resolves the current APKPure version and a pinned prebuilt module commit.
2. It dispatches the game-specific `workflow_dispatch` workflow with that immutable version and
   module selection.
3. The workflow downloads and validates the XAPK, patches the base, signs the complete split set,
   verifies the result, and publishes a short-lived release asset.
4. The browser polls the same-origin status API and starts the download when the asset is ready.

The janitor workflow removes temporary `lspatch-*` and `onceworld-lspatch-*` releases. Durable
module releases live in their module repositories and are never removed by this janitor.

## Signing identities

The identities are intentionally different and must never be interchanged:

- **Another Eden:** the established Ashfur keystore is stored as encrypted GitHub Actions secrets.
  The workflow reconstructs it only under `$RUNNER_TEMP` and verifies its public certificate
  fingerprint before patching.
- **OnceWorld:** the default LSPatch host key is extracted from the exact integrity-pinned LSPatch
  jar. Its expected public certificate fingerprint is also checked before patching.

Changing either identity prevents an unrooted Android installation from updating a prior build.
Always install the complete base and split set; signing compatibility does not bypass Android's
normal package-name and version-code rules.

## Private configuration

Credential values belong in GitHub Actions secrets or Vercel environment variables, never in Git.
The workflows expect these GitHub secrets:

- `AE_HOST_KEYSTORE_BASE64`, `AE_HOST_KEYSTORE_PASSWORD`, `AE_HOST_KEY_ALIAS`
- `AE_MODULE_REPO`, `AE_MODULE_REPO_TOKEN`
- `ONCEWORLD_MODULE_REPO`

Public, non-secret identity checks use these GitHub variables:

- `AE_HOST_CERT_SHA256`
- `ONCEWORLD_HOST_CERT_SHA256`
- `ONCEWORLD_MODULE_CERT_SHA256`

The Vercel functions read repository, workflow, module, and API-token configuration from the
environment. `.env*`, browser-test data, OS metadata, and `builder/signing/` are ignored to reduce
the chance of accidentally committing local credentials or PII.

## Local checks

```sh
npm ci
npm run check
```

Real signing and packaging checks run in GitHub Actions because their credentials are not available
to public checkouts. Both builders use retries, transfer fallbacks, archive validation,
package/version/ABI checks, and post-signing certificate verification before publishing an asset.
