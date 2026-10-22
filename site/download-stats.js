// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

(function () {
  "use strict";

  var DIRECT = ["windows", "ubuntu", "fedora", "arch", "appimage"];
  var API = "https://api.github.com/repos/Paullux/GalleonCore/releases?per_page=100&page=";

  function platformFor(name) {
    var lower = typeof name === "string" ? name.toLowerCase() : "";
    if (lower.endsWith("-setup.exe") || lower.endsWith("-windows-x86_64.zip")) return "windows";
    if (lower.endsWith(".deb") && !lower.endsWith(".ddeb")) return "ubuntu";
    if (lower.endsWith(".rpm") && !lower.includes(".src.") &&
        !lower.includes("-debuginfo-") && !lower.includes("-debugsource-")) return "fedora";
    if (lower.endsWith(".pkg.tar.zst") && !lower.includes("-debug-")) return "arch";
    if (lower.endsWith(".appimage")) return "appimage";
    return null;
  }

  function assetsFromReleases(releases) {
    var assets = {};
    releases.forEach(function (release) {
      if (!release || typeof release !== "object") return;
      if (release.draft || release.prerelease) return;
      (Array.isArray(release.assets) ? release.assets : []).forEach(function (asset) {
        if (!asset || typeof asset !== "object") return;
        var platform = platformFor(asset.name || "");
        if (platform && Number.isSafeInteger(asset.id) &&
            Number.isSafeInteger(asset.download_count) && asset.download_count >= 0) {
          assets[String(asset.id)] = { platform: platform, count: asset.download_count };
        }
      });
    });
    return assets;
  }

  function totals(assets) {
    var result = {};
    DIRECT.forEach(function (name) { result[name] = 0; });
    Object.keys(assets || {}).forEach(function (id) {
      var asset = assets[id];
      if (asset && DIRECT.includes(asset.platform) &&
          Number.isSafeInteger(asset.count) && asset.count >= 0) {
        result[asset.platform] += asset.count;
      }
    });
    return result;
  }

  function sevenDayDeltas(current, baseline) {
    var result = {};
    DIRECT.forEach(function (name) { result[name] = 0; });
    Object.keys(current).forEach(function (id) {
      var item = current[id];
      if (!item || !Number.isSafeInteger(item.count) || item.count < 0) return;
      var old = (baseline || {})[id];
      var previous = old && Number.isSafeInteger(old.count) && old.count >= 0 ? old.count : 0;
      if (DIRECT.includes(item.platform)) result[item.platform] += Math.max(0, item.count - previous);
    });
    return result;
  }

  if (typeof module !== "undefined") {
    module.exports = { platformFor: platformFor, assetsFromReleases: assetsFromReleases,
      totals: totals, sevenDayDeltas: sevenDayDeltas };
  }
  if (typeof document === "undefined") return;

  var published = null;
  var controller = null;
  var note = document.getElementById("download-stats-note");

  function setNumber(selector, value) {
    if (Number.isSafeInteger(value) && value >= 0) {
      document.querySelector(selector).textContent = value.toLocaleString();
    }
  }

  function baselineAssets(stats) {
    if (!stats.since || !Array.isArray(stats.history)) return null;
    var entry = stats.history.find(function (item) { return item.date === stats.since; });
    return entry && entry.assets && typeof entry.assets === "object" ? entry.assets : null;
  }

  function setDirect(assets, baseline, live) {
    var current = totals(assets);
    var weekly = baseline ? sevenDayDeltas(assets, baseline) : null;
    DIRECT.forEach(function (name) {
      setNumber('[data-download-stat="' + name + '"]', current[name]);
      if (weekly) setNumber('[data-download-week="' + name + '"]', weekly[name]);
    });
    if (weekly && published && published.by_channel) {
      var ppa = published.by_channel.ppa;
      var copr = published.by_channel.copr;
      if (Number.isSafeInteger(ppa) && Number.isSafeInteger(copr)) {
        setNumber('[data-download-stat="total"]',
          DIRECT.reduce(function (sum, name) { return sum + weekly[name]; }, ppa + copr));
      }
    }
    document.getElementById("direct-count-source").textContent = live ? "GitHub API" : "Daily snapshot";
  }

  function setNote(stats, live) {
    var checked = stats.repository_checked_at || {};
    var dates = ["ppa", "copr"].map(function (name) {
      var period = (stats.repository_period || {})[name];
      if (period && period.from && period.to) return name.toUpperCase() + " " + period.from + "–" + period.to;
      return name.toUpperCase() + " " + (checked[name] || "—");
    }).join("; ");
    note.innerHTML = '<span lang="en">Direct totals: <span data-source></span> at page load (daily snapshot when optional content is refused). Seven-day changes need a previous snapshot. PPA/COPR are checked once per week; their latest measured periods: <span data-repo-dates></span>. The combined recent total is approximate because these periods can differ. AUR votes are not downloads; downloads include updates, not unique users.</span><span lang="fr">Totaux directs : <span data-source></span> à l’ouverture (relevé quotidien si le contenu facultatif est refusé). Les variations sur sept jours demandent un relevé antérieur. PPA/COPR sont vérifiés une fois par semaine ; dernières périodes mesurées : <span data-repo-dates></span>. Le total récent est approximatif car ces périodes peuvent différer. Les votes AUR ne sont pas des téléchargements ; les comptes incluent les mises à jour, pas des utilisateurs uniques.</span>';
    if (!stats.since) {
      note.insertAdjacentHTML("afterbegin", '<strong lang="en">First daily snapshot recorded. Recent counts will appear after the next daily snapshot. </strong><strong lang="fr">Premier relevé quotidien enregistré. Les mesures récentes apparaîtront après le prochain relevé quotidien. </strong>');
    }
    note.querySelectorAll("[data-source]").forEach(function (span) {
      span.textContent = live ? "GitHub API" : stats.generated_at || "Daily snapshot";
    });
    note.querySelectorAll("[data-repo-dates]").forEach(function (span) { span.textContent = dates; });
  }

  function renderSnapshot(stats) {
    published = stats;
    var history = Array.isArray(stats.history) ? stats.history : [];
    var latest = history[history.length - 1];
    if (latest && latest.assets) setDirect(latest.assets, baselineAssets(stats), false);
    var repositoryWeekly = stats.repository_weekly || stats.by_channel || {};
    ["ppa", "copr"].forEach(function (name) {
      setNumber('[data-download-stat="' + name + '"]', repositoryWeekly[name]);
    });
    if (stats.by_channel) {
      setNumber('[data-download-stat="total"]', stats.total);
    }
    setNumber('[data-download-stat="aur"]', stats.aur_votes);
    setNote(stats, false);
    refreshLive();
  }

  async function fetchLiveReleases(signal) {
    var releases = [];
    for (var page = 1; page <= 10; page++) {
      var response = await fetch(API + page, { cache: "no-store", signal: signal });
      if (!response.ok) throw new Error("GitHub API unavailable");
      var batch = await response.json();
      if (!Array.isArray(batch)) throw new Error("Invalid GitHub API response");
      releases.push.apply(releases, batch);
      if (batch.length < 100) return releases;
    }
    throw new Error("Too many releases for a complete count");
  }

  async function refreshLive() {
    if (!published || document.documentElement.dataset.externalAllowed !== "yes" || controller) return;
    controller = new AbortController();
    var active = controller;
    try {
      var releases = await fetchLiveReleases(active.signal);
      if (document.documentElement.dataset.externalAllowed === "yes") {
        setDirect(assetsFromReleases(releases), baselineAssets(published), true);
        setNote(published, true);
      }
    } catch (error) {
      if (error.name !== "AbortError") setNote(published, false);
    } finally {
      if (controller === active) controller = null;
    }
  }

  document.addEventListener("galleoncore:external-choice", function (event) {
    if (event.detail.enabled) refreshLive();
    else if (controller) controller.abort();
  });

  fetch("./downloads-weekly.json", { cache: "no-store" }).then(function (response) {
    if (!response.ok) throw new Error("Download counts unavailable");
    return response.json();
  }).then(renderSnapshot).catch(function () {
    note.innerHTML = '<span lang="en">Download counts are temporarily unavailable.</span><span lang="fr">Les compteurs de téléchargement sont temporairement indisponibles.</span>';
  });
})();
