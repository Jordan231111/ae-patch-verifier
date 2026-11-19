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

  async function json(url, timeout) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);
    try {
      const response = await fetch(url, { headers: { accept: "application/json" }, signal: controller.signal });
      if (!response.ok) throw new Error("Release metadata is unavailable");
      return await response.json();
    } finally {
      clearTimeout(timer);
    }
  }

  function validatedApps(data) {
    const apps = data?.apps;
    if (!Array.isArray(apps)) throw new Error("Invalid catalog");
    const ids = new Set();
    for (const app of apps) {
      if (!app || typeof app.name !== "string" || !app.name.trim() ||
          typeof app.id !== "string" || !/^[a-z0-9-]+$/.test(app.id) || ids.has(app.id) ||
          !Array.isArray(app.variants) || !app.variants.length ||
          app.variants.some(v => !v || typeof v !== "object")) throw new Error("Invalid app record");
      ids.add(app.id);
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

  function updateRelease(entry) {
    const { app, variant, live, fields } = entry;
    const data = live?.data;
    const currentVersion = versionLabel(data?.versionName || variant.version);
    const size = fileSize(live ? data?.sizeBytes : variant.sizeBytes);
    const arch = architecture(data?.architecture || variant.architecture);
    const url = live ? (data?.status === "ready" && linkUrl(data.downloadUrl, true)) : linkUrl(variant.downloadUrl);
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
    fields.module.textContent = variant.moduleVersion ? "Module " + variant.moduleVersion : "";
    fields.module.hidden = !fields.module.textContent;
    fields.detailsInfo.replaceChildren();
    addDetail(fields.detailsInfo, "Format", fileFormat);
    addDetail(fields.detailsInfo, "Architecture", arch);
    addDetail(fields.detailsInfo, "Module version", variant.moduleVersion);
    const updated = Date.parse(data?.preparedAt || variant.updatedAt);
    if (Number.isFinite(updated)) addDetail(fields.detailsInfo, "Released", dateFormat.format(updated));
    fields.builder.hidden = !workshop || (!live && !url);
    if (workshop) fields.builder.href = workshop;

    let control;
    if (url) {
      control = action("a", "Download", "download", "download");
      control.href = url;
      control.setAttribute("aria-label", "Download " + app.name + " " + currentVersion +
        (app.variants.length > 1 ? ", " + edition : ""));
    } else if (live?.loading) {
      control = element("span", "status-text", "Checking…");
    } else if (live) {
      control = action("button", "Check again", "retry", "secondary");
      control.setAttribute("aria-label", "Check " + app.name + " availability again");
      control.addEventListener("click", () => checkLive(entry, true));
    } else if (workshop) {
      control = action("a", "Open builder", "build", "secondary");
      control.href = workshop;
      control.setAttribute("aria-label", "Open builder for " + app.name);
    } else {
      control = element("span", "status-text", "Unavailable");
    }
    fields.actions.replaceChildren(control);
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
      app, row, fields, variant: app.variants[0], live: app.statusUrl ? { loading: false, data: null } : null,
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
        entry.variant = app.variants[select.selectedIndex];
        updateRelease(entry);
      });
      container.append(select, icon("chevron"));
      label.append(element("span", "detail-label", "Edition"), container);
      details.append(label);
    }
    details.append(fields.detailsInfo, fields.builder);
    row.append(main, details);
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
