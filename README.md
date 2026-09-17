# AE Patch Verifier and LSPatch Workshop

This repository hosts the verifier UI and the short-lived GitHub Actions builders used by
[verify-ae-modmenu.vercel.app](https://verify-ae-modmenu.vercel.app). It supports Another Eden and
the ARM64 OnceWorld release while keeping their build inputs, release tags, and signing identities
separate. OnceWorld resolves and downloads from Google Play first through an Aurora-compatible
client; its established APKPure path remains a guarded fallback. Another Eden Global uses Google
Play directly, while the region-gated Japan package uses the optimized APKPure XAPK source.

## Build flow

1. The Vercel API resolves the current Google Play listing and a pinned prebuilt module commit. If
   Play listing metadata is unavailable, this lookup alone falls back to APKPure.
2. It dispatches the game-specific `workflow_dispatch` workflow with that immutable version and
   module selection.
3. The OnceWorld workflow requests the complete ARM64 split set directly from Google Play. It
   contacts APKPure only after Play authentication, delivery, or validation fails, and refuses a
   stale APKPure fallback when the Play listing already advertised a newer release.
4. The workflow normalizes either source into the same manifest contract, patches the base, signs
   the complete split set, verifies the result, and publishes a short-lived release asset.
5. The browser polls the same-origin status API and starts the download when the asset is ready.

Another Eden Global follows the same parallel Play/module/patcher preparation with no mirror
fallback. Japan explicitly selects APKPure because Google Play requires a Japan-entitled account.
Japan builds preflight the exact ARM64 APKPure variant before dispatch; when APKPure has only
published its 32-bit variant, the API reports that temporary upstream condition instead of
spending a runner or producing an incompatible package.

The janitor workflow removes temporary `lspatch-*` and `onceworld-lspatch-*` releases. Durable
module releases live in their module repositories and are never removed by this janitor.

Both builders pin JingMatrix LSPatch `v1.2` build `487` by its release-jar SHA-256
(`d238fdc414d121b7fa454d8b4ccf420df3a8c97d563761861ff92bd9c5da2165`) and verify the digest
before execution. Published bundles must report Vector API 102, LSPatch 1.2, signature-bypass
level 2, and byte-identical embedded module/origin inputs. Another Eden `main` and OnceWorld both
reject classic XposedBridge modules and require modern libxposed API 102. The separate Another Eden
emulator compatibility branch retains its own loader contract.

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
- `GPLAYDL_API_KEY` (a persistent key created once with `gplaydl link`; the short pairing code is
  not used by CI)
- `GPLAYDL_GLOBAL_EMAIL` (the dedicated Play account selected for Global/OnceWorld downloads)

Public, non-secret identity checks use these GitHub variables:

- `AE_HOST_CERT_SHA256`
- `AE_GLOBAL_SOURCE_CERT_SHA256`
- `AE_JAPAN_SOURCE_CERT_SHA256`
- `ONCEWORLD_HOST_CERT_SHA256`
- `ONCEWORLD_SOURCE_CERT_SHA256`

The Vercel functions read repository, workflow, module, and API-token configuration from the
environment. `.env*`, browser-test data, OS metadata, and `builder/signing/` are ignored to reduce
the chance of accidentally committing local credentials or PII.

## Local checks

```sh
npm run check
npm run build
```

Real signing and packaging checks run in GitHub Actions because their credentials are not available
to public checkouts. Both builders use retries, transfer fallbacks, archive validation,
package/version/ABI checks, and post-signing certificate verification before publishing an asset.
The OnceWorld Play credential must belong to a dedicated account without payment methods; it is
stored only as an Actions secret and is never exposed to Vercel or browser code.
Another Eden and OnceWorld production builds run only in GitHub Actions so Play credentials and
signing material never enter Vercel or browser code.

## Native verifier provenance

Another Eden verification is a **read-only resolver audit**. A Web Worker maps the uploaded
ARM64 ELF into a private byte image and runs imported C++ instruction, RTTI, call-graph and
unwind-range contracts. The upload remains byte-identical. The website does not execute
uploaded ARM64 game code, attach to the game, change inventory or exercise a live hook chain.

Protocol 3 requires every generated target and check exactly once, `READ_ONLY unchanged=1`,
`AUDIT_COMPLETE ok=1`, and a zero engine exit code. Missing, ambiguous, duplicated, interrupted
or stale output cannot produce a successful audit. Optional Lua APIs are N/A only when their
registration name is genuinely absent; ambiguity remains a failure.

`native/provenance.json` records the module commit and SHA-256 digests for production code,
Android integration, build inputs and the locked ShadowHook source. A normal import requires
an entirely clean, committed module checkout. `--allow-dirty` is a development option whose
output is labelled and rejected by deployment checks and the browser Worker.

```sh
python3 scripts/build-native-engine.py /path/to/ae-pcd-stamp-tracer
npm run check
```

Generation and compilation take place in a temporary sibling tree. Publication uses an atomic
directory exchange only after all work succeeds and the original sources are unchanged.
Failures leave the previous generated files and compiled assets intact; concurrent edits are
preserved. `--skip-build` publishes source only and removes stale compiled assets. `npm run build`
also stages the complete native directory before publication.

Commit the generated C++/headers, coverage manifest and provenance together. After that commit,
run `npm run build` to stamp the final verifier commit into `native/build.json`. Emscripten 6.0.9
is required; an installed compiler of another version is rejected. The fallback SDK checkout
is pinned by commit. Compiled JS/WASM are deployment artifacts, not tracked source files.

### Inventory and feature coverage

The static audit includes:

- Signed int64 input, safe duplicate merging, overflow handling and production queue models.
- Token and resource contracts, limits, native save bindings and character readiness in
  anonymous ELF BSS, plus the native forbidden-currency restriction.
- Equipment, Pet equipment, Buddy equipment and unidentified-equipment owning queries,
  factories, precise removers and native count indexes.
- Pet FBS and three-index preservation contracts, and native creation publication boundaries.
- Fish pools with their original weights, native size/signature generation and inventory-only
  storage contracts. The audit does not run the sampler or create a fish.
- Legacy scalar lottery tickets versus server-issued expiry records, and character growth
  gifts whose amount/writer do not represent reversible inventory quantities.
- Independent runtime feature targets, Director delta/cap contracts, shop ownership/binders,
  RTTI/vtable uniqueness, complete FDE extents, missing/duplicate/moved targets and Lua
  registration pointer-row models.

Seven **RUNTIME** rows separate what an ELF cannot prove: live objects/native ABI calls;
actual quantities, identities and paired restoration; resources and memory pressure; saving
and persistence; Android lifecycle and feature isolation; ShadowHook chains and unhooking;
and live shop ownership/restoration. Runtime-only rows never count as static passes. Native
ownership/failure tests that execute ARM64 helpers run in disposable Android processes, and
business mutations run only in the dedicated device lab.

### Historical regression results

`native/validation.json` records the input hashes, exit codes and report counts for all
13 retained versions from 3.10.70 through 3.17.0. The command uses the website's real ELF loader,
compiled WASM and report parser, with a fresh process for each file:

```sh
npm run check:fixtures -- /path/to/ae-pcd-stamp-tracer/fixtures/libapp/arm64-v8a
```

All required contracts must pass for every supported fixture. Counts are derived from the
coverage manifest and actual engine output, so adding a check cannot silently leave an old
fixed pass total. The oldest versions have explicitly reported optional Lua absences. No game
binaries, downloaded APKs or private account snapshots are committed or deployed.

### Deployment and prebuilt freshness

`native/build.json` identifies the verifier commit, imported module commit and artifact hashes.
The Worker verifies matching provenance and the exact JS/WASM bytes before loading them,
including across an alias change. Nonzero or missing engine exit status cannot become success.
The page displays the source revisions.

Module downloads are pinned to a full module commit and its durable `module-<FULL_SHA>` GitHub
release. Release and debug APKs are distinct required assets. Module APKs embed their own
`assets/module-build.json`; a clean source commit and SHA-256 identify the published build.
Separate native debug symbols accompany the module release. No private signing material is
included in Vercel or browser code.

### Compatibility boundaries

Private offsets and call targets are derived from the inspected image, not selected from a
version/RVA table. A new compiler layout, missing anchor, changed ABI or rewritten subsystem
can require new contracts. Thirteen historical passes and current-device tests are evidence
within those scopes, not a guarantee for unknown future versions.

The website evaluates one pinned module implementation. A new library can be audited against
it immediately; a changed implementation requires regenerating and deploying the engine.
The pure byte scanner is checked against independent reference cases. Performance depends on
the uploaded image, browser and device; simulator function timings do not establish whole-device
performance or energy use.
