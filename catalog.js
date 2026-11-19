(() => {
  "use strict";

  const byId = id => document.getElementById(id);
  const list = byId("app-list");
  const count = byId("catalog-count");
  const message = byId("catalog-message");
  const search = byId("app-search");
  const sort = byId("app-sort");
  const clear = byId("clear-search");
  const guide = byId("install-guide");
  const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: "base" });
  const dateFormat = new Intl.DateTimeFormat(undefined, { dateStyle: "medium" });
  const tiles = {
    allrelic: ["AR", 1], anothereden: ["AE", 2], epicconquest: ["EC", 3],
    onceworld: ["OW", 4], relevel: ["RL", 5], rustaxion: ["RA", 6]
  };
  const pendingStates = new Set(["preparing", "queued", "pending", "running", "in_progress", "building"]);
  const maxBuildWait = 12 * 60 * 1000;
  let entries = [];
  let loaded = false;

  function element(tag, className, value) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (value != null) node.textContent = value;
    return node;
  }

  function icon(name) {
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    const use = document.createElementNS(svg.namespaceURI, "use");
    svg.setAttribute("class", "icon" + (name === "chevron" ? " chevron" : ""));
    svg.setAttribute("aria-hidden", "true");
    use.setAttribute("href", "#icon-" + name);
    svg.append(use);
    return svg;
  }

  // Hosted files come from a workshop route or a GitHub release download.
  function linkUrl(value, localOnly = false) {
    if (typeof value !== "string" || !value.trim()) return null;
    try {
      const url = new URL(value, location.href);
      if (url.username || url.password || !["http:", "https:"].includes(url.protocol)) return null;
      if (url.origin === location.origin) return url.href;
      if (!localOnly && url.origin === "https://github.com" &&
          /^\/[^/]+\/[^/]+\/releases\/download\/.+\/.+$/.test(url.pathname)) return url.href;
    } catch {
      // An incomplete record must not become a clickable download.
    }
    return null;
  }

  async function json(url, timeout, options = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);
    try {
      const response = await fetch(url, {
        ...options, headers: { accept: "application/json", ...options.headers }, signal: controller.signal
      });
      if (!response.ok) {
        const error = new Error("Release metadata is unavailable");
        error.status = response.status;
        error.reply = await response.json().catch(() => null);
        throw error;
      }
      return await response.json();
    } finally {
      clearTimeout(timer);
    }
  }

  function validatedApps(data) {
    const apps = data?.apps;
    if (!Array.isArray(apps)) throw new Error("Invalid catalog");
    const ids = new Set();
    const requestIds = new Set();
    for (const app of apps) {
      if (!app || typeof app.name !== "string" || !app.name.trim() ||
          typeof app.id !== "string" || !/^[a-z0-9-]+$/.test(app.id) || ids.has(app.id) ||
          !Array.isArray(app.variants) || !app.variants.length ||
          app.variants.some(v => !v || typeof v.id !== "string" || !v.id)) throw new Error("Invalid app record");
      ids.add(app.id);
      if (!app.statusUrl) for (const variant of app.variants) {
        if (requestIds.has(variant.id)) throw new Error("Duplicate edition");
        requestIds.add(variant.id);
      }
    }
    return apps;
  }

  function versionLabel(value) {
    if (value == null || String(value).trim() === "") return "Version unavailable";
    return "v" + String(value).trim().replace(/^v(?=\d)/i, "");
  }

  function fileSize(value) {
    const size = Number(value);
    if (!Number.isFinite(size) || size <= 0) return "";
    if (size >= 1e9) return (size / 1e9).toLocaleString(undefined, { maximumFractionDigits: 2 }) + " GB";
    if (size >= 1e6) return (size / 1e6).toLocaleString(undefined, { maximumFractionDigits: 1 }) + " MB";
    return Math.ceil(size / 1e3).toLocaleString() + " kB";
  }

  function normalized(value) {
    return value.normalize("NFKD").replace(/\p{M}/gu, "").toLowerCase().replace(/[^\p{L}\p{N}]/gu, "");
  }

  function architecture(value) {
    return value === "arm64-v8a" ? "ARM64" : value;
  }

  function notice(label, buttonText, callback) {
    message.replaceChildren(element("p", "", label));
    if (callback) {
      const button = element("button", "text-button", buttonText);
      button.type = "button";
      button.addEventListener("click", callback);
      message.append(button);
    }
    message.hidden = false;
  }

  function resetSearch() {
    search.value = "";
    search.focus();
    filterList(true);
  }

  function filterList(showFirst = false) {
    const query = normalized(search.value.trim());
    clear.hidden = !search.value;
    byId("search-key").hidden = !clear.hidden;
    if (!loaded) return;

    let visible = 0;
    for (const entry of entries) {
      entry.row.hidden = !entry.searchName.includes(query);
      if (!entry.row.hidden) visible++;
    }
    const noun = entries.length === 1 ? "app" : "apps";
    count.textContent = query ? visible + " of " + entries.length + " " + noun : entries.length + " " + noun;
    byId("catalog-head").hidden = !visible;
    message.hidden = !!visible;
    if (!visible && query) notice("No matching apps", "Clear search", resetSearch);
    if (!entries.length && !query) notice("No releases are available yet.");
    if (showFirst && count.getBoundingClientRect().bottom < 0) count.scrollIntoView({ block: "start" });
  }

  function sortList() {
    const result = entries.slice().sort((a, b) => {
      if (sort.value === "recent" && a.updated !== b.updated) return b.updated - a.updated;
      const order = collator.compare(a.app.name, b.app.name);
      return sort.value === "za" ? -order : order;
    });
    list.append(...result.map(item => item.row));
  }

  function action(tag, label, glyph, style) {
    const node = element(tag, "action " + style);
    if (tag === "button") node.type = "button";
    node.append(icon(glyph), element("span", "action-label", label));
    return node;
  }

  function addDetail(parent, label, value) {
    if (!value) return;
    const item = element("div", "detail-item");
    item.append(element("span", "detail-label", label), element("span", "", value));
    parent.append(item);
  }

  function fileUrl(value) {
    const href = linkUrl(value);
    if (!href) return null;
    const url = new URL(href);
    if (url.origin === location.origin) {
      return /\.apks$/i.test(url.pathname) || /^\/api\/[a-z0-9-]+\/download\/?$/.test(url.pathname) ? href : null;
    }
    return /^\/Jordan231111\/ae-patch-verifier\/releases\/download\/[^/]+\/[^/]+\.apks$/i.test(url.pathname) ? href : null;
  }

  function statusUrl(value, variantId) {
    const href = linkUrl(value, true);
    if (!href) return null;
    const url = new URL(href);
    const variant = url.searchParams.get("variant");
    return url.pathname === "/api/catalog/status" && !url.hash && (!variant || variant === variantId) ? href : null;
  }

  function observedVersion(reply) {
    const value = reply?.version ?? reply?.versionName ?? reply?.latest?.version ?? reply?.latest?.versionName;
    if (typeof value === "number" && Number.isFinite(value)) return String(value);
    return typeof value === "string" && value.trim() ? value.trim() : null;
  }

  function jobActive(job) {
    return job?.phase === "checking" || job?.phase === "preparing";
  }

  function buildNotice(job) {
    if (job.phase === "checking") return "Checking the current app version…";
    if (job.phase === "failed") return job.message;
    const version = job.version ? " " + versionLabel(job.version) : "";
    if (job.phase === "ready") return "Bundle" + version + " is ready.";
    return "Preparing" + version + " for download…";
  }

  function renderBuildAction(entry, job, accessibleName) {
    const { fields } = entry;
    const busy = jobActive(job);
    const retry = job?.phase === "failed";
    const button = fields.buildButton;
    const label = busy ? (job.phase === "checking" ? "Checking…" : "Preparing…") : retry ? "Retry" : "Download";
    button.className = "action " + (busy || retry ? "secondary" : "download");
    button.setAttribute("aria-label", busy ? label + " " + entry.app.name :
      (retry ? "Retry download for " + entry.app.name : accessibleName));
    button.setAttribute("aria-disabled", String(busy));
    button.querySelector(".action-label").textContent = label;
    button.querySelector("use").setAttribute("href", "#icon-" + (busy || retry ? "retry" : "download"));
    if (fields.actions.firstChild !== button) fields.actions.replaceChildren(button);
    fields.feedback.hidden = !job;
    if (!job) {
      fields.note.textContent = "";
      fields.file.removeAttribute("href");
      return;
    }
    const note = buildNotice(job);
    if (fields.note.textContent !== note) fields.note.textContent = note;
    fields.feedback.dataset.phase = job.phase;
    fields.progress.hidden = !busy;
    fields.file.hidden = job.phase !== "ready";
    if (!fields.file.hidden) {
      fields.file.href = job.downloadUrl;
      fields.file.setAttribute("aria-label", "Open verified file for " + entry.app.name + " " + versionLabel(job.version) +
        (entry.app.variants.length > 1 ? ", " + (entry.variant.label || entry.variant.id) : ""));
    } else {
      fields.file.removeAttribute("href");
    }
  }

  function updateRelease(entry) {
    const { app, variant, live, fields } = entry;
    const data = live?.data;
    const job = entry.jobs.get(variant.id);
    const current = data?.versionName || job?.version || variant.version;
    const currentVersion = versionLabel(current);
    const sameVersion = String(current) === String(variant.version) &&
      (job?.versionCode == null || String(job.versionCode) === String(variant.versionCode));
    const size = fileSize(live ? data?.sizeBytes : sameVersion ? variant.sizeBytes : null);
    const arch = architecture(data?.architecture || variant.architecture);
    const url = data?.status === "ready" && linkUrl(data.downloadUrl, true);
    const workshop = linkUrl(variant.workshopUrl || app.workshopUrl, true);
    const statusLabels = {
      checking: "Checking for updates", preparing: "Preparing release",
      requires_review: "Release under review", retry_pending: "Verification pending"
    };
    const edition = variant.label || variant.id;
    const fileFormat = ".apks";
    fields.version.textContent = currentVersion;
    fields.size.textContent = size || "—";
    fields.arch.textContent = arch ? arch + " · " + fileFormat : fileFormat;
    fields.mobile.textContent = size ? currentVersion + " · " + size : currentVersion;
    fields.edition.textContent = app.variants.length > 1 ? edition : "";
    fields.edition.hidden = !fields.edition.textContent;
    fields.availability.hidden = !live || live.loading || !!url;
    fields.availability.textContent = fields.availability.hidden ? "" :
      statusLabels[data?.status] || "Download unavailable";
    const moduleVersion = sameVersion ? variant.moduleVersion : null;
    fields.module.textContent = moduleVersion ? "Module " + moduleVersion : "";
    fields.module.hidden = !fields.module.textContent;
    fields.detailsInfo.replaceChildren();
    addDetail(fields.detailsInfo, "Format", fileFormat);
    addDetail(fields.detailsInfo, "Architecture", arch);
    addDetail(fields.detailsInfo, "Module version", moduleVersion);
    const updated = Date.parse(data?.preparedAt || (sameVersion ? variant.updatedAt : null));
    if (Number.isFinite(updated)) addDetail(fields.detailsInfo, "Released", dateFormat.format(updated));
    fields.builder.hidden = !workshop;
    if (workshop) fields.builder.href = workshop;

    const accessibleName = "Download " + app.name + " " + currentVersion + (app.variants.length > 1 ? ", " + edition : "");
    if (!live) {
      renderBuildAction(entry, job, accessibleName);
      return;
    }
    let control;
    if (url) {
      control = action("a", "Download", "download", "download");
      control.href = url;
      control.setAttribute("aria-label", accessibleName);
    } else if (live?.loading) {
      control = element("span", "status-text", "Checking…");
    } else {
      control = action("button", "Check again", "retry", "secondary");
      control.setAttribute("aria-label", "Check " + app.name + " availability again");
      control.addEventListener("click", () => checkLive(entry, true));
    }
    fields.actions.replaceChildren(control);
  }

  function recordReady(variant, reply, job) {
    const version = observedVersion(reply);
    const downloadUrl = fileUrl(reply.downloadUrl);
    if (!version || !Number.isSafeInteger(reply.sizeBytes) || reply.sizeBytes < 1 || !downloadUrl) {
      throw new Error("Incomplete file record");
    }
    if (version !== String(variant.version) ||
        (reply.versionCode != null && String(reply.versionCode) !== String(variant.versionCode)) ||
        (reply.moduleSha && reply.moduleSha !== variant.moduleSha)) {
      delete variant.updatedAt;
      delete variant.moduleVersion;
      delete variant.versionCode;
    }
    Object.assign(variant, { version, sizeBytes: reply.sizeBytes });
    for (const key of ["versionCode", "moduleVersion", "moduleSha", "updatedAt"]) {
      if (reply[key] != null) variant[key] = reply[key];
    }
    Object.assign(job, { phase: "ready", version, downloadUrl });
  }

  function failureMessage(error) {
    if (error.status === 409 || error.reply?.status === "changed") return "The release changed. Retry to check the new version.";
    if (error.status === 429) return "The builder is busy. Retry shortly.";
    if (error.name === "AbortError" || error.name === "TimeoutError") return "The request timed out. Retry to check the release.";
    return "The bundle could not be prepared. Retry to check the release.";
  }

  async function prepareDownload(entry) {
    const { variant, jobs } = entry;
    if (jobActive(jobs.get(variant.id))) return;
    const job = { phase: "checking", autoDownload: true };
    jobs.set(variant.id, job);
    const render = () => { if (entry.variant === variant) updateRelease(entry); };
    render();
    try {
      const deadline = Date.now() + maxBuildWait;
      let reply = await json("/api/catalog/build", 55000, {
        method: "POST", cache: "no-store", redirect: "error",
        headers: { "content-type": "application/json" }, body: JSON.stringify({ variant: variant.id })
      });
      let pollUrl;
      let transientFailures = 0;
      while (true) {
        const echoed = reply?.variantId ?? reply?.variant;
        if (!reply || (typeof echoed === "string" && echoed !== variant.id)) throw new Error("Unexpected edition");
        const version = observedVersion(reply);
        if ((job.version && version && version !== job.version) ||
            (job.versionCode != null && reply.versionCode != null && String(job.versionCode) !== String(reply.versionCode))) {
          throw Object.assign(new Error("Version changed during preparation"), { status: 409 });
        }
        if (version) job.version = version;
        if (reply.versionCode != null) job.versionCode = reply.versionCode;
        if (reply.status === "ready") {
          recordReady(variant, reply, job);
          render();
          if (entry.variant === variant && job.autoDownload) {
            try {
              window.location.assign(job.downloadUrl);
            } catch {
              // The visible file link can complete the handoff on a new user gesture.
            }
          }
          return;
        }
        if (!pendingStates.has(reply.status)) throw Object.assign(new Error("Build ended"), { reply });
        if (!pollUrl) pollUrl = statusUrl(reply.statusUrl, variant.id);
        if (!pollUrl) throw new Error("No status for this request");
        job.phase = "preparing";
        render();
        const delay = Math.min(3000 + transientFailures * 1500, deadline - Date.now());
        if (delay <= 0) throw new DOMException("Build timed out", "TimeoutError");
        await new Promise(resolve => setTimeout(resolve, delay));
        const remaining = deadline - Date.now();
        if (remaining <= 0) throw new DOMException("Build timed out", "TimeoutError");
        try {
          reply = await json(pollUrl, Math.min(12000, remaining), { cache: "no-store", redirect: "error" });
          transientFailures = 0;
        } catch (error) {
          const transient = !error.status || [408, 429, 502, 503, 504].includes(error.status);
          if (!transient || error.reply?.status === "failed" || ++transientFailures > 2) throw error;
        }
      }
    } catch (error) {
      const version = observedVersion(error.reply);
      if (version) job.version = version;
      const code = error.reply?.versionCode ?? error.reply?.latest?.versionCode;
      if (code != null) job.versionCode = code;
      job.phase = "failed";
      job.message = failureMessage(error);
      render();
    }
  }

  async function checkLive(entry, restoreFocus = false) {
    if (entry.live.loading) return;
    entry.live.loading = true;
    entry.live.data = null;
    updateRelease(entry);
    try {
      const url = linkUrl(entry.app.statusUrl, true);
      if (url) entry.live.data = await json(url, 8000);
    } catch {
      // The card remains usable for checking the release again.
    } finally {
      entry.live.loading = false;
      updateRelease(entry);
      if (restoreFocus && document.activeElement === document.body) {
        entry.fields.actions.querySelector("a, button")?.focus();
      }
    }
  }

  function createEntry(app) {
    const row = element("li", "app-row");
    const main = element("div", "app-main row-grid");
    const title = element("h2");
    const toggle = element("button", "info-switch");
    const personalTile = Object.hasOwn(tiles, app.id) ? tiles[app.id] : null;
    const words = app.name.replace(/(\p{Ll})(\p{Lu})/gu, "$1 $2").match(/[\p{L}\p{N}]+/gu) || [];
    const initials = words.map(word => Array.from(word)[0]).join("").toUpperCase();
    const tile = element("span", "tile", personalTile?.[0] || initials.slice(0, 2) || app.name.slice(0, 2));
    tile.dataset.tone = String(personalTile?.[1] || (app.id.codePointAt(0) % 6 + 1));
    tile.setAttribute("aria-hidden", "true");
    const identity = element("span", "identity-copy");
    const name = element("span", "app-name", app.name);
    name.append(icon("chevron"));
    const fields = {
      mobile: element("span", "mobile-spec summary-line"),
      edition: element("span", "summary-line"), availability: element("span", "summary-line"),
      version: element("span", "version"), module: element("span", "cell-sub"),
      size: element("span", "size"), arch: element("span", "cell-sub"),
      actions: element("div", "action-cell"), detailsInfo: element("div", "details-info"),
      builder: element("a", "detail-link", "Open builder")
    };
    if (app.statusUrl) {
      fields.actions.setAttribute("aria-live", "polite");
      fields.actions.setAttribute("aria-atomic", "true");
    }
    identity.append(name, fields.mobile, fields.edition, fields.availability);
    toggle.type = "button";
    toggle.setAttribute("aria-label", app.name + (app.variants.length > 1 ? ", editions and release details" : ", release details"));
    toggle.setAttribute("aria-expanded", "false");
    toggle.setAttribute("aria-controls", "release-" + app.id);
    toggle.append(tile, identity);
    title.append(toggle);
    const versionCell = element("div", "version-cell");
    versionCell.append(fields.version, fields.module);
    const bundleCell = element("div", "bundle-cell");
    bundleCell.append(fields.size, fields.arch);
    main.append(title, versionCell, bundleCell, fields.actions);
    const details = element("div", "release-details");
    details.id = "release-" + app.id;
    details.hidden = true;
    const entry = {
      app, row, fields, variant: app.variants[0], jobs: new Map(), live: app.statusUrl ? { loading: false, data: null } : null,
      updated: Math.max(0, ...app.variants.map(v => Date.parse(v.updatedAt) || 0)),
      searchName: [app.name, app.id, initials, tile.textContent].map(normalized).join(" ")
    };
    if (app.variants.length > 1) {
      const label = element("label", "edition");
      const container = element("span", "edition-control");
      const select = element("select");
      select.setAttribute("aria-label", "Edition for " + app.name);
      app.variants.forEach((v, index) => {
        const option = element("option", "", v.label || v.id);
        option.value = String(index);
        select.append(option);
      });
      select.addEventListener("change", () => {
        const previous = entry.jobs.get(entry.variant.id);
        if (previous) previous.autoDownload = false;
        entry.variant = app.variants[select.selectedIndex];
        updateRelease(entry);
      });
      container.append(select, icon("chevron"));
      label.append(element("span", "detail-label", "Edition"), container);
      details.append(label);
    }
    details.append(fields.detailsInfo, fields.builder);
    row.append(main);
    if (!entry.live) {
      fields.buildButton = action("button", "Download", "download", "download");
      fields.buildButton.addEventListener("click", () => prepareDownload(entry));
      fields.feedback = element("div", "request-feedback");
      fields.note = element("span", "request-note");
      fields.note.id = "download-state-" + app.id;
      fields.buildButton.setAttribute("aria-describedby", fields.note.id);
      fields.note.setAttribute("role", "status");
      fields.note.setAttribute("aria-live", "polite");
      fields.progress = element("progress", "request-progress");
      fields.progress.setAttribute("aria-label", "Download preparation");
      fields.file = element("a", "detail-link file-link", "Open file");
      fields.feedback.append(fields.note, fields.progress, fields.file);
      row.append(fields.feedback);
    }
    row.append(details);
    toggle.addEventListener("click", () => {
      details.hidden = !details.hidden;
      toggle.setAttribute("aria-expanded", String(!details.hidden));
    });
    updateRelease(entry);
    return entry;
  }

  async function loadCatalog() {
    loaded = false;
    count.textContent = "";
    list.setAttribute("aria-busy", "true");
    notice("Loading app library…");
    try {
      let apps;
      try {
        apps = validatedApps(await json("/api/catalog", 3500));
      } catch {
        apps = validatedApps(await json("/catalog.json", 6000));
      }
      entries = apps.map(createEntry);
      list.replaceChildren();
      sortList();
      loaded = true;
      filterList();
      for (const entry of entries) if (entry.live) void checkLive(entry);
    } catch {
      notice("The app library could not be loaded.", "Try again", loadCatalog);
    } finally {
      list.setAttribute("aria-busy", "false");
    }
  }

  search.addEventListener("input", () => filterList(true));
  byId("catalog-search").addEventListener("submit", event => {
    event.preventDefault();
    search.blur();
    filterList(true);
  });
  clear.addEventListener("click", resetSearch);
  sort.addEventListener("change", sortList);
  document.addEventListener("keydown", event => {
    if (guide.open || event.altKey || event.ctrlKey || event.metaKey || event.isComposing) return;
    if (event.key === "Escape" && event.target === search && search.value) {
      event.preventDefault();
      resetSearch();
    }
    if (event.key === "/" && !["INPUT", "TEXTAREA", "SELECT"].includes(event.target.tagName)) {
      event.preventDefault();
      search.focus();
    }
  });
  byId("open-guide").addEventListener("click", () => guide.showModal());
  guide.addEventListener("click", event => { if (event.target === guide) guide.close(); });
  void loadCatalog();
})();
