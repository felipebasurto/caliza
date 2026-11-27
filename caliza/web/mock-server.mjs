#!/usr/bin/env node
// Offline Caliza panel harness. It serves the real static files with fixture-only API data.
// Fixture data is explicit test data and is never used by the production panel path.
import http from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL(".", import.meta.url));
const types = new Map([
  [".html", "text/html; charset=utf-8"], [".css", "text/css; charset=utf-8"], [".js", "text/javascript; charset=utf-8"],
  [".mjs", "text/javascript; charset=utf-8"], [".svg", "image/svg+xml"], [".png", "image/png"], [".woff2", "font/woff2"],
]);

const games = [
  { id: "fixture-dustline.iso", title: "Dustline Rally (fixture)", region: "PAL", serial: "FIX-00001", size: 2362234880, badges: ["16:9"], cover: true, settings: true, playing: true },
  { id: "fixture-tidewatch.iso", title: "Tidewatch (fixture)", region: "NTSC-U", serial: "FIX-00002", size: 1825361100, badges: [], cover: true, settings: false, playing: false },
  { id: "fixture-silver-quarry.iso", title: "Silver Quarry (fixture)", region: "NTSC-J", serial: "FIX-00003", size: 4110417920, badges: ["hidden"], cover: false, settings: false, playing: false },
  { id: "fixture-homebrew.elf", title: "Strata Test Homebrew (fixture)", region: "", serial: "", size: 1245184, badges: ["ELF"], cover: false, settings: false, playing: false },
];
const valuesGlobal = {
  "upscale_multiplier": "2",
  "AspectRatio": "Auto 4:3/3:2",
  "EmuCore/EnableFastBoot": "true",
  "MemoryCards/Slot1_Filename": "Mcd001.ps2",
  "MemoryCards/Slot1_Enable": "true",
};
const valuesGame = {
  "upscale_multiplier": "3",
  "EmuCore/EnableWideScreenPatches": "true",
};
const settingsCache = new Map();
function settings(id) {
  if (settingsCache.has(id)) return settingsCache.get(id);
  const game = games.find((g) => g.id === id);
  const isGlobal = id === "@global";
  if (!isGlobal && !game) return null;
  const data = {
    id,
    title: isGlobal ? "All games" : game.title,
    serial: isGlobal ? "" : game.serial,
    file: isGlobal ? "gs.ini" : "settings/" + id.replace(/\.[^.]+$/, "") + ".ini",
    exists: true,
    values: structuredClone(isGlobal ? valuesGlobal : valuesGame),
    global: isGlobal ? {} : structuredClone(valuesGlobal),
    recommended: { found: !isGlobal, values: { upscale_multiplier: "3", "EmuCore/EnableWideScreenPatches": "true" }, patches: [] },
    is_recommended: false,
    patches: isGlobal ? [] : [{ name: "widescreen", file: "FIX-00001.pnach", description: "Fixture widescreen patch", enabled: false }],
    cheats: isGlobal ? [] : [],
    modern_controls: isGlobal
      ? []
      : [{ name: "Modern analog controls", file: "FIX-00001_cafefeed.pnach", description: "Fixture modern-controls patch", enabled: false, kind: "patch" }],
    gamefixes: isGlobal ? undefined : { UserHacks_HalfPixelOffset: "1" },
  };
  settingsCache.set(id, data);
  return data;
}
function coverSvg(id) {
  const game = games.find((g) => g.id === id);
  if (!game || !game.cover) return null;
  const hue = id.includes("tidewatch") ? "#477fd0" : "#b36b3c";
  const title = game.title.replace(" (fixture)", "");
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 500 700"><defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop stop-color="${hue}"/><stop offset="1" stop-color="#111827"/></linearGradient></defs><rect width="500" height="700" fill="url(#g)"/><path d="M0 430 150 250l95 100 110-145 145 225v270H0Z" fill="#080B12" opacity=".58"/><path d="M0 500h500" stroke="#A5E7FF" stroke-width="8" opacity=".55"/><text x="42" y="92" fill="#EEF4FF" font-family="sans-serif" font-size="48" font-weight="800">${title}</text><text x="42" y="138" fill="#A5E7FF" font-family="sans-serif" font-size="24">offline fixture</text></svg>`;
}
const cards = { cards: [{ name: "Mcd001.ps2", bytes: 8650752 }, { name: "RPG saves.ps2", bytes: 17301504 }], sizes: [8, 16, 32, 64], defaults: ["Mcd001.ps2", "Mcd002.ps2"] };
const achievements = {
  game_id: 1001,
  title: "Dustline Rally (fixture)",
  playing: "fixture-dustline.iso",
  busy: false,
  message: "",
  entries: [
    { id: 1, points: 10, unlocked: true, title: "First stage clear", description: "Finish the opening stage." },
    { id: 2, points: 25, unlocked: false, title: "Clean line", description: "Complete a stage without collision." },
    { id: 3, points: 50, unlocked: false, title: "Rally master", description: "Finish the fixture cup." },
  ],
};

function json(res, data, status = 200) {
  res.writeHead(status, { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" });
  res.end(JSON.stringify(data));
}
function notFound(res, message = "not found") { json(res, { error: message }, 404); }
async function bodyText(req) {
  let body = "";
  for await (const part of req) body += part;
  return body;
}
export function createHarnessServer() {
  return http.createServer(async (req, res) => {
    const url = new URL(req.url, "http://harness.local");
    const path = url.pathname;
    if (path === "/api/state") return json(res, { app: "fixture", build: "offline-harness", test: 0, language: "en", strings: {}, mode: "game", playing: { id: games[0].id, title: games[0].title, serial: games[0].serial } });
    if (path === "/api/probe") return json(res, { build: "offline-harness", session: "fixture-session", mode: "game", vm_running: true, game_sequence: 3, frames: 18420, shelf_frames: 902 });
    if (path === "/api/games") return json(res, games);
    if (path === "/api/cover") {
      const svg = coverSvg(url.searchParams.get("id"));
      if (!svg) return notFound(res, "no cover");
      res.writeHead(200, { "content-type": "image/svg+xml", "cache-control": "no-store" });
      return res.end(svg);
    }
    if (path === "/api/settings" && req.method === "GET") {
      const data = settings(url.searchParams.get("id") || "@global");
      return data ? json(res, data) : notFound(res, "no such game");
    }
    if (path === "/api/settings" && req.method === "POST") {
      const id = url.searchParams.get("id") || "@global";
      const data = settings(id);
      if (!data) return notFound(res, "no such game");
      const body = await bodyText(req);
      if (body.trim() !== "recommended") {
        for (const line of body.split("\n")) {
          if (line.startsWith("set ")) {
            const eq = line.indexOf("=");
            data.values[line.slice(4, eq)] = line.slice(eq + 1);
          } else if (line.startsWith("unset ")) delete data.values[line.slice(6)];
          else if (line.startsWith("patch+ ")) data.patches.concat(data.modern_controls).forEach((p) => { if (p.name === line.slice(7)) p.enabled = true; });
          else if (line.startsWith("patch- ")) data.patches.concat(data.modern_controls).forEach((p) => { if (p.name === line.slice(7)) p.enabled = false; });
        }
      } else Object.assign(data.values, data.recommended.values);
      return json(res, data);
    }
    if (path === "/api/memcards" && req.method === "GET") return json(res, cards);
    if (path === "/api/memcards" && req.method === "POST") {
      const body = await bodyText(req);
      const name = body.split(" ").slice(2).join(" ") || "fixture.ps2";
      const finalName = name.endsWith(".ps2") ? name : name + ".ps2";
      if (!cards.cards.some((c) => c.name === finalName)) cards.cards.push({ name: finalName, bytes: Number(body.split(" ")[1] || 8) * 1081344 });
      return json(res, { ...cards, created: finalName });
    }
    if (path === "/api/discs") return json(res, { current: "/fixtures/dustline-disc1.iso", set: [{ path: "/fixtures/dustline-disc1.iso", name: "dustline-disc1.iso" }, { path: "/fixtures/dustline-disc2.iso", name: "dustline-disc2.iso" }], games: games.filter((g) => !g.id.endsWith(".elf")).map((g) => ({ path: "/fixtures/" + g.id, title: g.title })) });
    if (path === "/api/disc" && req.method === "POST") return json(res, { ok: true });
    if (path === "/api/note" && req.method === "POST") return json(res, { ok: true });
    if (path === "/api/report") {
      res.writeHead(200, { "content-type": "text/plain; charset=utf-8", "content-disposition": "attachment; filename=caliza-fixture-report.txt" });
      return res.end("Caliza offline fixture report\nThis file is generated by the harness, not a console.\n");
    }
    if (path === "/api/achievements") return json(res, achievements);
    if (path === "/api/achievement-badge") {
      res.writeHead(200, { "content-type": "image/svg+xml", "cache-control": "no-store" });
      return res.end('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 96 96"><rect width="96" height="96" rx="14" fill="#202957"/><path d="M22 52 42 72 76 28" fill="none" stroke="#A5E7FF" stroke-width="10" stroke-linecap="round"/></svg>');
    }
    if (path.startsWith("/api/")) return notFound(res);
    const clean = normalize(path === "/" ? "/index.html" : path).replace(/^([/\\])+/, "");
    if (clean.includes("..")) return notFound(res);
    try {
      const file = await readFile(join(root, clean));
      res.writeHead(200, { "content-type": types.get(extname(clean)) || "application/octet-stream", "cache-control": "no-store" });
      res.end(file);
    } catch {
      notFound(res);
    }
  });
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const port = Number(process.env.PORT || 8765);
  createHarnessServer().listen(port, () => {
    console.log(`Caliza offline harness: http://127.0.0.1:${port}/`);
    console.log("Fixture data only; production builds use the console API.");
  });
}
