const { test } = require("node:test");
const assert = require("node:assert/strict");
const { mergeReady } = require("../server/catalog");
const catalog = require("../catalog.json");

test("the deployed snapshot contains a numbered release and a real download path for every app", () => {
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
      } else {
        assert.match(variant.sha256, /^[a-f0-9]{64}$/);
        assert.match(variant.downloadUrl, /^https:\/\/github\.com\/Jordan231111\/ae-patch-verifier\/releases\/download\/catalog-/);
      }
    }
  }
});

test("only a matching immutable signed asset can replace its own catalog entry", () => {
  const seed = catalog.apps.find(app => app.id === "rustaxion").variants[0];
  const next = { ...seed, version: "2.0.0", versionCode: seed.versionCode + 1,
    updatedAt: "2099-01-01T00:00:00Z", sha256: "a".repeat(64) };
  next.downloadUrl = `https://github.com/Jordan231111/ae-patch-verifier/releases/download/catalog-${seed.id}-${next.sha256.slice(0, 20)}/${seed.id}-${next.version}.apks`;
  const apply = delta => mergeReady(catalog, { [seed.id]: { ...next, ...delta } }).apps
    .find(app => app.id === "rustaxion").variants[0];
  assert.equal(apply({}).downloadUrl, next.downloadUrl);
  for (const delta of [{ signer: "changed" }, { downloadUrl: "https://example.org/file" },
    { versionCode: seed.versionCode - 1 }, { updatedAt: "2000-01-01" }, { sizeBytes: 0 }]) {
    assert.deepEqual(apply(delta), seed);
  }
  assert.deepEqual(mergeReady(catalog, undefined), catalog);
  const gated = catalog.apps.find(app => app.statusUrl);
  assert.deepEqual(mergeReady(catalog, { [gated.variants[0].id]: next }).apps.find(app => app.id === gated.id), gated);
});
