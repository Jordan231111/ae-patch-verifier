const test = require("node:test");
const assert = require("node:assert/strict");
const github = require("../api/_shared/github");
const catalog = require("../catalog.json");
const { transferRecord } = require("../server/catalog/transfer");

const variant = catalog.apps.find(app => app.id === "rustaxion").variants[0];
const key = "b".repeat(64);
const hash = "a".repeat(64);
const raw = { ...variant, variant: variant.id, version: "2.0", versionCode: variant.versionCode + 1,
  requestKey: key, sha256: hash, runId: 10, artifactId: 21, expiresAt: "2099-01-01T00:00:00Z" };
const ticket = { key, nonce: "1795127145798-1234abcd", time: Date.now(), version: raw.version, runId: 10, transfer: raw };

test("a fast receipt is specific to the edition, signer, request, run and file lifetime", () => {
  assert.equal(transferRecord(variant, key, ticket), raw);
  assert.equal(transferRecord(variant, "c".repeat(64), ticket), null);
  for (const changes of [{ variant: "other" }, { signer: "changed" }, { version: "9.0" }, { versionCode: 0 },
    { runId: 11 }, { requestKey: "other" }, { artifactId: -1 }, { sizeBytes: 0 }, { sha256: "" },
    { expiresAt: "2020-01-01T00:00:00Z" }]) {
    assert.equal(transferRecord(variant, key, { ...ticket, transfer: { ...raw, ...changes } }), null);
  }
});

function setup(t, assetWorks = true) {
  const state = { records: {}, oldRelease: null,
    asset: { id: 21, workflow_run: { id: 10 }, name: `${variant.id}-${raw.version}.apks`, expired: false,
      expires_at: raw.expiresAt, size_in_bytes: raw.sizeBytes, digest: "sha256:" + hash } };
  const signed = "https://storage.example.test/signed.apks";
  t.mock.method(github, "config", () => ({ githubOwner: "Jordan231111", githubRepo: "ae-patch-verifier" }));
  t.mock.method(github, "githubJson", async (_, method, path) => {
    assert.equal(method, "GET");
    if (path.includes("contents/")) {
      const data = path.includes("requests/") ? ticket : state.records;
      return { encoding: "base64", content: Buffer.from(JSON.stringify(data)).toString("base64") };
    }
    if (path.endsWith("/actions/artifacts/21")) return state.asset;
    if (path.includes("/releases/tags/") && state.oldRelease) return state.oldRelease;
    if (path.endsWith("/runs/10")) return { status: "in_progress" };
    throw Object.assign(new Error("Not found"), { status: 404 });
  });
  t.mock.method(github, "githubRequest", async (_, req) => {
    assert.equal(req.apiPath, "/repos/Jordan231111/ae-patch-verifier/actions/artifacts/21/zip");
    assert.equal(req.headersOnly, true);
    assert.equal(req.accept, "application/vnd.github+json");
    return assetWorks ? { status: 302, headers: { location: signed } } : { status: 410, headers: {} };
  });
  for (const file of ["../server/catalog/transfer", "../server/catalog/requests"]) delete require.cache[require.resolve(file)];
  const routes = require("../server/catalog/requests");
  async function call(action, sha = hash) {
    const res = { headers: {}, setHeader(k, v) { this.headers[k] = v; }, end(body) { this.body = body; } };
    await routes[action]({ method: "GET", url: `/api/catalog/${action}?variant=${variant.id}&key=${key}&sha256=${sha}` }, res);
    return res;
  }
  return { call, state, signed };
}

test("a waiting visitor gets the signed raw file before the permanent record exists", async t => {
  const { call, signed } = setup(t);
  const status = JSON.parse((await call("status")).body);
  assert.equal(status.status, "ready");
  assert.equal(status.sha256, hash);
  assert.equal(status.transferUrl, signed);
  assert.equal(status.downloadUrl, `/api/catalog/download?variant=${variant.id}&key=${key}&sha256=${hash}`);
  const reply = await call("download");
  assert.equal(reply.statusCode, 302);
  assert.equal(reply.headers.location, signed);
  assert.equal(reply.headers["cache-control"], "no-store");
  assert.equal((await call("download", "f".repeat(64))).statusCode, 409);
});

test("an unavailable temporary transfer waits for the verified permanent file", async t => {
  const { call, state } = setup(t, false);
  assert.equal(JSON.parse((await call("status")).body).status, "preparing");
  assert.equal((await call("download")).statusCode, 409);
  const tag = `catalog-${variant.id}-${hash.slice(0, 20)}`;
  const name = `${variant.id}-${raw.version}.apks`;
  const url = `https://github.com/Jordan231111/ae-patch-verifier/releases/download/${tag}/${name}`;
  state.oldRelease = { draft: false, tag_name: tag, assets: [{ name, state: "uploaded", digest: "sha256:" + hash, browser_download_url: url }] };
  assert.equal((await call("download")).headers.location, url);
  state.oldRelease.draft = true;
  assert.equal((await call("download")).statusCode, 409);
  state.records[variant.id] = { ...raw, downloadUrl: url, updatedAt: "2098-01-01T00:00:00Z" };
  assert.equal(JSON.parse((await call("status")).body).downloadUrl, url);
  assert.equal((await call("download")).headers.location, url);
});


test("an upload can become readable on a later poll without trusting incomplete storage metadata", async t => {
  const { call, state } = setup(t);
  const good = state.asset;
  for (const invalid of [{ digest: null }, { workflow_run: { id: 99 } }, { name: "archive.zip" },
    { size_in_bytes: 1 }, { expired: true }, { expires_at: "2000-01-01" }]) {
    state.asset = { ...good, ...invalid };
    assert.equal(JSON.parse((await call("status")).body).status, "preparing");
  }
  state.asset = good;
  assert.equal(JSON.parse((await call("status")).body).status, "ready");
});

for (const sameEdition of [false, true]) {
  test(sameEdition ? "simultaneous visitors join the winning ticket" : "other editions can update Git without repeating store lookups", async t => {
    let lookups = 0, writes = 0, dispatches = 0, competing = null;
    const play = require("../api/_shared/googleplay");
    t.mock.method(play, "resolveLatestPlayListing", async () => {
      lookups++;
      return { source: "google-play", versionName: "99.0", updatedAt: 123 };
    });
    t.mock.method(github, "config", () => ({ githubOwner: "Jordan231111", githubRepo: "ae-patch-verifier", githubRef: "main" }));
    t.mock.method(github, "githubJson", async (_, method, path, body) => {
      if (method === "GET") return { encoding: "base64", content: Buffer.from(JSON.stringify(
        path.includes("requests/") && competing ? competing : {})).toString("base64") };
      if (method === "PUT") {
        writes++;
        if (writes <= 3) {
          if (sameEdition) competing = JSON.parse(Buffer.from(body.content, "base64").toString());
          throw Object.assign(new Error("Concurrent Git update"), { status: 409 });
        }
        return { content: { sha: "revision" } };
      }
      assert.equal(method, "POST");
      dispatches++;
      return {};
    });
    delete require.cache[require.resolve("../server/catalog/requests")];
    const { build } = require("../server/catalog/requests");
    const res = { setHeader() {}, end(text) { this.body = JSON.parse(text); } };
    await build({ method: "POST", url: "/api/catalog/build", headers: { "content-type": "application/json" },
      body: { variant: variant.id } }, res);
    assert.equal(res.statusCode, 202);
    assert.equal(res.body.status, "preparing");
    assert.equal(lookups, 1);
    assert.equal(dispatches, sameEdition ? 0 : 1);
    assert.equal(writes, sameEdition ? 1 : 4);
  });
}
