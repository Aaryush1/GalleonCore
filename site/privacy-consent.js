// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

(function () {
  "use strict";
  var key = "galleoncore-external-v1";
  var root = document.documentElement;
  var choice = null;
  try { choice = localStorage.getItem(key); } catch (error) {}

  if (document.body.classList.contains("legal-page")) {
    var savedLanguage = null;
    try { savedLanguage = localStorage.getItem("galleoncore-lang"); } catch (error) {}
    var language = savedLanguage || ((navigator.language || "en").toLowerCase().startsWith("fr") ? "fr" : "en");
    root.setAttribute("data-lang", language);
    root.setAttribute("lang", language);
    var toggle = document.querySelector("[data-legal-lang-toggle]");
    toggle.addEventListener("click", function () {
      var next = root.getAttribute("data-lang") === "fr" ? "en" : "fr";
      root.setAttribute("data-lang", next);
      root.setAttribute("lang", next);
      try { localStorage.setItem("galleoncore-lang", next); } catch (error) {}
    });
  }

  document.querySelectorAll(".badges img[data-external-src]").forEach(function (image) {
    var fallback = document.createElement("span");
    fallback.className = "badge-fallback";
    fallback.textContent = image.alt;
    image.after(fallback);
  });

  function applyExternal(enabled) {
    document.querySelectorAll("link[data-external-href]").forEach(function (link) {
      if (enabled) {
        link.disabled = false;
        link.href = link.dataset.externalHref;
      } else {
        link.disabled = true;
        link.removeAttribute("href");
      }
    });
    document.querySelectorAll("img[data-external-src]").forEach(function (image) {
      if (enabled) image.src = image.dataset.externalSrc;
      else image.removeAttribute("src");
    });
  }

  var banner = document.createElement("aside");
  banner.className = "privacy-banner";
  banner.setAttribute("aria-label", "Privacy choices / Choix de confidentialité");
  banner.innerHTML = '<p><span lang="en">This site stores your language and privacy choice on this device. Optional fonts and status badges contact Google, GitHub and Shields.io only if you allow them. You can change your choice at any time. <a href="privacy.html">Privacy details</a>.</span><span lang="fr">Ce site mémorise ta langue et ton choix de confidentialité sur cet appareil. Les polices et badges facultatifs contactent Google, GitHub et Shields.io seulement si tu les autorises. Tu peux modifier ton choix à tout moment. <a href="privacy.html">Détails sur la confidentialité</a>.</span></p><div class="privacy-actions"><button type="button" data-choice="no"><span lang="en">Reject optional content</span><span lang="fr">Refuser le contenu facultatif</span></button><button type="button" data-choice="yes"><span lang="en">Allow optional content</span><span lang="fr">Autoriser le contenu facultatif</span></button></div>';
  document.body.append(banner);
  banner.hidden = choice === "yes" || choice === "no";
  applyExternal(choice === "yes");

  banner.querySelectorAll("[data-choice]").forEach(function (button) {
    button.addEventListener("click", function () {
      choice = button.dataset.choice;
      try { localStorage.setItem(key, choice); } catch (error) {}
      applyExternal(choice === "yes");
      banner.hidden = true;
    });
  });
  document.querySelectorAll("[data-open-privacy-choices]").forEach(function (button) {
    button.addEventListener("click", function () {
      banner.hidden = false;
      banner.querySelector("[data-choice]").focus();
    });
  });
})();
