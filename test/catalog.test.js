const { test } = require("node:test");
const assert = require("node:assert/strict");
const { readFileSync } = require("node:fs");
const { runInNewContext } = require("node:vm");
const { mergeReady } = require("../server/catalog");
const catalog = require("../catalog.json");
const inputs = require("../scripts/catalog/inputs.json");
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

test("Daphne preflights the exact APKPure ARM64 variant before requesting its pinned module build", async () => {
  const { app, variant } = entry("daphne-arm64-release");
  const seen = [];
  const config = { githubOwner: "owner", githubRepo: "workshop", githubRef: "main" };
  const dependencies = {
    "node:crypto": require("node:crypto"),
    "../../catalog.json": catalog,
    "../../scripts/catalog/inputs.json": inputs,
    "../../api/_shared/recipes": require("../api/_shared/recipes"),
    "./index": { mergeReady },
    "./transfer": {},
    "../../api/_shared/googleplay": {
      resolveLatestPlayListing() { assert.fail("Daphne must use the configured APKPure source"); }
    },
    "../../api/_shared/apkpure": {
      async resolveLatestXapk(packageName) {
        assert.equal(packageName, variant.packageName);
        return { versionName: "2.3.0", versionCode: "23446" };
      },
      async resolveXapkVariant(...params) {
        assert.deepEqual(params, [variant.packageName, "23446", "arm64-v8a", "2.3.0"]);
        seen.push("preflight");
      }
    },
    "../../api/_shared/github": {
      config: () => config,
      async githubJson(_config, method, path, body) {
        if (method === "GET") throw Object.assign(new Error("not found"), { status: 404 });
        if (method === "PUT") return { content: { sha: "ticket" } };
        assert.match(path, /\/actions\/workflows\/catalog-ready\.yml\/dispatches$/);
        const candidate = JSON.parse(body.inputs.candidate);
        assert.equal(candidate.app, app.id);
        assert.equal(candidate.source, "apkpure");
        assert.equal(candidate.module, inputs.daphne.moduleSha256);
        assert.equal(candidate.recipe, require("../api/_shared/recipes").recipe(app.id));
        assert.equal(candidate.previous, null);
        seen.push("dispatch");
        return { workflow_run_id: 123 };
      }
    }
  };
  const module = { exports: {} };
  runInNewContext(readFileSync(require.resolve("../server/catalog/requests"), "utf8"), {
    module, require: id => dependencies[id], Buffer, URL, URLSearchParams, console
  });
  const response = { setHeader() {}, end(body) { this.body = JSON.parse(body); } };
  await module.exports.build({
    method: "POST", url: "/api/catalog/build",
    headers: { "content-type": "application/json" },
    body: { variant: variant.id }
  }, response);
  assert.equal(response.statusCode, 202);
  assert.equal(response.body.status, "preparing");
  assert.deepEqual(seen, ["preflight", "dispatch"]);
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
