# AE Patch Verifier and LSPatch Workshop

[LSPatch Workshop](https://verify-ae-modmenu.vercel.app/) delivers signed ARM64 APK sets. The [advanced workshop](https://verify-ae-modmenu.vercel.app/workshop) contains per-app builders and the read-only Another Eden native verifier.

## Delivery

A request resolves the current publisher release and completed module build. An exact input match reuses its verified immutable download. Concurrent requests for the same edition share one build. Browsing the catalog does not trigger builds.

- Another Eden Global uses Google Play. Japan explicitly uses the ARM64 APKPure variant. Release and debug module editions are separate assets.
- Epic Conquest 2 and Rust Axion use the modules and producer packaging tools pinned in `scripts/catalog/inputs.json`. Each producer has its own reviewed contract.
- OnceWorld tries Google Play first, with an exact-version, original-signer-checked APKPure fallback. AllRelic uses APKPure.
- ReLevel uses its separate 15-minute background preparation flow.

All hosts use **LSPatch 1.2 build 487**, checked against its published SHA-256. EC2 uses signature level **0**, with scoped startup and Google identity support. Other supported catalog hosts use level **2**. Do not substitute another loader or apply EC2's setting globally.

The workflow verifies the complete publisher split set, selected module, signing identity and finished APK set. Source Stamp certificates are distributor attestations, not APK signers. Google identity metadata derives from the verified original and selected MicroG-RE companion. The reviewed host helper must match the module's explicit preparation contract; an APK cannot authorize arbitrary embedded build code. Unsupported contracts fail before publication.

After verification, a raw Actions artifact starts delivery while the job archives the same bytes as a permanent GitHub Release. The client checks the artifact's run, name, size, expiry and digest before using a signed storage URL. The server redirects downloads without buffering or recompressing them. If temporary delivery is unavailable, polling continues to the permanent release.

The `workshop-ready` branch contains current receipts and request claims. `/api/catalog` merges valid receipts with supported edition metadata. Retired signing identities cannot be served as current downloads. `catalog.json` may omit a download until the edition has a valid production receipt.

The on-demand target is **less than 60 seconds from click to first file bytes**, including cold requests. This is a target, not a promise about GitHub scheduling or upstream transfers. Queue time, source transfer, packaging, upload and first-byte latency must be measured separately. ReLevel is prepared on its own schedule.

## Shared signing

Owned modules, MicroG-RE and every delivered host split use one identity:

- PKCS12; RSA 4096; SHA256withRSA; alias `jordan-modules`.
- Valid November 25, 2026 through November 25, 2036.
- Local profile: `/Users/jordan/.android/signing/jordan-modules.json`.
- Local keystore: `/Users/jordan/.android/signing/jordan-modules.p12`.
- Password file: `/Users/jordan/.android/signing/jordan-modules.password`.

The directory and files are private. Keep signing material out of repositories, build outputs, command lines, browser code and logs. Local commands load the profile with `python3 scripts/shared_signing.py COMMAND [ARGUMENT ...]`.

CI stages the key under `$RUNNER_TEMP`, verifies its certificate, and removes it on completion. Configure secrets `TEMPLATE_KS_BASE64`, `TEMPLATE_KS_PASS`, `TEMPLATE_KEY_ALIAS`, `TEMPLATE_KEY_PASS`, and public variable `TEMPLATE_CERT_SHA256`. The expected certificate is in `scripts/catalog/inputs.json`. Install every base/split together; signing does not replace Android's package and version rules.

## Service configuration

GitHub Actions also uses:

- `AE_MODULE_REPO`, `AE_MODULE_REPO_TOKEN`, `ONCEWORLD_MODULE_REPO` for completed module releases.
- `GPLAYDL_API_KEY` and `GPLAYDL_GLOBAL_EMAIL` for the selected disposable Play delivery account.
- `RELEVEL_CI_TRANSFER_KEY` for encrypted private inputs between public CI jobs.
- Source-certificate variables `AE_GLOBAL_SOURCE_CERT_SHA256`, `AE_JAPAN_SOURCE_CERT_SHA256`, `ONCEWORLD_SOURCE_CERT_SHA256`, `ALLRELIC_SOURCE_CERT_SHA256`, and `RELEVEL_SOURCE_CERT_SHA256S`.

Vercel needs the repository/workflow/API-token settings read by `api/_shared/github.js`. It never receives APK signing material or Play credentials. `api/workshop.js` routes catalog, OnceWorld and AllRelic handlers through the existing eight deployment functions.

Play delivery uses the hash-locked public wheels in `.github/play-client`. The checked-out patcher and archive tools avoid repeated dependency resolution. Private producer tools are pinned by source commit and file hash, downloaded without signing credentials, then invoked with only the signing environment.

## ReLevel

The Cloudflare Durable Object in `workers/relevel-timer` requests a check every 15 minutes. The public workflow resolves the current Play version and completed module, then prepares a changed input once. Its cache identity includes the signing certificate. Module-only changes can reuse verified recovered game inputs.

A changed game requires two repeatable pre-init native captures in disposable ARM64 Android. Unknown callbacks, unsupported layouts, nonrepeatable native data, failed hooks or failed signatures cannot publish. The resulting production bundle is checked in a fresh runtime before its ready index advances. Web requests never build or force a retry.

Failures have a bounded three-attempt budget. The timer uses one alarm, bounded network timeouts and backoff. A stale observation or changed input cannot relabel an old file as current. Only the current bundle and recovered inputs required to rebuild it are retained. Temporary capture artifacts are removed when the run completes.

## Native verifier

The browser worker maps an uploaded ARM64 ELF into a private byte image and runs production instruction, RTTI, call-flow and unwind contracts. It never executes uploaded ARM64 instructions or mutates a running game. Seven runtime-only rows distinguish live objects, inventory changes, resources, saving, Android lifecycle, hook chains and shop ownership from file checks.

`native/provenance.json` binds the imported resolver to its clean module commit and input hashes. `native/build.json` binds the deployment's JS/WASM and verifier commit. The worker checks these before execution. Missing or duplicate checks, ambiguous targets, a nonzero exit, changed input bytes or an incomplete report fail verification. Optional Lua bindings are N/A only when their registration is genuinely absent.

Regenerate from a clean committed module checkout:

```sh
python3 scripts/build-native-engine.py /Users/jordan/Downloads/PersonalAppReverse/ae-pcd-stamp-tracer
npm run check
```

Generation and compilation use an atomic staging directory. Failed imports leave the working engine intact. `--allow-dirty` labels development imports that deployment checks reject. `--skip-build` generates sources and removes obsolete compiled output.

Commit generated sources, coverage and provenance together, then build deployable assets:

```sh
npm run build
npm run check:fixtures -- /path/to/original/libapp.so
```

Emscripten 6.0.9 is pinned for reproducibility. Compiled assets are generated during deployment. Game binaries and raw trials are never committed. Regression tests cover report completeness, mixed deployment artifacts, source/split signing, cache invalidation, failed publication and request coalescing. New layouts require validation against their own originals; passing an existing fixture is not a future-version guarantee.

ZIP entry alignment and ELF LOAD/RELRO compatibility are independent. Current MicroG-RE has two native dependencies that require 4 KB compatibility mode on a 16 KB system; ZIP alignment does not establish full 16 KB runtime support.
