#!/usr/bin/env node
// Static smoke checks for the Caliza companion panel. No network or package install required.
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { join } from "node:path";
import { createHarnessServer } from "./mock-server.mjs";

const root = fileURLToPath(new URL(".", import.meta.url));
const required = [
  "index.html", "caliza.css", "app.js", "mock-server.mjs",
  "assets/caliza-mark.svg", "assets/caliza-mark-mono.svg", "assets/strata-ornament.svg",
  "assets/fonts/source-sans-3-latin.woff2", "assets/fonts/source-sans-3-latin-ext.woff2", "assets/fonts/LICENSE.md",
];
const failures = [];
const check = (ok, message) => { if (!ok) failures.push(message); };

for (const file of required) check(existsSync(join(root, file)), `missing ${file}`);
const [html, css, js] = await Promise.all(["index.html", "caliza.css", "app.js"].map((f) => readFile(join(root, f), "utf8")));

check(html.includes('href="caliza.css"'), "index does not load caliza.css");
check(html.includes('src="app.js"'), "index does not load app.js");
check(html.includes("viewport-fit=cover"), "viewport lacks safe-area support");
const legacyProduct = ["Ze", "ru", "ko"].join("");
check(!(new RegExp(legacyProduct, "i")).test(html), "index contains a visible legacy product string");
check(!/ps2ui/i.test(html + css + js), "PS2UI reference found in panel files");
check(!/https?:\/\//i.test(html + css), "production HTML/CSS references an external network resource");
check((js.match(/\/api\/state/g) || []).length > 0, "app does not call /api/state");
const endpoints = ["/api/probe", "/api/games", "/api/cover", "/api/settings", "/api/report", "/api/discs", "/api/disc", "/api/note", "/api/memcards", "/api/achievements", "/api/achievement-badge"];
for (const endpoint of endpoints) {
  check(js.includes(endpoint), `app missing endpoint ${endpoint}`);
}
for (const feature of ["getGamepads", "ArrowLeft", "prefers-reduced-motion", "forced-colors", ":focus-visible"]) {
  check((js + css).includes(feature), `accessibility feature missing: ${feature}`);
}
check(css.split("{").length === css.split("}").length, "CSS braces are unbalanced");
check(js.includes("CALIZA_TEST_MODE"), "app lacks explicit test-mode isolation");
check(js.includes('"Caliza/"'), "canonical settings key prefix construction missing");

const syntax = spawnSync(process.execPath, ["--check", join(root, "app.js")], { encoding: "utf8" });
check(syntax.status === 0, "app.js syntax check failed: " + syntax.stderr);

for (const svgFile of ["assets/caliza-mark.svg", "assets/caliza-mark-mono.svg", "assets/strata-ornament.svg"]) {
  const svg = await readFile(join(root, svgFile), "utf8");
  check(svg.trim().startsWith("<svg") && svg.includes("</svg>"), `${svgFile} is not a complete SVG`);
  check((svg.match(/<path/g) || []).length >= 3, `${svgFile} does not contain the expected vector geometry`);
}

if (!failures.length) {
  const server = createHarnessServer();
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const port = server.address().port;
  try {
    const [page, state, games, settings, memcards, discs, probe] = await Promise.all([
      fetch(`http://127.0.0.1:${port}/`).then((r) => r.text()),
      fetch(`http://127.0.0.1:${port}/api/state`).then((r) => r.json()),
      fetch(`http://127.0.0.1:${port}/api/games`).then((r) => r.json()),
      fetch(`http://127.0.0.1:${port}/api/settings?id=%40global`).then((r) => r.json()),
      fetch(`http://127.0.0.1:${port}/api/memcards`).then((r) => r.json()),
      fetch(`http://127.0.0.1:${port}/api/discs`).then((r) => r.json()),
      fetch(`http://127.0.0.1:${port}/api/probe`).then((r) => r.json()),
    ]);
    check(page.includes("Caliza PS4 Companion"), "harness did not serve index.html");
    check(state.mode === "game" && state.playing, "harness state fixture malformed");
    check(Array.isArray(games) && games.length >= 3, "harness games fixture malformed");
    check(settings.id === "@global" && settings.values, "harness settings fixture malformed");
    check(Array.isArray(memcards.cards) && Array.isArray(memcards.sizes), "harness memory-card fixture malformed");
    check(Array.isArray(discs.set) && Array.isArray(discs.games), "harness disc fixture malformed");
    check(typeof probe.frames === "number", "harness probe fixture malformed");
  } catch (error) {
    failures.push("harness request failed: " + error.message);
  } finally {
    await new Promise((resolve) => server.close(resolve));
  }
}

if (failures.length) {
  console.error("Caliza web smoke test failed:");
  for (const failure of failures) console.error(" - " + failure);
  process.exit(1);
}
console.log(`Caliza web smoke test passed (${required.length} files, ${endpoints.length} endpoint checks, harness round trip).`);
