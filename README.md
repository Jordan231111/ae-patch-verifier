# AE Patch Verifier and LSPatch Workshop

This repository hosts the verifier UI and the short-lived GitHub Actions builders used by
[verify-ae-modmenu.vercel.app](https://verify-ae-modmenu.vercel.app). It supports Another Eden,
ARM64 OnceWorld, and ARM64 ReLevel while keeping their inputs, release tags, and signing identities
separate. OnceWorld resolves and downloads from Google Play first through an Aurora-compatible
client; its established APKPure path remains a guarded fallback. Another Eden Global uses Google
Play directly, while the region-gated Japan package uses the optimized APKPure XAPK source.

## Another Eden and OnceWorld build flow

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

## ReLevel prepared downloads and updates

ReLevel's Download button resolves a verified GitHub Release and redirects to its storage URL.
It does not dispatch Actions, download Play inputs, compile a module, or recover PairIP. APK bytes
never pass through Vercel. This targets a download start within a minute; transfer time depends on
the recipient's connection.
Vercel reads the fresh verified public index; private ReLevel source/module access remains in CI.

`relevel-ready.yml` checks the dedicated account's current Google Play **versionCode** and the
newest completed `main` module prebuilt every 15 minutes. An unchanged check only refreshes the
small `relevel-ready` release index. A module update reuses verified recovered inputs for the same
Play version. A game update fetches the complete split set and runs fresh recovery. Versions,
native ranges, String holders, imports, and binding addresses come from the current inputs;
there is no game-version whitelist.

The expensive path runs in the background: verify Play signatures and the prebuilt module;
capture two independent original-app launches in native ARM64 Android; recover and sign on x86;
then install the exact candidate in a separate Android container. Every current binding and
import helper, cold startup, Home/resume, and delayed stability must pass before the index changes
to `ready`. Unknown VM callbacks, changed contracts, nonrepeatable native data, and failed hooks
require review. The same failing input is not rebuilt on every poll. Maintainers can retry it with
the workflow's **force** option after investigation.

The disposable Android runtime uses a root-configured wrapper and a separate native helper to
save Unity's writable data before its `JNI_OnLoad` and capture remaining segments just before
`il2cpp_init`. Frida attaches afterward to capture the discovered String and
lifecycle fields. Early Frida instrumentation interfered with protection startup; later native
snapshots contained process pointers and were rejected. Neither workaround nor relaxed recovery
guards is shipped: the helper stays outside the module and APKs, and the existing two-capture
repeatability, pointer, signing, and alignment checks remain mandatory.

Scheduled runs use only the public repository's standard `ubuntu-latest` and `ubuntu-24.04-arm`
runners, with one ReLevel preparation at a time and bounded job timeouts. The workflow refuses
private-repository polling. Durable bundles and recovered inputs use GitHub Releases; temporary
Actions artifacts are deleted at completion. The current and previous bundle and their recovered
inputs are retained; older ReLevel caches are pruned. No paid runner, always-on server, Vercel Cron, or
Vercel blob storage is required. Existing private module CI still uses the owner's normal included
Actions allowance; Play checks never trigger that build. See [GitHub Actions billing](https://docs.github.com/en/billing/concepts/product-billing/github-actions)
and [Release storage/bandwidth limits](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).

GitHub may delay scheduled jobs and can disable schedules after prolonged repository inactivity.
The site rejects an observation older than one hour, even if the public listing still shows the
same display version. Re-enable/run the workflow if scheduling is interrupted. Failed or pending
updates cannot relabel an older bundle as current. Publication rechecks both Play and the module
to catch updates arriving while a candidate is being prepared.

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
- **ReLevel:** the existing ReLevel key signs the prebuilt module and every split. Its public
  SHA-256 is `29b04c032802179a86374c8a13dba9ddbe8d9303f4909ac03766cb77de17c027`.

Changing an identity prevents an unrooted Android installation from updating a prior build.
Always install the complete base and split set; signing compatibility does not bypass Android's
normal package-name and version-code rules.

## Private configuration

Credential values belong in GitHub Actions secrets or Vercel environment variables, never in Git.
The workflows expect these GitHub secrets:

- `AE_HOST_KEYSTORE_BASE64`, `AE_HOST_KEYSTORE_PASSWORD`, `AE_HOST_KEY_ALIAS`
- `AE_MODULE_REPO`, `AE_MODULE_REPO_TOKEN`
- `ONCEWORLD_MODULE_REPO`
- `RELEVEL_HOST_KEYSTORE_BASE64` (the existing ReLevel key, never a generated replacement)
- `RELEVEL_CI_TRANSFER_KEY` (encrypts private module/tool inputs between public CI jobs)
- `GPLAYDL_API_KEY` (a persistent key created once with `gplaydl link`; the short pairing code is
  not used by CI)
- `GPLAYDL_GLOBAL_EMAIL` (the dedicated Play account selected for Global/OnceWorld downloads)

Public, non-secret identity checks use these GitHub variables:

- `AE_HOST_CERT_SHA256`
- `AE_GLOBAL_SOURCE_CERT_SHA256`
- `AE_JAPAN_SOURCE_CERT_SHA256`
- `ONCEWORLD_HOST_CERT_SHA256`
- `ONCEWORLD_SOURCE_CERT_SHA256`
- `RELEVEL_HOST_CERT_SHA256`
- `RELEVEL_SOURCE_CERT_SHA256S` (the reviewed complete Play signer/stamp set)

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

### APKS build latency

The on-demand Another Eden builder downloads the app, precompiled module and pinned
patcher in parallel. It never compiles the module or runs the native resolver suite.
If no matching prebuilt exists, it returns a retryable error instead of a slow build.
Downloaded source identity, patcher digest, signing identity and install alignment
remain checked. Redundant ZIP rescans of files just hashed or created are omitted.

Another Eden uploads the already-built APKS directly through the pinned
`actions/upload-artifact` v7.0.1 action with `archive: false`. This avoids both the
slow Release-assets upload route and a redundant outer ZIP. The status endpoint
selects only a finalized, unexpired artifact matching the request nonce. The download
endpoint redirects to signed artifact storage; it never buffers or repackages APKs.
Legacy completed releases remain downloadable until their normal expiry.

The polling client remembers the resolved run ID and checks at most every three
seconds rather than repeatedly listing every recent run or waiting eight seconds
after a build finishes. No downloaded game/module cache is required. A cold
30–40 second build is the target; GitHub queue/network availability remains external.
Artifacts have a one-day platform retention ceiling and the janitor removes our
APKS outputs after one hour. The janitor uses upload/publication timestamps, not
the older source commit timestamp, when expiring temporary releases.
