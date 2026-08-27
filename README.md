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

Both builders pin JingMatrix LSPatch `v1.2` build `487` by its release-jar SHA-256
(`d238fdc414d121b7fa454d8b4ccf420df3a8c97d563761861ff92bd9c5da2165`) and verify the digest
before execution. Published bundles must report Vector API 102, LSPatch 1.2, signature-bypass
level 2, and byte-identical embedded module/origin inputs. When the Another Eden `main` source is
selected, the builder additionally rejects classic XposedBridge modules and requires modern
libxposed API 102. The separate emulator compatibility branch retains its own loader contract.

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
The local rehearsal path uses Android SDK `zipalign`/`apksigner` too; it requires
`LSPATCH_JAR`, `ASHFUR_KEYSTORE`, `ASHFUR_ALIAS`, `ASHFUR_STORE_PASS`, `ASHFUR_KEY_PASS`, and
`AE_HOST_CERT_SHA256` (plus `ANDROID_HOME`, unless `APKSIGNER` and `ZIPALIGN` are supplied). It
refuses any patcher digest other than the pinned v1.2 release and validates the runtime config and
every output certificate.
