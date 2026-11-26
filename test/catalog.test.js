const { test } = require("node:test");
const assert = require("node:assert/strict");
const { mergeReady } = require("../server/catalog");
const catalog = require("../catalog.json");
const { entry, identity, workflowInputs } = require("../server/catalog/requests");

test("the catalog contains supported editions and only offers identified signed releases", () => {
  assert.equal(new Set(catalog.apps.map(app => app.id)).size, catalog.apps.length);
  const ids = new Set();
  for (const app of catalog.apps) {
    assert.ok(app.variants.length);
    for (const variant of app.variants) {
      assert.ok(!ids.has(variant.id));
      ids.add(variant.id);
      assert.match(variant.version, /^\d/);
      assert.ok(variant.versionCode > 0);
      assert.ok(variant.sizeBytes > 0);
      if (app.statusUrl) {
        assert.equal(app.statusUrl, "/api/relevel/status");
      } else if (variant.downloadUrl) {
        assert.match(variant.sha256, /^[a-f0-9]{64}$/);
        assert.match(variant.downloadUrl, /^https:\/\/github\.com\/Jordan231111\/ae-patch-verifier\/releases\/download\/catalog-/);
      }
    }
  }
});

test("ReLevel is excluded from on-demand builds and its timer remains the only preparation route for it", () => {
  assert.equal(entry("relevel-arm64-release"), null);
  assert.equal(entry("unknown"), null);
  const { app, variant } = entry("anothereden-japan-arm64-debug");
  const module = { sha: "a".repeat(40), ref: "main" };
  const source = { source: "apkpure", versionName: "1.2", versionCode: "3" };
  const key = identity(variant, source, module);
  assert.notEqual(key, identity(variant, { ...source, versionCode: "4" }, module));
  assert.notEqual(key, identity(variant, source, { ...module, sha: "b".repeat(40) }));
  assert.notEqual(identity(variant, source, { ...module, recipe: "old" }),
    identity(variant, source, { ...module, recipe: "new" }));
  assert.notEqual(identity(variant, source, { ...module, assetDigest: "a".repeat(64) }),
    identity(variant, source, { ...module, assetDigest: "b".repeat(64) }));
  assert.notEqual(key, identity({ id: "anothereden-global-arm64-debug" }, source, module));
  const candidate = { key, version: "1.2", code: "3", source: "apkpure" };
  const dispatch = workflowInputs(app, variant, candidate, module, "1234567890123-1234abcd");
  assert.equal(dispatch.region, "japan");
  assert.equal(dispatch.variant, "debug");
  assert.equal(dispatch.playLocale, "ja-JP");
  assert.equal(dispatch.expectedVersionCode, "3");
  assert.deepEqual(JSON.parse(dispatch.catalog), candidate);
  const rust = entry("rustaxion-arm64-release");
  assert.equal(workflowInputs(rust.app, rust.variant, candidate, module, "nonce").candidate, JSON.stringify(candidate));
});

test("only a matching immutable signed asset can replace its own catalog entry", () => {
  const seed = catalog.apps.find(app => app.id === "rustaxion").variants[0];
  const next = { ...seed, version: "2.0.0", versionCode: seed.versionCode + 1, sizeBytes: 100,
    updatedAt: "2099-01-01T00:00:00Z", sha256: "a".repeat(64) };
  next.downloadUrl = `https://github.com/Jordan231111/ae-patch-verifier/releases/download/catalog-${seed.id}-${next.sha256.slice(0, 20)}/${seed.id}-${next.version}.apks`;
  const apply = delta => mergeReady(catalog, { [seed.id]: { ...next, ...delta } }).apps
    .find(app => app.id === "rustaxion").variants[0];
  assert.equal(apply({}).downloadUrl, next.downloadUrl);
  for (const delta of [{ signer: "changed" }, { downloadUrl: "https://example.org/file" },
    { versionCode: seed.versionCode - 1 }, { updatedAt: "invalid" }, { sizeBytes: 0 }]) {
    assert.deepEqual(apply(delta), seed);
  }
  assert.deepEqual(mergeReady(catalog, undefined), catalog);
  const gated = catalog.apps.find(app => app.statusUrl);
  assert.deepEqual(mergeReady(catalog, { [gated.variants[0].id]: next }).apps.find(app => app.id === gated.id), gated);
});
