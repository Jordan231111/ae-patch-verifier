/* AllRelic's isolated APKPure/prebuilt-module workshop. */
function createAllrelicWorkshop({ readJsonSafe, sameOriginApiUrl, sleep }) {
  const element = name => document.getElementById("allrelic" + name);
  const button = element("Button");
  const stages = Array.from(document.querySelectorAll("#allrelicTimeline .build-stage"));
  let version = null, module = null, busy = false, pending = null, stage = 0;

  function status(text, kind = "info") {
    element("Status").className = "status " + kind;
    element("Status").querySelector(".text").textContent = text;
  }
  function available() { button.disabled = busy || !version || !module; }
  function timeline(state) {
    if (state === "queued") stage = 0;
    if (state === "running") stage = 1;
    if (state === "ready") stage = 2;
    stages.forEach((item, index) => {
      item.classList.remove("active", "done", "error");
      item.removeAttribute("aria-current");
      if (state === "idle") return;
      if (state === "complete" || index < stage) item.classList.add("done");
      else if (index === stage) {
        item.classList.add(state === "error" ? "error" : "active");
        item.setAttribute("aria-current", "step");
      }
    });
  }
  async function json(url, options) {
    const response = await fetch(url, { cache: "no-store", ...options });
    const body = await readJsonSafe(response) || {};
    if (!response.ok) {
      const error = new Error(body.message || "AllRelic request failed (HTTP " + response.status + ")");
      error.status = response.status;
      throw error;
    }
    return body;
  }
  function refresh() {
    if (pending) return pending;
    pending = (async () => {
      const results = await Promise.allSettled([
        json("/api/allrelic/version"), json("/api/allrelic/module/latest")
      ]);
      const game = results[0], mod = results[1];
      version = game.status === "fulfilled" && /^[1-9][0-9]*$/.test(String(game.value.versionCode))
        && typeof game.value.versionName === "string" ? game.value : null;
      module = mod.status === "fulfilled" && mod.value.prebuilt === true
        && /^[0-9a-f]{40}$/.test(mod.value.sha || "") ? mod.value : null;
      element("Version").textContent = version ? "v" + version.versionName : "unavailable";
      element("Version").className = "commit-chip " + (version ? "ok" : "warn");
      element("VersionMeta").textContent = version ? "code " + version.versionCode + " · APKPure · ARM64"
        : game.reason?.message || "Could not resolve the latest APKPure release";
      element("Module").textContent = module ? module.sha.slice(0, 7) : "unavailable";
      element("Module").className = "commit-chip " + (module ? "ok" : "warn");
      element("ModuleMeta").textContent = module ? "Signed ARKit release" : mod.reason?.message || "ARKit prebuilt is not ready";
      available();
      if (!busy) status(version && module ? "Ready to build the latest AllRelic release." : "Release information is unavailable. Select AllRelic again to retry.",
                        version && module ? "ok" : "warn");
      return Boolean(version && module);
    })().finally(() => { pending = null; });
    return pending;
  }
  async function poll(url) {
    const start = Date.now();
    for (;;) {
      const body = await json(url);
      if (body.status === "ready") { timeline("ready"); return body; }
      if (body.status === "failed" || body.status === "error") throw new Error(body.message || "AllRelic packaging failed");
      const elapsed = Math.round((Date.now() - start) / 1000);
      if (elapsed > 660) throw new Error("AllRelic build timed out. Check GitHub Actions before trying again.");
      timeline(body.status === "running" ? "running" : "queued");
      status((body.status === "running" ? "Packaging AllRelic... " : "Waiting for a build runner... ") + elapsed + "s");
      await sleep(4000);
    }
  }
  async function build() {
    if (busy) return;
    busy = true; available();
    status("Checking the latest AllRelic release and ARKit module...");
    try {
      if (!await refresh()) throw new Error("The AllRelic release or ARKit prebuilt is unavailable. Select AllRelic again to retry.");
      timeline("queued");
      const body = await json("/api/allrelic/build", {
        method: "POST", headers: { "content-type": "application/json" },
        body: JSON.stringify({ versionCode: version.versionCode, versionName: version.versionName })
      });
      if (body.mode !== "github" || !/^[0-9]{13}-[0-9a-f]{8}$/.test(body.nonce || "")) throw new Error("Invalid AllRelic build response");
      const endpoint = "/api/allrelic/status?nonce=" + encodeURIComponent(body.nonce);
      const ready = await poll(sameOriginApiUrl(body.statusUrl, endpoint));
      const download = "/api/allrelic/download?nonce=" + encodeURIComponent(body.nonce);
      const signed = await json(sameOriginApiUrl(ready.downloadUrl, download) + "&format=json");
      const url = new URL(signed.url);
      if (url.protocol !== "https:" || !["release-assets.githubusercontent.com", "github.com"].includes(url.hostname)) {
        throw new Error("Invalid GitHub download address");
      }
      const link = document.createElement("a");
      link.href = url.href; link.download = signed.filename || ready.filename; link.rel = "noopener";
      document.body.appendChild(link); link.click(); link.remove();
      timeline("complete");
      status("Download started: " + link.download, "ok");
    } catch (error) {
      if (error.status === 409) await refresh();
      timeline("error"); status(error.message, "fail");
    } finally { busy = false; available(); }
  }
  button.addEventListener("click", build);
  return { refresh };
}
