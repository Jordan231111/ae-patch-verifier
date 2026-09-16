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

Another Eden verification runs the module's production C++ resolvers and patch functions in a
Web Worker. The ARM64 ELF is mapped and relocated into a private byte image; uploaded game
instructions are never executed or sent to the server. The report checks resolved layouts,
unique apply/undo sites, repeated-operation idempotence, and byte-for-byte image restoration.
A static pass verifies compatibility of the code contracts, not the state of a running shop.

`native/provenance.json` records the imported module commit, hashes of every Item Injection
production input (including the Android integration), and hashes of the generated native files.
Imports reject uncommitted production inputs unless explicitly requested with `--allow-dirty`;
the normal checks reject dirty provenance. To import a reviewed module checkout and rebuild:

```sh
python3 scripts/build-native-engine.py /path/to/ae-pcd-stamp-tracer
npm run check
```

Commit the generated C++/headers, `native/coverage.js`, and provenance together.
`npm run check` verifies their integrity, compiles/runs the production queue tests, and checks
the API, ELF loader and report's missing/duplicate/truncated-output behavior. GitHub Actions
also performs these checks and compiles the deployable WASM engine on pushes and pull requests.
Vercel runs `scripts/build-web-engine.sh` (also `npm run build`) with pinned
Emscripten 6.0.9 to regenerate `native/engine.js` and `native/engine.wasm`. These compiled
assets are ignored by Git. The SDK checkout is pinned by commit in the build script.
Game binaries, downloaded APKs, and local regression fixtures are not deployment inputs.

### Item Injection coverage

The browser imports the module's actual pure resolver and queue code. It does not invoke
uploaded ARM64 instructions, attach to a game or perform a grant. The 25 added checks cover:

- Token repository, classification and assignment; ordinary grants and key/ticket history.
- Real metadata getters, dynamic-cast operands and the instruction-derived grant virtual slot.
- Both resource predicates, pool members, low-water marks and inventory amount ceiling.
- All three initial-equipment eligibility predicates and their shared member.
- Unidentified-equipment factory, embedded ID member and secure-copy agreement.
- Save-manager/call-site consensus and the synchronization function.
- Actual production parsing/queue tests: 20,000 IDs, per-ID quantities, duplicate aggregation,
  overflow rejection, partial progress, cancellation, conflicting deltas and no terminal replay.

All native dialogue targets and field contracts are also reported, alongside the existing
catalog, shop, achievement, reward, battle and exact patch round-trip checks. The generated
coverage manifest makes omitted checks fail; old engine output cannot silently pass.
Optional Lua APIs are N/A only when the production resolver explicitly reports genuine
absence. An ambiguous optional binding remains a failure.

Five **RUNTIME** rows explicitly cover what a file cannot prove: live objects/ABI calls,
actual counts and special types, adaptive timing/memory/resource supply, networking/save
acknowledgement/persistence, and Android scheduling/storage/feature isolation. These rows
are never counted as static passes. The module's runtime test scope is documented in
[its Item Injection report](https://github.com/Jordan231111/ae-pcd-stamp-tracer/blob/main/docs/ITEM_INJECTION.md).

### Historical regression results

[native/validation.json](native/validation.json) records hashes and outcomes for all 13 retained
libraries, using the website's real ELF loader, compiled WASM and report parser:

```sh
npm run check:fixtures -- /path/to/ae-pcd-stamp-tracer/fixtures/libapp/arm64-v8a
```

All 25 Item Injection checks pass on **13/13** versions from 3.10.70 through 3.17.0.
The current 3.17.0 library has **105 static passes, zero failures and five runtime-only rows**.
Some older libraries still expose genuine ambiguity in pre-existing dialogue bindings:

| Libraries | Existing ambiguous binding |
| --- | --- |
| 3.15.50, 3.15.60 | lua.resetPlaySpeedAndAutoText |
| 3.16.60, 3.16.70, 3.16.71 | lua.setAutoFeedWaitTimeMinimum |

Those bindings have two candidate functions. They remain visible as FAIL, and the full
fixture command therefore exits nonzero for those files; they are not Item Injection failures
and are not hidden behind an N/A label. Other genuine optional absences remain N/A.
No historical game binaries are committed or deployed. Field-movement and contradictory-call
tests operate only on disposable copies of the library.

### Deployment and prebuilt freshness

Every deployment generates `native/build.json` with the **verifier commit**, imported **module
commit**, and compiled asset hashes. The Worker checks matching provenance and verifies the
engine JS/WASM SHA-256 before loading, so mixed deployment assets fail rather than producing
an apparently clean result. The page displays both site and module revisions.

The module metadata endpoint returns the full selected commit and requires the chosen release
or debug APK asset; a debug-only release cannot stand in for a release build. The LSPatch builder
already pins that full commit and downloads its durable `module-<sha>` asset. It does not use
bundled APKs from this website repository.

### Compatibility boundaries and performance

Addresses and private game-field offsets are decoded rather than taken from a version table.
The recognizers still require supported ARM64 instruction shapes, anchors and C++/ELF ABI
contracts. Compiler register allocation, inlining, removed strings, engine rewrites or changed
inventory/save behavior can require a resolver update. Historical static passes are evidence
of compatibility, not proof of future runtime semantics.

The website checks a **pinned module implementation**. A new game library can be checked against
that implementation immediately; changing the module implementation requires regenerating and
deploying the verifier sources. Provenance checks detect inconsistent imports; they do not
automatically redesign a resolver for a future module or game.

The production byte-mask matcher is imported verbatim and checked against 4,000 independent
reference cases. Byte-patch names and version alternatives come from the production descriptors.
Round-trip validation checks the actual union of owned write ranges and byte-for-byte restoration,
without fixing the number of writes or requiring identical write granularity during undo.

Verification runs in a Web Worker and retains a private image for reliable apply/undo checks.
On the development machine, the full 3.17.0 check took about 7 seconds, including about 6.7 seconds
in the native engine. Alternative scanners were benchmarked and rejected because they were slower.
This is a measured sample, not a speed guarantee on other devices or files.
