// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

const assert = require("node:assert/strict");
const stats = require("../site/download-stats.js");

const releases = [{
  draft: false, prerelease: false,
  assets: [
    { id: 1, name: "GalleonCore-0.1.0-Windows-x86_64-Setup.exe", download_count: 12 },
    { id: 2, name: "galleoncore_0.1.0-2.noble_amd64.deb", download_count: 7 },
    { id: 3, name: "galleoncore-0.1.0-1.fc44.x86_64.rpm", download_count: 5 },
    { id: 4, name: "galleoncore-0.1.0-1-x86_64.pkg.tar.zst", download_count: 3 },
    { id: 5, name: "GalleonCore-0.1.0-x86_64.AppImage", download_count: 2 },
    { id: 6, name: "galleoncore-0.1.0-1.fc44.src.rpm", download_count: 20 },
    { id: 7, name: "SHA256SUMS", download_count: 30 }
  ]
}, {
  draft: true, assets: [{ id: 8, name: "draft.AppImage", download_count: 100 }]
}];

const assets = stats.assetsFromReleases(releases);
assert.deepEqual(stats.totals(assets), {
  windows: 12, ubuntu: 7, fedora: 5, arch: 3, appimage: 2
});
assert.deepEqual(stats.sevenDayDeltas(assets, {
  "1": { count: 10, platform: "windows" },
  "2": { count: 9, platform: "ubuntu" }
}), { windows: 2, ubuntu: 0, fedora: 5, arch: 3, appimage: 2 });
assert.equal(stats.platformFor("galleoncore-debuginfo-0.1.0-1.fc44.x86_64.rpm"), null);
assert.equal(stats.approximateRecent(
  { windows: 2, ubuntu: 0, fedora: 5, arch: 3, appimage: 2 },
  { ppa: 7, copr: 8 }), 27);
assert.equal(stats.approximateRecent(
  { windows: 2, ubuntu: 0, fedora: 5, arch: 3, appimage: 2 },
  { ppa: 7, copr: null }), null);
assert.equal(stats.approximateRecent(null, { ppa: 7, copr: 8 }), null);
console.log("Live download classification, deltas, and approximate totals passed");
