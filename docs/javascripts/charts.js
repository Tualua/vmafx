/*
 * Copyright 2026 Lusoris
 * SPDX-License-Identifier: EUPL-1.2
 *
 * Interactive documentation charts (ADR-1508).
 *
 * A chart page holds a <figure class="vx-chart"> with two static SVGs that
 * scripts/docs/generate-charts.py rendered (light and dark) and the data table
 * below it. On such a page this script loads the vendored Vega bundle, which no
 * other page loads, and replaces the static image with a vega-embed view that
 * shows a tooltip with exact values on hover. It re-renders when the reader
 * toggles the colour scheme, and runs again after Material's instant
 * navigation. Without JavaScript the static image and the table remain.
 *
 * The spec, the rows and the colours come from the files the generator wrote
 * next to the static images: docs/charts/<slug>/spec.vl.json and data.json,
 * and docs/assets/charts/theme.json.
 */
(function () {
  "use strict";

  var TOKEN = "@vx/";
  var bundle = null;
  var themeCache = null;

  function siteRoot(img) {
    var src = img.currentSrc || img.src;
    var at = src.lastIndexOf("/assets/charts/");
    return at < 0 ? null : src.slice(0, at + 1);
  }

  function slugOf(img) {
    var file = (img.currentSrc || img.src).split("#")[0].split("/").pop();
    return file.replace(/\.(light|dark)\.svg$/, "");
  }

  function loadBundle(root) {
    if (window.vegaEmbed) {
      return Promise.resolve();
    }
    if (!bundle) {
      bundle = new Promise(function (resolve, reject) {
        var script = document.createElement("script");
        script.type = "module";
        script.src = root + "javascripts/vendor/vega/vega-bundle.js";
        script.onload = resolve;
        script.onerror = reject;
        document.head.appendChild(script);
      });
    }
    return bundle;
  }

  function fetchJson(url) {
    return fetch(url).then(function (response) {
      if (!response.ok) {
        throw new Error(url + ": " + response.status);
      }
      return response.json();
    });
  }

  function scheme() {
    var value = document.body.getAttribute("data-md-color-scheme");
    return value === "slate" ? "dark" : "light";
  }

  /* Replace "@vx/<name>" strings with the scheme's colours, iteratively. */
  function substitute(spec, tokens) {
    var stack = [spec];
    while (stack.length) {
      var node = stack.pop();
      var keys = Object.keys(node);
      for (var i = 0; i < keys.length; i++) {
        var value = node[keys[i]];
        if (typeof value === "string" && value.indexOf(TOKEN) === 0) {
          node[keys[i]] = tokens[value.slice(TOKEN.length)];
        } else if (value && typeof value === "object") {
          stack.push(value);
        }
      }
    }
    return spec;
  }

  function mount(figure) {
    var img = figure.querySelector("img.vx-chart__static");
    var root = img ? siteRoot(img) : null;
    if (!root) {
      return Promise.resolve();
    }
    var slug = slugOf(img);
    var base = root + "charts/" + slug + "/";
    themeCache = themeCache || fetchJson(root + "assets/charts/theme.json");
    return Promise.all([
      fetchJson(base + "spec.vl.json"),
      fetchJson(base + "data.json"),
      themeCache,
      loadBundle(root),
    ]).then(function (parts) {
      var theme = parts[2][scheme()];
      var spec = substitute(parts[0], theme.tokens);
      spec.data = { values: parts[1] };
      spec.description = img.alt;
      if (typeof spec.width === "number") {
        spec.width = "container";
        spec.autosize = { type: "fit-x", contains: "padding" };
      }
      var view = figure.querySelector(".vx-chart__live");
      if (!view) {
        view = document.createElement("div");
        view.className = "vx-chart__live";
        figure.insertBefore(view, figure.querySelector("figcaption"));
      }
      return window.vegaEmbed(view, spec, {
        actions: false,
        renderer: "svg",
        config: theme.config,
        tooltip: { theme: scheme() === "dark" ? "dark" : "light" },
        logLevel: 1,
      }).then(function () {
        var svg = view.querySelector("svg");
        if (svg) {
          svg.setAttribute("role", "img");
          svg.setAttribute("aria-label", img.alt);
        }
        figure.classList.add("vx-chart--live");
      });
    }).catch(function (error) {
      figure.classList.remove("vx-chart--live");
      if (window.console) {
        window.console.warn("vx-chart: static image kept:", error);
      }
    });
  }

  function mountAll() {
    var figures = document.querySelectorAll("figure.vx-chart");
    for (var i = 0; i < figures.length; i++) {
      mount(figures[i]);
    }
  }

  var observer = new MutationObserver(function () {
    if (document.querySelector("figure.vx-chart--live")) {
      mountAll();
    }
  });
  observer.observe(document.body, { attributes: true, attributeFilter: ["data-md-color-scheme"] });

  if (window.document$ && typeof window.document$.subscribe === "function") {
    window.document$.subscribe(mountAll);
  } else if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", mountAll);
  } else {
    mountAll();
  }
})();
