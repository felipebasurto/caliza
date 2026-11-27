"use strict";
/* Caliza companion panel: original Estratos implementation. */
(() => {
const TEST_MODE = window.CALIZA_TEST_MODE === true;
const $ = (id) => document.getElementById(id);
const esc = (value) => String(value ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const enc = encodeURIComponent;
const truthy = (value) => value === "true" || value === "1" || value === true;
const clamp = (n, min, max) => Math.max(min, Math.min(max, n));
const apiText = { "Content-Type": "text/plain" };
const calizaKey = (suffix) => "Caliza/" + suffix;

function bytesHuman(bytes) {
  const n = Number(bytes) || 0;
  if (n >= 1e9) return (n / 1e9).toFixed(n >= 1e10 ? 0 : 1) + " GB";
  if (n >= 1e6) return (n / 1e6).toFixed(n >= 1e8 ? 0 : 1) + " MB";
  if (n >= 1e3) return (n / 1e3).toFixed(n >= 1e5 ? 0 : 1) + " KB";
  return n + " B";
}
function cardSize(bytes) {
  if (bytes === 131072) return "PS1 card";
  for (const mb of [8, 16, 32, 64]) if (bytes === mb * 1081344 || bytes === mb * 1048576) return mb + " MB";
  return bytesHuman(bytes);
}
function icon(name) { return '<svg aria-hidden="true"><use href="#i-' + name + '"></use></svg>'; }
function coverUrl(id) { return "/api/cover?id=" + enc(id); }
function sameChoice(a, b) { return String(a).trim().toLowerCase() === String(b).trim().toLowerCase() || parseFloat(a) === parseFloat(b); }

async function api(path, opts = {}, timeoutMs = 10000) {
  if (TEST_MODE && typeof window.CALIZA_MOCK_API === "function") return window.CALIZA_MOCK_API(path, opts);
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  let response;
  try {
    response = await fetch(path, { ...opts, signal: controller.signal, cache: "no-store" });
  } catch (error) {
    clearTimeout(timer);
    throw new Error(error.name === "AbortError" ? "the console did not answer in time" : "the console is unreachable");
  }
  clearTimeout(timer);
  const text = await response.text();
  let data = null;
  try { data = text ? JSON.parse(text) : {}; } catch (error) { data = { error: text || response.statusText }; }
  if (!response.ok) throw new Error(data.error || response.statusText || ("HTTP " + response.status));
  return data;
}

// Settings schema. The key strings are canonical Caliza INI keys; they are not shown as product copy.
const CROP_PX = [["0","Off"],["2","2 px"],["4","4 px"],["6","6 px"],["8","8 px"],["10","10 px"],["12","12 px"],["16","16 px"],["24","24 px"],["32","32 px"]];
const TEXEL_OFFSETS = [["0","Off"],["100","100"],["200","200"],["250","250"],["300","300"],["400","400"],["500","500"],["525","525"],["550","550"],["600","600"],["700","700"],["750","750"],["800","800"],["900","900"],["1000","1000"]];
const BUTTON_TARGETS = [["Cross","Cross"],["Circle","Circle"],["Square","Square"],["Triangle","Triangle"],["L1","L1"],["R1","R1"],["L2","L2"],["R2","R2"],["L3","L3"],["R3","R3"],["Start","Start"],["Select","Select"],["Up","D-pad up"],["Down","D-pad down"],["Left","D-pad left"],["Right","D-pad right"],["Analog","Analog button"],["Pressure","Light press"],["None","Nothing"]];
const DEADZONES = [["0","Off"],["5","5%"],["10","10%"],["15","15%"],["20","20%"],["25","25%"],["30","30%"]];
const STRENGTHS = [["1","Full"],["0.75","75%"],["0.5","50%"],["0.4","40%"],["0.3","30%"],["0.25","25%"],["0.2","20%"],["0.15","15%"],["0.1","10%"]];
const INVERT = [["0","Off"],["1","Up-down"],["2","Left-right"],["3","Both"]];
const COMBO_BUTTONS = [["L3R3","L3 + R3"],["Cross","Cross"],["Circle","Circle"],["Square","Square"],["Triangle","Triangle"],["L1","L1"],["R1","R1"],["L2","L2"],["R2","R2"],["L3","L3"],["R3","R3"],["Options","Options"],["Touchpad","Touchpad click"],["TouchLeft","Touchpad left"],["TouchRight","Touchpad right"],["Up","D-pad up"],["Down","D-pad down"],["Left","D-pad left"],["Right","D-pad right"],["None","Nothing"]];
const item = (key, label, type, def, extra = {}) => ({ key, label, type, def, ...extra });
const buttonItem = (name, label, def) => item(calizaKey("Button" + name), label, "pick", def, { options: BUTTON_TARGETS });
const strengthItem = (name, label) => item(calizaKey("Button" + name + "Pressure"), label, "pick", "1", { options: STRENGTHS });
const comboItem = (key, label, def, pair) => item(calizaKey(key), label, "pick", def, { options: COMBO_BUTTONS, pair });

const GROUPS = [
  { title: "Library folders", tab: "settings", only: "global", items: [
    item(calizaKey("GameFolders"), "Additional game folders", "text", "", { restart: true, placeholder: "/mnt/usb0/games;/mnt/ext1/PS2", hint: "Extra folders searched for game images, separated by semicolons. Caliza already checks its own library folders and compatible USB or extended-storage locations. Applies after restart." }),
    item(calizaKey("BezelDir"), "Overlay image folder", "text", "", { placeholder: "/mnt/usb0/PS2/overlays", hint: "Optional first location for per-game overlay PNG files." }),
    item(calizaKey("NfsShares"), "Network game shares", "text", "", { restart: true, placeholder: "nfs://192.168.1.10/volume1/PS2", hint: "Read-only NFS shares, separated by semicolons. Mounting happens when Caliza starts; the boot log records the result." }),
    item(calizaKey("BiosFolder"), "BIOS folder", "text", "", { restart: true, placeholder: "/mnt/usb0/PS2/bios", hint: "Preferred location for a BIOS backup from your own console. Applies after restart." }),
    item("Filenames/BIOS", "BIOS image", "text", "", { restart: true, placeholder: "Automatic", hint: "Exact BIOS file name from the configured BIOS folder. Leave empty for automatic selection. Applies when the game starts." }),
  ]},
  { title: "Library", tab: "settings", only: "game", items: [
    item(calizaKey("HideGame"), "Hide this game", "toggle", "false", { short: "Hidden %", hint: "Removes this game from the library grid. It can be brought back while hidden games are shown." }),
  ]},
  { title: "Library", tab: "settings", only: "global", items: [
    item(calizaKey("ShowHiddenGames"), "Show hidden games", "toggle", "false", { short: "Hidden games %", hint: "Shows hidden entries dimmed so they can be restored." }),
  ]},
  { title: "Discs", tab: "settings", items: [
    item(calizaKey("CheatDiscStart"), "Start from the cheat disc", "toggle", "false", { restart: true, short: "Cheat disc %", hint: "Starts supported games through the cheat disc selected in the all-games settings, then continues with the game's own discs." }),
  ]},
  { title: "Display", tab: "settings", items: [
    item("upscale_multiplier", "Internal resolution", "seg", "1", { short: "%", cols: 4, options: [["1","1x"],["2","2x"],["3","3x"],["4","4x"],["5","5x"],["6","6x"],["8","8x"]], hint: "Higher resolutions sharpen the image and cost more GPU time. 6x is roughly 4K." }),
    item("AspectRatio", "Aspect ratio", "seg", "Auto 4:3/3:2", { short: "Aspect %", options: [["Auto 4:3/3:2","Auto"],["4:3","4:3"],["16:9","16:9"],["Stretch","Stretch"]] }),
    item("EmuCore/EnableWideScreenPatches", "Widescreen patches", "toggle", "false", { short: "Widescreen %", hint: "Uses a 16:9 patch when one exists for the game." }),
    item("TVShader", "Display filter", "seg", "0", { short: "%", cols: 4, options: [["6","FSR"],["7","FSR soft"],["0","Classic"],["5","CRT"]] }),
    item(calizaKey("FrameGeneration"), "Frame generation", "toggle", "false", { restart: true, short: "Frame gen %", hint: "Experimental frame interpolation for displays that accept the higher rate. Applies when the game starts." }),
  ]},
  { title: "Graphics", tab: "settings", items: [
    item(calizaKey("Renderer"), "Renderer", "seg", "Hardware", { restart: true, short: "%", options: [["Hardware","Hardware"],["Software","Software"]], hint: "Software rendering is slower but can be more faithful for effects the hardware renderer gets wrong." }),
    item("filter", "Texture filtering", "seg", "2", { short: "% filtering", options: [["0","Nearest"],["2","PS2"],["1","Bilinear"],["3","Not sprites"]] }),
    item("MaxAnisotropy", "Anisotropic filtering", "seg", "0", { short: "AF %", cols: 5, options: [["0","Off"],["2","2x"],["4","4x"],["8","8x"],["16","16x"]] }),
    item("accurate_blending_unit", "Blending accuracy", "seg", "1", { short: "Blending %", options: [["0","Minimum"],["1","Basic"],["2","Medium"],["3","High"]], hint: "Higher accuracy fixes more effects and costs speed." }),
    item("hw_mipmap", "Mipmapping", "toggle", "true", { short: "Mipmaps %" }),
    item("LoadTextureReplacements", "Texture replacements", "toggle", "false", { short: "Replacements %", hint: "Loads replacement textures from the configured texture folders when a pack exists for the game." }),
    item(calizaKey("TexturesDir"), "Texture folder", "text", "", { restart: true, placeholder: "/mnt/usb0/PS2/textures", hint: "Optional folder containing per-game replacement texture directories." }),
  ]},
  { title: "Performance", tab: "settings", items: [
    item("EmuCore/Speedhacks/EECycleRate", "EE cycle rate", "seg", "0", { short: "EE %", cols: 4, options: [["-3","50%"],["-2","60%"],["-1","75%"],["0","100%"],["1","130%"],["2","180%"],["3","300%"]], hint: "Values above 100% can reduce original-console slowdown, but some games become unstable." }),
    item("EmuCore/Speedhacks/EECycleSkip", "EE cycle skip", "seg", "0", { short: "Cycle skip %", options: [["0","Off"],["1","Mild"],["2","Moderate"],["3","Maximum"]] }),
    item("EmuCore/Speedhacks/vuThread", "MTVU threading", "toggle", "true", { restart: true, short: "MTVU %", hint: "Runs the VU1 processor on its own thread. Applies when the game starts." }),
    item(calizaKey("VUClampMode"), "VU clamp mode", "seg", "-1", { short: "VU clamp %", cols: 5, options: [["-1","Auto"],["0","None"],["1","Normal"],["2","Extra"],["3","Full"]], hint: "Overrides vector unit clamping. Auto follows the game database; Normal or None can speed up VU-heavy games where the database forces Full clamping." }),
    item("HWDownloadMode", "GPU readbacks", "seg", "0", { short: "Readbacks %", cols: 2, options: [["0","Accurate"],["1","Whole area"],["3","Don't wait"],["4","Skip"]], hint: "Controls slow texture readbacks used by some games. Less accurate modes can break effects that depend on them." }),
    item("EmuCore/Speedhacks/OrbisMTVUSpin", "MTVU spin wait", "toggle", "true", { short: "VU spin %", hint: "The VU thread waits briefly for work before sleeping. Only matters with MTVU threading." }),
    item("EmuCore/Speedhacks/OrbisMTVUBatch", "MTVU kick batching", "toggle", "false", { short: "VU batch %", hint: "Wakes the VU thread once at the end of a VIF1 transfer instead of for every packet. Try it per game." }),
    item("EmuCore/Speedhacks/OrbisMTVURingKB", "MTVU ring (KB)", "text", "2048", { short: "VU ring % KB", placeholder: "256–2048", hint: "The EE-to-VU ring length in KB. Shorter can stay in cache; too short can stutter or break a game." }),
  ]},
  { title: "Game system", tab: "settings", items: [
    item(calizaKey("GameLanguage"), "Game language", "seg", "-1", { restart: true, short: "Language %", cols: 3, options: [["-1","Auto"],["0","Japanese"],["1","English"],["2","French"],["3","Spanish"],["4","German"],["5","Italian"],["6","Dutch"],["7","Portuguese"]], hint: "The language reported to multi-language games. Applies when the game starts." }),
    item("DEV9/Eth/EthEnable", "Network adapter", "toggle", "true", { restart: true, short: "Network %", hint: "Emulates the network adapter for online and LAN play. Applies when the game starts." }),
    item("EmuCore/HostFs", "Host filesystem", "toggle", "false", { restart: true, short: "Host files %", hint: "Allows homebrew software that explicitly uses the host device to reach files beside its executable or disc image." }),
    item("EmuCore/EnableFastBoot", "Fast boot", "toggle", "true", { restart: true, short: "Fast boot %", hint: "Off shows the original console startup sequence; on goes directly to the game." }),
  ]},
  { title: "On screen", tab: "settings", items: [
    item(calizaKey("Overlay"), "Performance overlay", "seg", "1", { short: "Overlay %", cols: 3, options: [["0","Off"],["1","FPS"],["2","FPS + load"]], hint: "Shows the frame rate as a number, or with processor load and picture size in a box while playing." }),
    item(calizaKey("FpsGraph"), "Frame-rate graph", "toggle", "false", { short: "FPS graph %" }),
    item(calizaKey("Bezel"), "Overlay image", "toggle", "false", { short: "Overlay %", hint: "Draws a PNG frame around the game when one exists for its serial or as a default overlay." }),
    item(calizaKey("RAChallengeIcons"), "Achievement challenge icons", "toggle", "true", { short: "Challenge icons %", hint: "Shows active achievement challenge badges while they are in progress." }),
  ]},
  { title: "Hardware fixes", tab: "settings", only: "game", hint: "Targeted workarounds for upscaling artifacts. Changing one enables manual fixes for this game and preserves its database-provided fixes.", items: [
    item("UserHacks", "Manual hardware fixes", "toggle", "false", { short: "Manual fixes %" }),
    item("UserHacks_HalfPixelOffset", "Half-pixel offset", "pick", "0", { fix: true, short: "Half-pixel %", options: [["0","Off"],["1","Normal (vertex)"],["2","Special (texture)"],["3","Special (aggressive)"],["4","Align to native"],["5","Native + texture offset"]], hint: "Realigns effects such as bloom, blur and shadows when upscaling." }),
    item("UserHacks_native_scaling", "Native scaling", "pick", "0", { fix: true, short: "Native scaling %", options: [["0","Off"],["1","Normal"],["2","Aggressive"],["3","Normal (keep upscale)"],["4","Aggressive (keep upscale)"]], hint: "Draws some post-processing at native resolution before scaling." }),
    item("UserHacks_round_sprite_offset", "Round sprite", "seg", "0", { fix: true, short: "Round sprite %", options: [["0","Off"],["1","Half"],["2","Full"]], hint: "Can remove lines and blur in upscaled 2D art." }),
    item("UserHacks_align_sprite_X", "Align sprite", "toggle", "false", { fix: true, short: "Align sprite %" }),
    item("UserHacks_merge_pp_sprite", "Merge sprite", "toggle", "false", { fix: true, short: "Merge sprite %" }),
    item("UserHacks_BilinearHack", "Bilinear upscale", "seg", "0", { fix: true, short: "Bilinear %", options: [["0","Auto"],["1","Bilinear"],["2","Nearest"]] }),
    item("UserHacks_TCOffsetX", "Texture offset X", "pick", "0", { fix: true, short: "Texture X %", options: TEXEL_OFFSETS, hint: "Shifts texture reads in thousandths of a texel for edge artifacts." }),
    item("UserHacks_TCOffsetY", "Texture offset Y", "pick", "0", { fix: true, short: "Texture Y %", options: TEXEL_OFFSETS }),
    item("UserHacks_AutoFlushLevel", "Auto flush", "seg", "0", { fix: true, short: "Auto flush %", options: [["0","Off"],["1","Sprites"],["2","All"]], hint: "Can repair effects that read back their own render target, at a speed cost." }),
    item("UserHacks_TextureInsideRt", "Texture inside target", "seg", "0", { fix: true, short: "Texture target %", options: [["0","Off"],["1","Inside"],["2","Merge"]] }),
  ]},
  { title: "Crop", tab: "settings", hint: "Cuts edge pixels before scaling, for games with garbage at a border.", items: [
    item("CropLeft", "Crop left", "seg", "0", { short: "Crop left %", cols: 5, options: CROP_PX }),
    item("CropTop", "Crop top", "seg", "0", { short: "Crop top %", cols: 5, options: CROP_PX }),
    item("CropRight", "Crop right", "seg", "0", { short: "Crop right %", cols: 5, options: CROP_PX }),
    item("CropBottom", "Crop bottom", "seg", "0", { short: "Crop bottom %", cols: 5, options: CROP_PX }),
  ]},
  { title: "Interface accessibility", tab: "settings", only: "global", hint: "These Caliza interface settings are shared with the console UI. Browser text size, contrast and motion preferences can also be adjusted in Display preferences.", items: [
    item(calizaKey("UiTextScalePercent"), "Console text size", "seg", "100", { short: "Text %", cols: 5, options: [["100","Standard"],["125","Large"],["150","Extra large"],["175","Huge"],["200","Maximum"]] }),
    item(calizaKey("UiHighContrast"), "High contrast", "toggle", "false", { short: "Contrast %" }),
    item(calizaKey("UiReduceMotion"), "Reduce motion", "toggle", "false", { short: "Motion %" }),
    item(calizaKey("UiLargeFocus"), "Large focus indicator", "toggle", "true", { short: "Focus %" }),
  ]},
  { title: "State and disc shortcuts", tab: "controls", hint: "Combos use two physical buttons held together. These shortcuts do not depend on the in-game remapping below.", items: [
    comboItem("SaveButton1", "Save: first button", "L3R3", "Save state"), comboItem("SaveButton2", "Save: second button", "Up"),
    comboItem("LoadButton1", "Load: first button", "L3R3", "Load state"), comboItem("LoadButton2", "Load: second button", "Down"),
    item(calizaKey("StateHold"), "Hold time", "seg", "0", { cols: 6, options: [["0","Instant"],["0.5","0.5 s"],["1","1 s"],["1.5","1.5 s"],["2","2 s"],["3","3 s"]], hint: "A longer hold prevents accidental saves and loads." }),
    comboItem("FastButton1", "Fast forward: first button", "None", "Fast forward"), comboItem("FastButton2", "Fast forward: second button", "None"),
    item(calizaKey("FastSpeed"), "Fast forward speed", "seg", "0", { cols: 4, options: [["0","Maximum"],["2","2x"],["3","3x"],["4","4x"]] }),
    comboItem("DiscButton1", "Change disc: first button", "L3R3", "Change disc"), comboItem("DiscButton2", "Change disc: second button", "Right"),
  ]},
  { title: "Button remapping", tab: "controls", hint: "Choose what each controller control presses in the emulated game.", items: [
    buttonItem("Cross", "Cross", "Cross"), buttonItem("Circle", "Circle", "Circle"), buttonItem("Square", "Square", "Square"), buttonItem("Triangle", "Triangle", "Triangle"),
    buttonItem("L1", "L1", "L1"), buttonItem("R1", "R1", "R1"), buttonItem("L2", "L2", "L2"), buttonItem("R2", "R2", "R2"),
    buttonItem("L3", "L3", "L3"), buttonItem("R3", "R3", "R3"), buttonItem("Options", "Options", "Start"), buttonItem("Touchpad", "Touchpad click", "Select"),
    buttonItem("Up", "D-pad up", "Up"), buttonItem("Down", "D-pad down", "Down"), buttonItem("Left", "D-pad left", "Left"), buttonItem("Right", "D-pad right", "Right"),
  ]},
  { title: "Button strength", tab: "controls", hint: "Pressure-sensitive games can react differently to a light press.", items: [
    strengthItem("Cross", "Cross"), strengthItem("Circle", "Circle"), strengthItem("Square", "Square"), strengthItem("Triangle", "Triangle"),
    strengthItem("L1", "L1"), strengthItem("R1", "R1"), strengthItem("L2", "L2"), strengthItem("R2", "R2"), strengthItem("Touchpad", "Touchpad click"),
    strengthItem("Up", "D-pad up"), strengthItem("Down", "D-pad down"), strengthItem("Left", "D-pad left"), strengthItem("Right", "D-pad right"),
  ]},
  { title: "Sticks", tab: "controls", items: [
    item(calizaKey("SwapSticks"), "Swap sticks", "toggle", "false"),
    item(calizaKey("InvertLeft"), "Invert left stick", "seg", "0", { options: INVERT }),
    item(calizaKey("InvertRight"), "Invert right stick", "seg", "0", { options: INVERT }),
    item(calizaKey("LeftStickDpad"), "Left stick as D-pad", "seg", "0", { cols: 3, options: [["0","Off"],["1","Also"],["2","Only"]], hint: "Useful in games that only read the D-pad." }),
    item(calizaKey("DeadzoneLeft"), "Left stick dead zone", "pick", "0", { options: DEADZONES, hint: "Increase this if the stick drifts." }),
    item(calizaKey("DeadzoneRight"), "Right stick dead zone", "pick", "0", { options: DEADZONES, hint: "Increase this if the camera moves by itself." }),
  ]},
  { title: "Controller", tab: "controls", items: [
    item(calizaKey("Rumble"), "Rumble", "toggle", "true", { short: "Rumble %" }),
    item(calizaKey("RumbleStrength"), "Rumble strength", "pick", "1", { options: [["0.25","25%"],["0.5","50%"],["0.75","75%"],["1","100%"],["1.25","125%"],["1.5","150%"],["2","200%"]] }),
    item(calizaKey("Multitap"), "Multitap", "seg", "0", { restart: true, cols: 3, options: [["0","Off"],["1","Port 1"],["2","Port 2"]], hint: "Enables additional local players in games that support a multitap. Applies when the game starts." }),
  ]},
  { title: "Keyboard and mouse", tab: "controls", items: [
    item(calizaKey("KeyboardMouse"), "Keyboard and mouse mode", "seg", "0", { short: "Keyboard/mouse %", cols: 2, options: [["0","Auto"],["1","Controller"],["2","USB devices"],["3","Off"]], hint: "Auto uses controller emulation until software asks for USB keyboard or mouse devices." }),
    item(calizaKey("MouseAim"), "Mouse movement", "seg", "1", { cols: 3, options: [["1","Right stick"],["2","Left stick"],["0","Nothing"]] }),
    item(calizaKey("MouseSpeed"), "Mouse speed", "seg", "2", { cols: 4, options: [["1","Slow"],["2","Normal"],["3","Fast"],["4","Very fast"]] }),
    item(calizaKey("MouseButtons"), "Mouse buttons", "seg", "0", { cols: 2, options: [["0","R1 and L1"],["1","R2 and L2"]] }),
  ]},
];

const SLOT_SETTINGS = [
  { n: 1, file: "MemoryCards/Slot1_Filename", on: "MemoryCards/Slot1_Enable", def: "Mcd001.ps2" },
  { n: 2, file: "MemoryCards/Slot2_Filename", on: "MemoryCards/Slot2_Enable", def: "Mcd002.ps2" },
];

const app = {
  booted: false,
  state: null,
  games: [],
  gamesLoaded: false,
  cards: null,
  cardsError: "",
  discs: null,
  discsError: "",
  probe: undefined,
  probeError: "",
  settingsCache: new Map(),
  settingsErrors: new Map(),
  settingsPromises: new Map(),
  pendingByScope: new Map(),
  saveTimers: new Map(),
  saveInFlight: new Set(),
  route: { name: "library" },
  editorScopes: new Map(),
  activeTab: "settings",
  achievement: { data: null, error: "", loading: false, page: 0, filter: "all" },
  filters: { query: "", mode: "all" },
  memoryScope: "@global",
  connection: "connecting",
  confirmations: new Map(),
  cardForm: { size: 8, name: "", busy: false },
  renderSerial: 0,
  pollTimer: 0,
  achievementTimer: 0,
  gamepadSeen: false,
};

function currentGameId() { return app.route.name === "game" ? app.route.id : null; }
function editorScopeFor(gameId) { return gameId === "@global" ? "@global" : (app.editorScopes.get(gameId) || gameId); }
function settingsInView() {
  if (app.route.name === "game") return editorScopeFor(app.route.id);
  if (app.route.name === "memory") return app.memoryScope;
  return null;
}
function pendingFor(scope) {
  if (!app.pendingByScope.has(scope)) app.pendingByScope.set(scope, new Map());
  return app.pendingByScope.get(scope);
}
function findGame(id) { return app.games.find((g) => g.id === id); }
function allSettingsItems() { return GROUPS.flatMap((group) => group.items); }
function findSettingItem(key) { return allSettingsItems().find((it) => it.key === key); }

function toast(message) {
  const el = $("toast");
  if (!el) return;
  el.textContent = message;
  el.classList.add("show");
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => el.classList.remove("show"), 2600);
}
function saveStatus(message) {
  const el = $("save-status");
  if (el) el.textContent = message || "";
}
function updateClock() {
  const el = $("clock");
  if (!el) return;
  const now = new Date();
  el.textContent = String(now.getHours()).padStart(2, "0") + ":" + String(now.getMinutes()).padStart(2, "0");
  el.dateTime = now.toISOString();
}

function setConnection(state, label) {
  app.connection = state;
  const pill = $("connection-pill");
  const text = $("connection-label");
  if (pill) pill.dataset.state = state;
  if (text) text.textContent = label || (state === "online" ? "Connected" : state === "offline" ? "Offline" : "Connecting");
}

function iconLabelButton(action, label, iconName, extra = "") {
  return '<button class="button ' + extra + '" type="button" data-action="' + esc(action) + '" data-focus-id="action-' + esc(action) + '">' + icon(iconName) + esc(label) + '</button>';
}
function notice(kind, title, body, actions = "") {
  return '<div class="notice ' + kind + '" role="' + (kind === "error" ? "alert" : "status") + '">' + icon(kind === "error" || kind === "warn" ? "alert" : "check") +
    '<div><strong>' + esc(title) + '</strong><p>' + body + '</p>' + (actions ? '<div class="actions empty-actions">' + actions + '</div>' : '') + '</div></div>';
}
function loadingBlock(text) {
  return '<div class="loading-block" role="status" aria-live="polite"><span>' + esc(text) + '</span></div>';
}
function emptyState(iconName, title, body, actions = "") {
  return '<div class="empty-state"><div class="empty-icon">' + icon(iconName) + '</div><h2>' + esc(title) + '</h2><p>' + body + '</p>' +
    (actions ? '<div class="empty-actions">' + actions + '</div>' : '') + '</div>';
}
function captureFocus() {
  const el = document.activeElement;
  if (!el || el === document.body || !el.isConnected) return null;
  const id = el.dataset && el.dataset.focusId;
  if (!id) return null;
  return { id, start: typeof el.selectionStart === "number" ? el.selectionStart : null, end: typeof el.selectionEnd === "number" ? el.selectionEnd : null };
}
function restoreFocus(token) {
  if (!token) return;
  const el = document.querySelector('[data-focus-id="' + CSS.escape(token.id) + '"]');
  if (!el) return;
  el.focus({ preventScroll: true });
  if (token.start !== null && typeof el.setSelectionRange === "function") {
    try { el.setSelectionRange(token.start, token.end); } catch (error) {}
  }
}
function render(options = {}) {
  const preserve = options.preserveFocus !== false;
  const focusToken = preserve ? captureFocus() : null;
  app.renderSerial++;
  updateNav();
  const main = $("main");
  if (!main) return;
  if (!app.booted) {
    main.innerHTML = renderBoot();
  } else if (app.connection === "offline" && !app.state) {
    main.innerHTML = renderOffline();
  } else {
    if (app.route.name === "library") main.innerHTML = renderLibrary();
    else if (app.route.name === "game") main.innerHTML = renderGameDetail(app.route.id);
    else if (app.route.name === "memory") main.innerHTML = renderMemoryCards();
    else if (app.route.name === "discs") main.innerHTML = renderDiscs();
    else if (app.route.name === "diagnostics") main.innerHTML = renderDiagnostics();
    else main.innerHTML = renderLibrary();
  }
  bindDynamic(main);
  if (options.focusMain) focusMainHeading();
  else if (preserve) restoreFocus(focusToken);
}
function focusMainHeading() {
  requestAnimationFrame(() => {
    const heading = document.querySelector("#main [data-view-heading]");
    if (heading) heading.focus({ preventScroll: false });
  });
}
function updateNav() {
  const section = app.route.name === "game" ? "library" : app.route.name;
  document.querySelectorAll("[data-nav]").forEach((link) => {
    if (link.dataset.nav === section) link.setAttribute("aria-current", "page");
    else link.removeAttribute("aria-current");
  });
}

function parseRoute() {
  const hash = location.hash || "#/library";
  const parts = hash.replace(/^#\/?/, "").split("/").filter(Boolean);
  const name = parts[0] || "library";
  if (name === "game" && parts[1]) {
    let id = parts.slice(1).join("/");
    try { id = decodeURIComponent(id); } catch (error) {}
    app.route = { name: "game", id };
  } else if (["memory", "discs", "diagnostics", "library"].includes(name)) {
    app.route = { name };
  } else {
    app.route = { name: "library" };
  }
}
function navigate(hash) { location.hash = hash; }
function navigateBack() {
  const dialog = $("display-dialog");
  if (dialog && dialog.open) { dialog.close(); return; }
  if (app.route.name !== "library") navigate("/library");
}
window.addEventListener("hashchange", () => {
  parseRoute();
  app.achievement.page = 0;
  render({ preserveFocus: false, focusMain: true });
});

function renderBoot() {
  return '<section class="view"><div class="view-title"><h1 data-view-heading tabindex="-1">Caliza</h1><p>Opening the companion panel.</p></div>' + loadingBlock("Contacting the console") + '</section>';
}
function renderOffline() {
  const retry = '<button class="button primary" type="button" data-action="retry-connection" data-focus-id="retry-connection">' + icon("refresh") + 'Try again</button>';
  return '<section class="view"><div class="view-title"><h1 data-view-heading tabindex="-1">Caliza</h1><p>The panel is loaded, but the console API is not answering.</p></div>' +
    notice("error", "Console not reachable", "Open this page from the console address while Caliza is running, check that both devices are on the same network, then try again.", retry) + '</section>';
}
function nowPlayingHtml() {
  const playing = app.state && app.state.playing;
  if (!playing) return "";
  return '<aside class="now-playing" aria-label="Now playing">' +
    '<img class="now-cover" src="' + coverUrl(playing.id) + '" alt="" data-now-cover>' +
    '<div class="now-copy"><span class="eyebrow-line"><span class="live-dot"></span>Now playing</span><strong>' + esc(playing.title) + '</strong><small>' + esc(playing.serial || "Serial unavailable") + ' · settings apply live where the emulator supports it</small></div>' +
    '<div class="now-actions"><a class="button ghost" href="#/discs" data-focus-id="now-discs">' + icon("disc") + 'Discs</a><a class="button ghost" href="#/game/' + enc(playing.id) + '" data-focus-id="now-settings">' + icon("settings") + 'Game settings</a></div></aside>';
}

function filteredGames() {
  const q = app.filters.query.trim().toLowerCase();
  return app.games.filter((g) => {
    if (q && !(g.title || "").toLowerCase().includes(q) && !(g.serial || "").toLowerCase().includes(q) && !(g.id || "").toLowerCase().includes(q)) return false;
    if (app.filters.mode === "playing") return !!g.playing;
    if (app.filters.mode === "configured") return !!g.settings;
    if (app.filters.mode === "covered") return !!g.cover;
    if (app.filters.mode === "uncovered") return !g.cover;
    return true;
  });
}
function gameCardHtml(g) {
  const meta = [g.serial, g.region, bytesHuman(g.size)].filter(Boolean).map(esc).join(" · ");
  const badges = [];
  if (g.settings) badges.push('<span class="badge settings">Settings</span>');
  for (const b of (g.badges || []).slice(0, 3)) badges.push('<span class="badge">' + esc(b) + '</span>');
  return '<button class="game-card" type="button" data-action="open-game" data-id="' + esc(g.id) + '" data-focus-id="card-' + esc(g.id) + '" aria-label="Open ' + esc(g.title) + '">' +
    (g.playing ? '<span class="playing-ribbon">Playing</span>' : '') +
    '<span class="cover-frame">' + (g.cover ? '<img loading="lazy" src="' + coverUrl(g.id) + '" alt="">' : '<span class="cover-placeholder"><img src="assets/caliza-mark.svg" alt=""><span>' + esc(g.title) + '</span></span>') + '</span>' +
    '<span class="game-title">' + esc(g.title) + '</span>' +
    '<span class="game-meta"><span class="meta-line">' + meta + '</span><span class="badge-row">' + badges.join("") + '</span></span></button>';
}
function renderLibrary() {
  const games = filteredGames();
  const total = app.games.length;
  const filterButtons = [["all","All"],["playing","Playing"],["configured","Has settings"],["covered","Has cover"],["uncovered","No cover"]].map(([mode,label]) =>
    '<button class="chip" type="button" aria-pressed="' + (app.filters.mode === mode ? "true" : "false") + '" data-action="filter-mode" data-mode="' + mode + '" data-focus-id="filter-' + mode + '">' + label + '</button>').join("");
  let content = "";
  if (!app.gamesLoaded) content = loadingBlock("Scanning the library");
  else if (!total) {
    content = emptyState("library", "No games found", "Caliza has not reported any compatible game images. Add a library folder in the all-games settings, connect the drive that holds your games, then refresh this panel.",
      '<a class="button primary" href="#/game/%40global" data-focus-id="empty-open-folders">' + icon("folder") + 'Open folder settings</a><button class="button ghost" type="button" data-action="refresh-library" data-focus-id="empty-refresh">' + icon("refresh") + 'Refresh</button>');
  } else if (!games.length) {
    content = emptyState("search", "No games match", "Change the search text or clear the active filter to see the collection again.",
      '<button class="button ghost" type="button" data-action="clear-filters" data-focus-id="clear-filters">' + icon("refresh") + 'Clear filters</button>');
  } else {
    content = '<div class="library-grid" aria-label="Games">' + games.map(gameCardHtml).join("") + '</div>';
  }
  return '<section class="view"><div class="view-header"><div class="view-title"><h1 data-view-heading tabindex="-1">Library</h1><p>Browse the collection reported by the console. Covers and metadata load only from the live API.</p></div><div class="header-controls"><button class="icon-button" type="button" data-action="refresh-library" data-focus-id="refresh-library" aria-label="Refresh library">' + icon("refresh") + '<span>Refresh</span></button></div></div>' +
    nowPlayingHtml() +
    '<div class="toolbar panel panel-pad"><div class="search-row"><label class="search-field"><svg aria-hidden="true"><use href="#i-search"></use></svg><input class="input" id="library-search" data-focus-id="library-search" type="search" autocomplete="off" enterkeyhint="search" placeholder="Search title, serial or file" value="' + esc(app.filters.query) + '" aria-label="Search games"></label><span class="library-count">' + games.length + ' of ' + total + '</span></div><div class="filter-row" aria-label="Library filters">' + filterButtons + '</div></div>' +
    content + '</section>';
}

function effectiveSetting(data, it) {
  if (!data) return { value: it.def, from: "default" };
  const own = data.values ? data.values[it.key] : undefined;
  if (own !== undefined) return { value: own, from: "own" };
  if (it.fix && data.gamefixes && data.gamefixes[it.key] !== undefined && !manualFixesOn(data)) return { value: data.gamefixes[it.key], from: "gamefix" };
  if (data.id !== "@global" && data.global && data.global[it.key] !== undefined) return { value: data.global[it.key], from: "global" };
  return { value: it.def, from: "default" };
}
function manualFixesOn(data) {
  if (!data) return false;
  const own = data.values && data.values.UserHacks;
  const inherited = data.id !== "@global" && data.global ? data.global.UserHacks : undefined;
  return truthy(own !== undefined ? own : inherited);
}
function optionLabel(it, value) {
  if (it.type === "toggle") return truthy(value) ? "on" : "off";
  const found = (it.options || []).find((o) => sameChoice(o[0], value));
  return found ? found[1] : String(value);
}
function sourceHtml(data, it, eff, scope, extraKey = "") {
  let label = "Default";
  let reset = "";
  if (eff.from === "own") {
    label = scope === "@global" ? "Set for all games" : "Set for this game";
    reset = ' <button class="text-button" type="button" data-action="reset-setting" data-key="' + esc(it.key) + '"' + (extraKey ? ' data-key2="' + esc(extraKey) + '"' : "") + ' data-focus-id="reset-' + esc(scope + '-' + it.key) + '">reset</button>';
  } else if (eff.from === "global") label = "From all games";
  else if (eff.from === "gamefix") label = "Game database fix";
  return '<div class="setting-source">' + esc(label) + reset + '</div>';
}
function selectHtml(it, eff, scope, pairRole = "") {
  let opts = "";
  for (const option of it.options || []) opts += '<option value="' + esc(option[0]) + '"' + (sameChoice(option[0], eff.value) ? " selected" : "") + '>' + esc(option[1]) + '</option>';
  if (!(it.options || []).some((o) => sameChoice(o[0], eff.value))) opts += '<option value="' + esc(eff.value) + '" selected>' + esc(eff.value) + '</option>';
  return '<select class="select" data-action-input="pick-setting" data-key="' + esc(it.key) + '" data-focus-id="setting-' + esc(scope + '-' + it.key + pairRole) + '" aria-label="' + esc(it.label) + '">' + opts + '</select>';
}
function renderSettingRow(data, group, it, index, scope) {
  if (it.pair && index > 0 && group.items[index - 1].pair) return "";
  const eff = effectiveSetting(data, it);
  const tag = it.restart ? '<span class="tag">next launch</span>' : "";
  const hint = it.hint ? '<div class="setting-hint">' + esc(it.hint) + '</div>' : "";
  let control = "";
  if (it.type === "toggle") {
    control = '<button class="toggle-switch" type="button" aria-pressed="' + (truthy(eff.value) ? "true" : "false") + '" aria-label="' + esc(it.label) + '" data-action="toggle-setting" data-key="' + esc(it.key) + '" data-value="' + (truthy(eff.value) ? "false" : "true") + '" data-focus-id="setting-' + esc(scope + '-' + it.key) + '"></button>';
  } else if (it.type === "pick" && it.pair) {
    const next = group.items[index + 1];
    const effNext = effectiveSetting(data, next);
    const pairOwn = eff.from === "own" || effNext.from === "own";
    const pairSource = pairOwn ? sourceHtml(data, it, eff.from === "own" ? eff : effNext, scope, eff.from === "own" ? next.key : it.key) : sourceHtml(data, it, eff, scope);
    return '<div class="setting-row"><div class="setting-copy"><div class="setting-label">' + esc(it.pair) + tag + '</div>' + pairSource + hint + '</div><div class="setting-control pair-grid">' + selectHtml(it, eff, scope, "-first") + '<span class="pair-plus">+</span>' + selectHtml(next, effNext, scope, "-second") + '</div></div>';
  } else if (it.type === "pick") {
    control = selectHtml(it, eff, scope);
  } else if (it.type === "text") {
    control = '<input class="input" type="text" data-action-input="text-setting" data-key="' + esc(it.key) + '" data-focus-id="setting-' + esc(scope + '-' + it.key) + '" autocomplete="off" autocapitalize="off" spellcheck="false" maxlength="240" placeholder="' + esc(it.placeholder || "") + '" value="' + esc(eff.from === "default" ? "" : eff.value) + '" aria-label="' + esc(it.label) + '">';
  } else {
    const style = it.cols ? ' style="--choice-cols:' + it.cols + '"' : "";
    control = '<div class="choice-grid' + (it.cols ? "" : " wrap") + '"' + style + '>' + (it.options || []).map((option) => {
      const current = sameChoice(option[0], eff.value);
      return '<button class="segment-button" type="button" aria-pressed="' + (current ? "true" : "false") + '" data-action="choice-setting" data-key="' + esc(it.key) + '" data-value="' + esc(option[0]) + '" data-focus-id="setting-' + esc(scope + '-' + it.key + '-' + option[0]) + '">' + esc(option[1]) + '</button>';
    }).join("") + '</div>';
  }
  return '<div class="setting-row"><div class="setting-copy"><div class="setting-label">' + esc(it.label) + tag + '</div>' + sourceHtml(data, it, eff, scope) + hint + '</div><div class="setting-control">' + control + '</div></div>';
}
function recommendedSummary(data) {
  const rec = data && data.recommended;
  if (!rec) return "";
  if (!rec.found && data.id !== "@global") return "No tuned preset for this game; it follows the all-games settings.";
  const bits = [];
  for (const it of allSettingsItems()) {
    const value = rec.values ? rec.values[it.key] : undefined;
    if (value !== undefined && it.short) bits.push(it.short.replace("%", optionLabel(it, value)));
  }
  for (const patch of rec.patches || []) bits.push(patch + " patch");
  return bits.length ? bits.join(" · ") : "Follows the all-games settings.";
}
function renderRecommended(data, scope) {
  if (!data || !data.recommended || (!data.recommended.found && scope === "@global")) return "";
  const armed = app.confirmations.get("recommended-" + scope) === true;
  const inUse = !!data.is_recommended;
  return '<section class="recommended"><div><h2>Recommended settings</h2><p>' + esc(recommendedSummary(data)) + '</p></div><button class="button ' + (inUse ? "ghost" : "primary") + '" type="button" data-action="apply-recommended" data-focus-id="apply-recommended" ' + (inUse ? "disabled" : "") + '>' + (inUse ? "In use" : armed ? "Confirm replacement" : "Use recommended") + '</button></section>';
}
function renderPatchList(data) {
  if (!data || data.id === "@global") return "";
  let html = "";
  if (Array.isArray(data.patches)) {
    html += '<section class="setting-section"><header><h2>Patches</h2><p>Game patch groups reported by the console.</p></header>';
    if (!data.patches.length) html += '<div class="setting-row"><div class="setting-copy"><div class="setting-hint">No patch file is installed for this game.</div></div></div>';
    for (const patch of data.patches) {
      html += '<div class="setting-row"><div class="setting-copy"><div class="setting-label">' + esc(patch.name) + '</div>' + (patch.description ? '<div class="setting-hint">' + esc(patch.description) + '</div>' : "") + (patch.file ? '<div class="setting-source">' + esc(patch.file) + '</div>' : "") + '</div><div class="setting-control"><button class="toggle-switch" type="button" aria-pressed="' + (patch.enabled ? "true" : "false") + '" aria-label="' + esc(patch.name) + '" data-action="toggle-patch" data-name="' + esc(patch.name) + '" data-value="' + (patch.enabled ? "false" : "true") + '" data-focus-id="patch-' + esc(patch.name) + '"></button></div></div>';
    }
    html += '</section>';
  }
  if (Array.isArray(data.cheats) && data.cheats.length) {
    html += '<section class="setting-section"><header><h2>Cheats</h2><p>Enabling a cheat also enables cheat processing for this game.</p></header>';
    for (const cheat of data.cheats) {
      html += '<div class="setting-row"><div class="setting-copy"><div class="setting-label">' + esc(cheat.name) + '</div>' + (cheat.description ? '<div class="setting-hint">' + esc(cheat.description) + '</div>' : "") + (cheat.file ? '<div class="setting-source">' + esc(cheat.file) + '</div>' : "") + '</div><div class="setting-control"><button class="toggle-switch" type="button" aria-pressed="' + (cheat.enabled ? "true" : "false") + '" aria-label="' + esc(cheat.name) + '" data-action="toggle-cheat" data-name="' + esc(cheat.name) + '" data-value="' + (cheat.enabled ? "false" : "true") + '" data-focus-id="cheat-' + esc(cheat.name) + '"></button></div></div>';
    }
    html += '</section>';
  }
  return html;
}
function renderDiscBindingSettings(data, scope) {
  if (!data) return "";
  const isElf = /\.elf$/i.test(data.id);
  if (scope !== "@global" && !isElf) return "";
  const key = scope === "@global" ? calizaKey("CheatDisc") : calizaKey("ElfDisc");
  const current = data.values[key] || "";
  const title = scope === "@global" ? "Cheat disc" : "ELF disc image";
  const none = scope === "@global" ? "None" : "No disc";
  const hint = scope === "@global"
    ? "The optional cheat or code-disc image used when a game has Start from the cheat disc enabled. It starts first; the game discs follow it."
    : "The disc image this executable runs with, for patched software or homebrew that expects one. No disc runs the executable alone.";
  let opts = '<option value=""' + (current ? "" : " selected") + '>' + none + '</option>';
  for (const game of app.games) {
    if (/\.elf$/i.test(game.id)) continue;
    opts += '<option value="' + esc(game.id) + '"' + (game.id === current ? " selected" : "") + '>' + esc(game.title) + '</option>';
  }
  if (current && !app.games.some((game) => game.id === current)) opts += '<option value="' + esc(current) + '" selected>' + esc(current) + ' · not in the library scan</option>';
  return '<section class="setting-section"><header><h2>' + title + '</h2><p>' + hint + '</p></header><div class="setting-row"><div class="setting-copy"><div class="setting-label">' + title + '<span class="tag">next launch</span></div><div class="setting-source">' + (current ? "Set here" : none) + (current ? ' <button class="text-button" type="button" data-action="reset-setting" data-key="' + key + '" data-focus-id="reset-' + key + '">reset</button>' : "") + '</div></div><div class="setting-control"><select class="select" data-action-input="pick-setting" data-key="' + key + '" data-focus-id="setting-' + esc(scope + '-' + key) + '" aria-label="' + title + '">' + opts + '</select></div></div></section>';
}

function renderSettingsEditor(data, scope, tab) {
  let html = "";
  if (tab === "settings") html += renderRecommended(data, scope) + renderDiscBindingSettings(data, scope);
  for (const group of GROUPS) {
    if ((group.tab || "settings") !== tab) continue;
    if (group.only === "global" && scope !== "@global") continue;
    if (group.only === "game" && scope === "@global") continue;
    html += '<section class="setting-section"><header><h2>' + esc(group.title) + '</h2>' + (group.hint ? '<p>' + esc(group.hint) + '</p>' : "") + '</header>';
    group.items.forEach((it, index) => { html += renderSettingRow(data, group, it, index, scope); });
    html += '</section>';
  }
  if (tab === "settings") html += renderPatchList(data);
  if (tab === "controls") {
    const hasOwn = GROUPS.filter((g) => g.tab === "controls").flatMap((g) => g.items).some((it) => data.values && data.values[it.key] !== undefined);
    const armed = app.confirmations.get("controls-reset-" + scope) === true;
    html += '<div class="empty-actions"><button class="button ghost" type="button" data-action="reset-controls" data-focus-id="reset-controls" ' + (hasOwn ? "" : "disabled") + '>' + (armed ? "Confirm control reset" : scope === "@global" ? "Reset controls here" : "Follow all-games controls") + '</button></div>';
  }
  html += '<p class="file-note">' + esc(data.file || "settings file") + (data.exists ? "" : " · created when the first change is saved") + '</p>';
  return '<div class="settings-stack">' + html + '</div>';
}

function ensureSettings(scope) {
  if (app.settingsCache.has(scope) || app.settingsPromises.has(scope)) return;
  const promise = api("/api/settings?id=" + enc(scope)).then((data) => {
    app.settingsCache.set(scope, data);
    app.settingsErrors.delete(scope);
  }).catch((error) => {
    app.settingsErrors.set(scope, error.message || "Settings could not be loaded");
  }).finally(() => {
    app.settingsPromises.delete(scope);
    if (settingsInView() === scope) render();
  });
  app.settingsPromises.set(scope, promise);
}
function settingsBlock(scope) {
  const data = app.settingsCache.get(scope);
  if (data) return data;
  const error = app.settingsErrors.get(scope);
  if (error) {
    return { error, html: notice("error", "Settings did not load", esc(error), '<button class="button ghost" type="button" data-action="retry-settings" data-focus-id="retry-settings">' + icon("refresh") + 'Try again</button>') };
  }
  ensureSettings(scope);
  return { loading: true, html: loadingBlock("Loading settings") };
}

function gameHeroHtml(game, id) {
  if (id === "@global") {
    return '<section class="detail-hero"><div class="detail-cover"><div class="cover-placeholder"><img src="assets/caliza-mark.svg" alt=""><span>All games</span></div></div><div class="detail-copy"><div class="detail-title"><h1 data-view-heading tabindex="-1">All games</h1><p>Defaults inherited by every game unless a game overrides them.</p></div><div class="detail-actions"><a class="button ghost" href="#/library" data-focus-id="back-library">' + icon("back") + 'Back to library</a></div></div></section>';
  }
  const cover = game && game.cover ? '<img src="' + coverUrl(id) + '" alt="">' : '<div class="cover-placeholder"><img src="assets/caliza-mark.svg" alt=""><span>' + esc(game ? game.title : "Game") + '</span></div>';
  const meta = game ? [game.serial, game.region, bytesHuman(game.size)].filter(Boolean).map(esc).join(" · ") : "";
  const playing = app.state && app.state.playing && app.state.playing.id === id;
  return '<section class="detail-hero"><div class="detail-cover">' + cover + '</div><div class="detail-copy"><div class="detail-title"><h1 data-view-heading tabindex="-1">' + esc(game ? game.title : "Unknown game") + '</h1><p>' + meta + (playing ? ' · <span class="high-contrast-note">playing now</span>' : "") + '</p></div>' +
    '<div class="badge-row">' + ((game && game.badges) || []).slice(0, 5).map((b) => '<span class="badge">' + esc(b) + '</span>').join("") + '</div>' +
    '<div class="detail-actions"><a class="button ghost" href="#/library" data-focus-id="back-library">' + icon("back") + 'Back</a><a class="button ghost" href="#/memory" data-focus-id="memory-link">' + icon("memory") + 'Memory cards</a>' + (playing ? '<a class="button ghost" href="#/discs" data-focus-id="disc-link">' + icon("disc") + 'Discs</a>' : "") + '</div>' +
    '<div class="scope-card"><div class="scope-copy"><strong>Settings scope</strong><span>Choose whether changes affect this game or act as defaults.</span></div><div class="segmented" role="group" aria-label="Settings scope"><button class="segment-button" type="button" aria-pressed="' + (editorScopeFor(id) === id ? "true" : "false") + '" data-action="editor-scope" data-scope="game" data-focus-id="scope-game">This game</button><button class="segment-button" type="button" aria-pressed="' + (editorScopeFor(id) === "@global" ? "true" : "false") + '" data-action="editor-scope" data-scope="@global" data-focus-id="scope-global">All games</button></div></div>' +
    '</div></section>';
}
function renderGameDetail(id) {
  const game = id === "@global" ? null : findGame(id);
  if (id !== "@global" && app.gamesLoaded && !game) {
    return '<section class="view">' + emptyState("library", "Game not found", "The console did not report this game in the current library scan. It may have been moved or the drive may be disconnected.", '<a class="button primary" href="#/library" data-focus-id="missing-back">' + icon("back") + 'Back to library</a><button class="button ghost" type="button" data-action="refresh-library" data-focus-id="missing-refresh">' + icon("refresh") + 'Refresh</button>') + '</section>';
  }
  const scope = editorScopeFor(id);
  const loaded = settingsBlock(scope);
  const tabs = [["settings","Settings"],["controls","Controls"],["achievements","Achievements"]].map(([tab,label]) =>
    '<button class="segment-button" type="button" role="tab" aria-selected="' + (app.activeTab === tab ? "true" : "false") + '" data-action="editor-tab" data-tab="' + tab + '" data-focus-id="tab-' + tab + '">' + label + '</button>').join("");
  let body = "";
  if (loaded.html) body = loaded.html;
  else if (app.activeTab === "achievements") body = renderAchievements(loaded, scope);
  else body = renderSettingsEditor(loaded, scope, app.activeTab);
  return '<section class="view">' + gameHeroHtml(game, id) + '<div class="tab-list" role="tablist" aria-label="Game editor sections">' + tabs + '</div>' + body + '</section>';
}

function renderAchievements(data, scope) {
  const playing = app.state && app.state.playing;
  if (!playing) return emptyState("controller", "No game is running", "Achievements are available only for the game currently running on the console.", '<a class="button ghost" href="#/library" data-focus-id="ach-back">' + icon("back") + 'Back to library</a>');
  if (playing.id !== scope || scope === "@global") {
    return emptyState("controller", "Open the running game", "Live achievements belong to the game currently running. Open that game's detail page to see them.", '<a class="button primary" href="#/game/' + enc(playing.id) + '" data-focus-id="ach-running">' + icon("play") + 'Open ' + esc(playing.title) + '</a>');
  }
  ensureAchievements();
  if (app.achievement.loading && !app.achievement.data) return loadingBlock("Loading achievements");
  if (app.achievement.error) return notice("error", "Achievements unavailable", esc(app.achievement.error), '<button class="button ghost" type="button" data-action="refresh-achievements" data-focus-id="refresh-achievements">' + icon("refresh") + 'Try again</button>');
  const snapshot = app.achievement.data;
  if (!snapshot) return loadingBlock("Loading achievements");
  const entries = Array.isArray(snapshot.entries) ? snapshot.entries : [];
  if (!entries.length) return emptyState("controller", "No achievements reported", esc(snapshot.message || "The console did not return published achievements for this game."), '<button class="button ghost" type="button" data-action="refresh-achievements" data-focus-id="refresh-achievements">' + icon("refresh") + 'Refresh</button>');
  const unlocked = entries.filter((entry) => entry.unlocked);
  const points = entries.reduce((sum, entry) => sum + (entry.points || 0), 0);
  const earned = unlocked.reduce((sum, entry) => sum + (entry.points || 0), 0);
  const filtered = entries.filter((entry) => app.achievement.filter === "all" || (app.achievement.filter === "unlocked") === !!entry.unlocked);
  const pages = Math.max(1, Math.ceil(filtered.length / 8));
  app.achievement.page = clamp(app.achievement.page, 0, pages - 1);
  const pageEntries = filtered.slice(app.achievement.page * 8, app.achievement.page * 8 + 8);
  let html = '<section class="panel panel-pad"><div class="achievement-summary"><div><strong>' + unlocked.length + ' / ' + entries.length + ' unlocked</strong><p class="lede">' + earned + ' / ' + points + ' points</p></div><div class="empty-actions"><button class="icon-button" type="button" data-action="refresh-achievements" data-focus-id="refresh-achievements">' + icon("refresh") + '<span>Refresh</span></button>' + ["all","unlocked","locked"].map((filter) => '<button class="chip" type="button" aria-pressed="' + (app.achievement.filter === filter ? "true" : "false") + '" data-action="achievement-filter" data-filter="' + filter + '" data-focus-id="ach-filter-' + filter + '">' + filter[0].toUpperCase() + filter.slice(1) + '</button>').join("") + '</div></div></section>';
  html += '<div class="achievement-list">' + pageEntries.map((entry) => '<article class="achievement-row"><div class="achievement-icon"><span>RA</span><img loading="lazy" src="/api/achievement-badge?id=' + enc(entry.id) + '" alt="" data-achievement-image></div><div><h3>' + esc(entry.title) + '</h3><p>' + esc(entry.description) + '</p><div class="achievement-status' + (entry.unlocked ? " earned" : "") + '">' + (entry.unlocked ? "Unlocked" : "Locked") + ' · ' + (entry.points || 0) + ' points</div></div></article>').join("") + '</div>';
  html += '<div class="pager-row"><button class="button small ghost" type="button" data-action="achievement-page" data-page="-1" data-focus-id="ach-prev" ' + (app.achievement.page === 0 ? "disabled" : "") + '>Previous</button><span>' + (app.achievement.page + 1) + ' / ' + pages + '</span><button class="button small ghost" type="button" data-action="achievement-page" data-page="1" data-focus-id="ach-next" ' + (app.achievement.page + 1 >= pages ? "disabled" : "") + '>Next</button></div>';
  return html;
}
function ensureAchievements(force = false) {
  if (app.achievement.loading && !force) return;
  app.achievement.loading = true;
  api("/api/achievements").then((snapshot) => {
    app.achievement.data = snapshot;
    app.achievement.error = "";
  }).catch((error) => {
    app.achievement.error = error.message || "Achievement data did not load";
  }).finally(() => {
    app.achievement.loading = false;
    if (app.route.name === "game" && app.activeTab === "achievements") render();
  });
}

function renderMemoryCards() {
  const scope = app.memoryScope;
  if (!app.cards && !app.cardsError) ensureCards();
  const scopeOptions = ['<option value="@global"' + (scope === "@global" ? " selected" : "") + '>All games</option>']
    .concat(app.games.map((g) => '<option value="' + esc(g.id) + '"' + (scope === g.id ? " selected" : "") + '>' + esc(g.title) + '</option>')).join("");
  let slots = "";
  const loaded = settingsBlock(scope);
  if (loaded.html) slots = loaded.html;
  else {
    slots = '<div class="slot-grid">' + SLOT_SETTINGS.map((slot) => {
      const fileEff = effectiveSetting(loaded, { key: slot.file, def: slot.def });
      const onEff = effectiveSetting(loaded, { key: slot.on, def: "true" });
      const own = fileEff.from === "own" || onEff.from === "own";
      const noCard = !truthy(onEff.value);
      const current = own ? (noCard ? "@none" : fileEff.value) : "";
      const inheritedFile = scope === "@global" ? slot.def : ((loaded.global && loaded.global[slot.file]) || slot.def);
      const inheritedOn = scope === "@global" ? "true" : ((loaded.global && loaded.global[slot.on]) || "true");
      const inherited = (scope === "@global" ? "Default" : "Same as all games") + " (" + (truthy(inheritedOn) ? inheritedFile : "no card") + ")";
      let opts = '<option value=""' + (current === "" ? " selected" : "") + '>' + esc(inherited) + '</option><option value="@none"' + (current === "@none" ? " selected" : "") + '>No card</option>';
      const cards = app.cards && Array.isArray(app.cards.cards) ? app.cards.cards : [];
      for (const card of cards) opts += '<option value="' + esc(card.name) + '"' + (card.name === current ? " selected" : "") + '>' + esc(card.name) + ' · ' + esc(cardSize(card.bytes)) + '</option>';
      if (current && current !== "@none" && !cards.some((card) => card.name === current)) opts += '<option value="' + esc(current) + '" selected>' + esc(current) + ' · not in memory card folder</option>';
      return '<div class="slot-card"><h3>Slot ' + slot.n + '</h3><p>' + (own ? (scope === "@global" ? "Set for all games" : "Set for this game") : "Inherited") + '</p><select class="select" data-action-input="memory-slot" data-slot="' + slot.n + '" data-focus-id="memory-slot-' + slot.n + '" aria-label="Memory card slot ' + slot.n + '">' + opts + '</select></div>';
    }).join("") + '</div>';
  }
  let cardList = "";
  if (app.cardsError) cardList = notice("error", "Memory cards did not load", esc(app.cardsError), '<button class="button ghost" type="button" data-action="refresh-cards" data-focus-id="refresh-cards">' + icon("refresh") + 'Try again</button>');
  else if (!app.cards) cardList = loadingBlock("Loading memory cards");
  else if (!app.cards.cards.length) cardList = '<p class="lede">No card files are present yet. Create a blank card below; the game or system browser formats it on first use.</p>';
  else cardList = '<div class="card-list">' + app.cards.cards.map((card) => '<div class="card-row"><div><div class="card-name">' + esc(card.name) + '</div><div class="card-meta">' + esc(cardSize(card.bytes)) + ' · ' + card.bytes + ' bytes</div></div><span class="badge">' + (card.name.endsWith(".ps2") ? "PS2" : "File") + '</span></div>').join("") + '</div>';
  const sizes = app.cards && app.cards.sizes ? app.cards.sizes : [8, 16, 32, 64];
  return '<section class="view"><div class="view-header"><div class="view-title"><h1 data-view-heading tabindex="-1">Memory cards</h1><p>Assign card files to slots and create blank cards. The API reports card files and capacity, not individual save contents.</p></div><div class="header-controls"><button class="icon-button" type="button" data-action="refresh-cards" data-focus-id="refresh-cards">' + icon("refresh") + '<span>Refresh</span></button></div></div>' +
    '<div class="scope-card panel panel-pad"><div class="scope-copy"><strong>Settings scope</strong><span>Slot assignments can be global or overridden for one game.</span></div><select class="select" data-action-input="memory-scope" data-focus-id="memory-scope" aria-label="Memory card settings scope">' + scopeOptions + '</select></div>' +
    '<div class="memory-layout"><div class="memory-primary"><section class="panel"><div class="panel-header"><div><h2>Slots</h2><p>Changes save through the same settings file as the game editor.</p></div></div><div class="panel-body">' + slots + '</div></section><section class="panel"><div class="panel-header"><div><h2>Card files</h2><p>Blank cards are written by the console.</p></div></div><div class="panel-body">' + cardList + '</div></section></div>' +
    '<aside class="memory-side"><section class="panel"><div class="panel-header"><div><h2>Create a blank card</h2><p>Most games expect 8 MB. Larger cards simply take longer to fill.</p></div></div><div class="panel-body"><div class="create-grid"><label>Card name<input class="input" id="memory-card-name" data-focus-id="memory-card-name" maxlength="40" autocomplete="off" placeholder="For example: RPG saves" value="' + esc(app.cardForm.name) + '"></label><div><div class="setting-label">Capacity</div><div class="size-row">' + sizes.map((size) => '<button class="chip" type="button" aria-pressed="' + (app.cardForm.size === size ? "true" : "false") + '" data-action="memory-size" data-size="' + size + '" data-focus-id="memory-size-' + size + '">' + size + ' MB</button>').join("") + '</div></div><button class="button primary" type="button" data-action="create-memory-card" data-focus-id="create-memory-card" ' + (app.cardForm.busy ? "disabled" : "") + '>' + icon("memory") + (app.cardForm.busy ? "Creating…" : "Create card") + '</button></div></div></section></aside></div></section>';
}
function ensureCards(force = false) {
  if (!force && app.cards) return;
  api("/api/memcards").then((cards) => {
    app.cards = cards;
    app.cardsError = "";
  }).catch((error) => {
    app.cardsError = error.message || "Memory cards could not be loaded";
  }).finally(() => {
    if (app.route.name === "memory") render();
  });
}
function memorySlotChoose(slotNumber, value) {
  const scope = app.memoryScope;
  const slot = SLOT_SETTINGS[slotNumber - 1];
  if (!slot) return;
  if (value === "") {
    setSetting(scope, slot.file, null, { deferRender: true });
    setSetting(scope, slot.on, null);
    return;
  }
  if (value === "@none") {
    setSetting(scope, slot.file, null, { deferRender: true });
    setSetting(scope, slot.on, "false");
    return;
  }
  setSetting(scope, slot.file, value, { deferRender: true });
  const data = app.settingsCache.get(scope);
  if (data && !truthy(effectiveSetting(data, { key: slot.on, def: "true" }).value)) setSetting(scope, slot.on, "true");
  else render();
}

function renderDiscs() {
  const playing = app.state && app.state.playing;
  let content = "";
  if (!playing) {
    content = emptyState("disc", "No game is running", "Disc changing is available while a game is running on the console. Start a game there, then return to this page.", '<a class="button ghost" href="#/library" data-focus-id="disc-back-library">' + icon("back") + 'Back to library</a>');
  } else {
    if (!app.discs && !app.discsError) ensureDiscs();
    if (app.discsError) content = notice("error", "Disc list did not load", esc(app.discsError), '<button class="button ghost" type="button" data-action="refresh-discs" data-focus-id="refresh-discs">' + icon("refresh") + 'Try again</button>');
    else if (!app.discs) content = loadingBlock("Loading disc images");
    else {
      const d = app.discs;
      let opts = "";
      if (d.set && d.set.length > 1) {
        opts += '<optgroup label="This game\'s discs">';
        d.set.forEach((disc, index) => { opts += '<option value="' + esc(disc.path) + '"' + (disc.path === d.current ? " selected" : "") + '>Disc ' + (index + 1) + (disc.path === d.current ? " (inserted)" : "") + ' · ' + esc(disc.name) + '</option>'; });
        opts += '</optgroup>';
      }
      opts += '<optgroup label="Library images">';
      for (const game of d.games || []) opts += '<option value="' + esc(game.path) + '"' + ((!d.set || d.set.length <= 1) && game.path === d.current ? " selected" : "") + '>' + esc(game.title) + (game.path === d.current ? " (inserted)" : "") + '</option>';
      opts += '</optgroup>';
      const rows = (d.set || []).map((disc, index) => '<div class="disc-row"><div><strong>Disc ' + (index + 1) + ' · ' + esc(disc.name) + '</strong><div class="path">' + esc(disc.path) + '</div></div>' + (disc.path === d.current ? '<span class="current-badge">Inserted</span>' : "") + '</div>').join("");
      content = nowPlayingHtml() + '<section class="panel"><div class="panel-header"><div><h2>Change disc</h2><p>The request is sent to the running game; it is not validated here beyond the console response.</p></div></div><div class="panel-body"><div class="disc-picker-row"><label>Disc image<select class="select" id="disc-picker" data-focus-id="disc-picker">' + opts + '</select></label><button class="button primary" type="button" data-action="change-disc" data-focus-id="change-disc">' + icon("disc") + 'Insert selected disc</button></div></div></section>' +
        (rows ? '<section class="panel"><div class="panel-header"><div><h2>Current disc set</h2><p>Images associated with the running game.</p></div></div><div class="panel-body"><div class="disc-list">' + rows + '</div></div></section>' : "");
    }
  }
  return '<section class="view"><div class="view-header"><div class="view-title"><h1 data-view-heading tabindex="-1">Discs</h1><p>Inspect the running game and insert another image known to the console.</p></div><div class="header-controls"><button class="icon-button" type="button" data-action="refresh-discs" data-focus-id="refresh-discs">' + icon("refresh") + '<span>Refresh</span></button></div></div>' + content + '</section>';
}
function ensureDiscs(force = false) {
  if (!force && app.discs) return;
  api("/api/discs").then((discs) => {
    app.discs = discs;
    app.discsError = "";
  }).catch((error) => {
    app.discsError = error.message || "Disc information could not be loaded";
  }).finally(() => {
    if (app.route.name === "discs") render();
  });
}

function renderDiagnostics() {
  const state = app.state || {};
  if (app.probe === undefined && !app.probeError) ensureProbe();
  const rows = [
    ["Build", state.build || "Not reported"],
    ["Mode", state.mode === "game" ? "Game running" : "Library / menu"],
    ["Now playing", state.playing ? state.playing.title : "Nothing"],
    ["Language", state.language || "Not reported"],
    ["Test build", state.test > 0 ? "Yes, test " + state.test : "No"],
  ];
  const stateHtml = '<dl class="status-list">' + rows.map(([k,v]) => '<div class="status-row"><dt>' + esc(k) + '</dt><dd>' + esc(v) + '</dd></div>').join("") + '</dl>';
  let probeHtml = "";
  if (app.probeError) probeHtml = notice("warn", "Probe unavailable", esc(app.probeError) + " This endpoint exists only in builds that expose a probe session.", '<button class="button ghost" type="button" data-action="refresh-probe" data-focus-id="refresh-probe">' + icon("refresh") + 'Try again</button>');
  else if (!app.probe) probeHtml = loadingBlock("Loading probe");
  else {
    const probeRows = [["Build", app.probe.build],["Session", app.probe.session],["Mode", app.probe.mode],["VM running", app.probe.vm_running ? "Yes" : "No"],["Game sequence", app.probe.game_sequence],["Frames", app.probe.frames],["Library frames", app.probe.shelf_frames]];
    probeHtml = '<dl class="status-list">' + probeRows.map(([k,v]) => '<div class="status-row"><dt>' + esc(k) + '</dt><dd>' + esc(v ?? "Not reported") + '</dd></div>').join("") + '</dl><div class="probe-actions"><button class="button small ghost" type="button" data-action="refresh-probe" data-focus-id="refresh-probe">' + icon("refresh") + 'Refresh probe</button></div>';
  }
  return '<section class="view"><div class="view-header"><div class="view-title"><h1 data-view-heading tabindex="-1">Diagnostics</h1><p>Live status, probe evidence and the console report download.</p></div></div>' +
    '<div class="diagnostics-grid"><section class="panel"><div class="panel-header"><div><h2>Console status</h2><p>Reported by the live state endpoint.</p></div></div><div class="panel-body">' + stateHtml + '</div></section>' +
    '<section class="panel"><div class="panel-header"><div><h2>Runtime probe</h2><p>Optional instrumentation exposed by probe-enabled builds.</p></div></div><div class="panel-body">' + probeHtml + '</div></section>' +
    '<section class="panel"><div class="panel-header"><div><h2>Report a problem</h2><p>Add a short note, then download the report with logs and settings evidence.</p></div></div><div class="panel-body"><div class="create-grid"><label for="problem-note">What happened?</label><textarea class="textarea" id="problem-note" data-focus-id="problem-note" maxlength="1500" placeholder="Example: froze after the first cutscene; audio continued"></textarea><div class="report-actions"><button class="button ghost" type="button" data-action="send-note" data-focus-id="send-note">' + icon("note") + 'Add note</button><button class="button primary" type="button" data-action="download-report" data-focus-id="download-report">' + icon("download") + 'Download report</button></div><p class="lede">The note is included beside the console logs with its timestamp. The download is generated by the console.</p></div></div></section>' +
    '<section class="panel"><div class="panel-header"><div><h2>Panel endpoints</h2><p>Technical compatibility surface used by this static panel.</p></div></div><div class="panel-body"><ul class="endpoint-list"><li><code>/api/state</code>, <code>/api/probe</code>, <code>/api/games</code>, <code>/api/cover</code></li><li><code>/api/settings</code> GET/POST, <code>/api/memcards</code> GET/POST</li><li><code>/api/discs</code>, <code>/api/disc</code>, <code>/api/note</code>, <code>/api/report</code></li><li><code>/api/achievements</code>, <code>/api/achievement-badge</code> when available</li></ul></div></section></div></section>';
}
function ensureProbe(force = false) {
  if (!force && app.probe !== undefined) return;
  api("/api/probe", {}, 5000).then((probe) => {
    app.probe = probe;
    app.probeError = "";
  }).catch((error) => {
    app.probe = null;
    app.probeError = error.message || "Probe did not answer";
  }).finally(() => {
    if (app.route.name === "diagnostics") render();
  });
}

function setSetting(scope, key, value, options = {}) {
  const data = app.settingsCache.get(scope);
  if (!data) return;
  const it = findSettingItem(key);
  const manualStarts = value !== null && scope !== "@global" && it && (it.fix || (key === "UserHacks" && truthy(value))) && !manualFixesOn(data);
  if (value === null) delete data.values[key];
  else data.values[key] = String(value);
  pendingFor(scope).set(key, value);
  if (manualStarts) {
    if (key !== "UserHacks") {
      data.values.UserHacks = "true";
      pendingFor(scope).set("UserHacks", "true");
    }
    for (const [fixKey, fixValue] of Object.entries(data.gamefixes || {})) {
      if (fixKey !== key && data.values[fixKey] === undefined) {
        data.values[fixKey] = fixValue;
        pendingFor(scope).set(fixKey, fixValue);
      }
    }
  }
  saveStatus("Unsaved changes");
  if (!options.deferRender) render();
  queueSave(scope);
}
function setPatch(data, scope, kind, name, on) {
  const list = kind === "patch" ? data.patches : data.cheats;
  const entry = (list || []).find((x) => x.name === name);
  if (entry) entry.enabled = on;
  pendingFor(scope).set(kind + ":" + name, on);
  if (kind === "cheat") {
    if (on) {
      data.values["EmuCore/EnableCheats"] = "true";
      pendingFor(scope).set("EmuCore/EnableCheats", "true");
    } else if (!(data.cheats || []).some((x) => x.enabled)) {
      delete data.values["EmuCore/EnableCheats"];
      pendingFor(scope).set("EmuCore/EnableCheats", null);
    }
  }
  saveStatus("Unsaved changes");
  render();
  queueSave(scope);
}
function queueSave(scope) {
  clearTimeout(app.saveTimers.get(scope));
  app.saveTimers.set(scope, setTimeout(() => flushSave(scope), 420));
}
async function flushSave(scope) {
  if (app.saveInFlight.has(scope)) return;
  const pending = pendingFor(scope);
  if (!pending.size) return;
  const sent = Array.from(pending.entries());
  pending.clear();
  app.saveInFlight.add(scope);
  saveStatus("Saving…");
  const lines = [];
  for (const [key, value] of sent) {
    if (key.startsWith("patch:")) lines.push((value ? "patch+ " : "patch- ") + key.slice(6));
    else if (key.startsWith("cheat:")) lines.push((value ? "cheat+ " : "cheat- ") + key.slice(6));
    else if (value === null) lines.push("unset " + key);
    else lines.push("set " + key + "=" + value);
  }
  try {
    const data = await api("/api/settings?id=" + enc(scope), { method: "POST", body: lines.join("\n") + "\n", headers: apiText }, 15000);
    // Merge changes that arrived while this write was in flight; they are saved by the next flush.
    const remaining = pendingFor(scope);
    for (const [key, value] of remaining) {
      if (key.includes(":")) continue;
      if (value === null) delete data.values[key];
      else data.values[key] = String(value);
    }
    app.settingsCache.set(scope, data);
    app.settingsErrors.delete(scope);
    saveStatus(remaining.size ? "Saving…" : "Saved");
    toast("Settings saved");
    refreshGamesQuietly();
  } catch (error) {
    const merged = pendingFor(scope);
    for (let i = sent.length - 1; i >= 0; i--) {
      const [key, value] = sent[i];
      if (!merged.has(key)) merged.set(key, value);
    }
    saveStatus("Save failed");
    toast("Settings not saved: " + error.message);
  } finally {
    app.saveInFlight.delete(scope);
    if (pendingFor(scope).size) queueSave(scope);
    if (settingsInView() === scope || app.route.name === "memory") render();
  }
}
async function applyRecommended(scope) {
  saveStatus("Applying recommended…");
  try {
    const data = await api("/api/settings?id=" + enc(scope), { method: "POST", body: "recommended\n", headers: apiText }, 15000);
    app.settingsCache.set(scope, data);
    pendingFor(scope).clear();
    saveStatus("Saved");
    toast("Recommended settings applied");
    render();
  } catch (error) {
    saveStatus("Save failed");
    toast("Recommended settings not applied: " + error.message);
  }
}
function resetControls(scope) {
  const data = app.settingsCache.get(scope);
  if (!data) return;
  for (const it of GROUPS.filter((g) => g.tab === "controls").flatMap((g) => g.items)) {
    if (data.values[it.key] !== undefined) {
      delete data.values[it.key];
      pendingFor(scope).set(it.key, null);
    }
  }
  saveStatus("Unsaved changes");
  render();
  queueSave(scope);
}

async function refreshGamesQuietly() {
  try {
    app.games = await api("/api/games");
    app.gamesLoaded = true;
  } catch (error) {}
}
async function refreshLibrary(showErrors = true) {
  try {
    app.games = await api("/api/games");
    app.gamesLoaded = true;
    app.games.forEach((g) => {
      if (app.settingsCache.has(g.id)) {
        // A badge may have changed; the settings objects themselves remain valid.
      }
    });
    setConnection("online", "Connected");
  } catch (error) {
    if (showErrors) toast("Library did not refresh: " + error.message);
  }
  render();
}
async function refreshState() {
  try {
    const next = await api("/api/state", {}, 6000);
    const before = JSON.stringify(app.state && { playing: app.state.playing, mode: app.state.mode, build: app.state.build, test: app.state.test });
    const after = JSON.stringify({ playing: next.playing, mode: next.mode, build: next.build, test: next.test });
    const playingChanged = before !== after;
    app.state = next;
    setConnection("online", "Connected");
    if (playingChanged) {
      app.discs = null;
      app.achievement.data = null;
      await refreshGamesQuietly();
      render();
    }
  } catch (error) {
    setConnection("offline", "Offline");
    if (!app.state) render();
  }
}
async function bootstrap() {
  if (app.booted) return;
  updateClock();
  setInterval(updateClock, 10000);
  parseRoute();
  render();
  try {
    const [state, games] = await Promise.all([api("/api/state", {}, 8000), api("/api/games", {}, 10000)]);
    app.state = state;
    app.games = games;
    app.gamesLoaded = true;
    app.booted = true;
    setConnection("online", "Connected");
  } catch (error) {
    app.booted = true;
    setConnection("offline", "Offline");
  }
  render({ preserveFocus: false, focusMain: true });
  clearInterval(app.pollTimer);
  app.pollTimer = setInterval(refreshState, 5000);
  clearInterval(app.achievementTimer);
  app.achievementTimer = setInterval(() => {
    if (app.route.name === "game" && app.activeTab === "achievements") ensureAchievements();
  }, 7000);
}

function bindDynamic(root) {
  root.querySelectorAll("[data-action]").forEach((el) => {
    el.addEventListener("click", (event) => {
      const action = el.dataset.action;
      if (action === "open-game") navigate("/game/" + enc(el.dataset.id));
      else if (action === "refresh-library") refreshLibrary();
      else if (action === "retry-connection") { app.booted = false; app.state = null; setConnection("connecting", "Connecting"); bootstrap(); }
      else if (action === "filter-mode") { app.filters.mode = el.dataset.mode; render(); }
      else if (action === "clear-filters") { app.filters.query = ""; app.filters.mode = "all"; render(); }
      else if (action === "editor-scope") {
        const id = currentGameId();
        if (id) { app.editorScopes.set(id, el.dataset.scope === "@global" ? "@global" : id); render(); }
      }
      else if (action === "editor-tab") { app.activeTab = el.dataset.tab; app.achievement.page = 0; render(); if (app.activeTab === "achievements") ensureAchievements(true); }
      else if (action === "retry-settings") { const scope = settingsInView(); if (scope) { app.settingsErrors.delete(scope); ensureSettings(scope); render(); } }
      else if (action === "toggle-setting" || action === "choice-setting") { const scope = settingsInView(); if (scope) setSetting(scope, el.dataset.key, el.dataset.value); }
      else if (action === "reset-setting") {
        const scope = settingsInView();
        if (scope) {
          if (el.dataset.key2) setSetting(scope, el.dataset.key2, null, { deferRender: true });
          setSetting(scope, el.dataset.key, null);
        }
      }
      else if (action === "toggle-patch" || action === "toggle-cheat") {
        const scope = settingsInView();
        const data = scope && app.settingsCache.get(scope);
        if (data) setPatch(data, scope, action === "toggle-patch" ? "patch" : "cheat", el.dataset.name, el.dataset.value === "true");
      }
      else if (action === "apply-recommended") {
        const scope = settingsInView();
        if (!scope) return;
        const key = "recommended-" + scope;
        if (app.confirmations.get(key)) { app.confirmations.delete(key); applyRecommended(scope); }
        else {
          app.confirmations.set(key, true);
          setTimeout(() => { app.confirmations.delete(key); if (settingsInView() === scope) render(); }, 3800);
          toast("Confirm to replace this settings file");
          render();
        }
      }
      else if (action === "reset-controls") {
        const scope = settingsInView();
        if (!scope) return;
        const key = "controls-reset-" + scope;
        if (app.confirmations.get(key)) { app.confirmations.delete(key); resetControls(scope); }
        else {
          app.confirmations.set(key, true);
          setTimeout(() => { app.confirmations.delete(key); if (settingsInView() === scope) render(); }, 3800);
          toast("Confirm the control reset");
          render();
        }
      }
      else if (action === "memory-size") { app.cardForm.size = Number(el.dataset.size); render(); }
      else if (action === "create-memory-card") createMemoryCard();
      else if (action === "refresh-cards") { app.cards = null; app.cardsError = ""; ensureCards(true); render(); }
      else if (action === "refresh-discs") { app.discs = null; app.discsError = ""; ensureDiscs(true); render(); }
      else if (action === "change-disc") changeDisc();
      else if (action === "refresh-probe") { app.probe = undefined; app.probeError = ""; ensureProbe(true); render(); }
      else if (action === "send-note") sendNote();
      else if (action === "download-report") downloadReport();
      else if (action === "refresh-achievements") ensureAchievements(true);
      else if (action === "achievement-filter") { app.achievement.filter = el.dataset.filter; app.achievement.page = 0; render(); }
      else if (action === "achievement-page") { app.achievement.page += Number(el.dataset.page); render(); }
      event.preventDefault();
    });
  });
  root.querySelectorAll("[data-action-input]").forEach((el) => {
    el.addEventListener("change", () => {
      const action = el.dataset.actionInput;
      if (action === "pick-setting" || action === "text-setting") {
        const scope = settingsInView();
        if (!scope) return;
        const value = el.value.trim();
        if (action === "text-setting" && value.includes("#")) { toast("A # cannot be used in this path"); render(); return; }
        setSetting(scope, el.dataset.key, action === "text-setting" && value === "" ? null : value);
      } else if (action === "memory-slot") memorySlotChoose(Number(el.dataset.slot), el.value);
      else if (action === "memory-scope") { app.memoryScope = el.value; ensureSettings(el.value); render(); }
    });
  });
  const search = root.querySelector("#library-search");
  if (search) search.addEventListener("input", () => { app.filters.query = search.value; render(); });
  const cardName = root.querySelector("#memory-card-name");
  if (cardName) cardName.addEventListener("input", () => { app.cardForm.name = cardName.value; });
  root.querySelectorAll("[data-achievement-image]").forEach((img) => img.addEventListener("error", () => { img.remove(); }));
  root.querySelectorAll("[data-now-cover]").forEach((img) => img.addEventListener("error", () => { img.style.visibility = "hidden"; }));
}
async function createMemoryCard() {
  const name = app.cardForm.name.trim();
  if (!name) { toast("Give the card a name first"); const input = $("memory-card-name"); if (input) input.focus(); return; }
  app.cardForm.busy = true;
  render();
  try {
    const cards = await api("/api/memcards", { method: "POST", body: "create " + app.cardForm.size + " " + name, headers: apiText }, 20000);
    app.cards = cards;
    app.cardForm.name = "";
    toast("Memory card created");
  } catch (error) {
    toast("Card not created: " + error.message);
  } finally {
    app.cardForm.busy = false;
    render();
  }
}
async function changeDisc() {
  const picker = $("disc-picker");
  if (!picker || !picker.value) return;
  try {
    await api("/api/disc", { method: "POST", body: picker.value, headers: apiText }, 10000);
    toast("Disc change requested");
    app.discs = null;
    setTimeout(() => ensureDiscs(true), 1200);
  } catch (error) {
    toast("Disc not changed: " + error.message);
  }
}
async function sendNote() {
  const area = $("problem-note");
  const text = area ? area.value.trim() : "";
  if (!text) { toast("Write a note first"); if (area) area.focus(); return false; }
  try {
    await api("/api/note", { method: "POST", body: text, headers: apiText }, 10000);
    area.value = "";
    toast("Note added to the report log");
    return true;
  } catch (error) {
    toast("The note did not reach the console: " + error.message);
    return false;
  }
}
async function downloadReport() {
  const area = $("problem-note");
  if (area && area.value.trim()) await sendNote();
  toast("Preparing the report download");
  location.href = "/api/report";
}

// Keyboard and controller navigation. Pointer input remains native; these helpers add spatial movement.
function focusableElements() {
  return Array.from(document.querySelectorAll('button:not(:disabled), a[href], input:not(:disabled), select:not(:disabled), textarea:not(:disabled), [tabindex]:not([tabindex="-1"])'))
    .filter((el) => el.offsetParent !== null || el === document.activeElement);
}
function isEditable(el) {
  return el && (el.tagName === "INPUT" || el.tagName === "TEXTAREA" || el.tagName === "SELECT" || el.isContentEditable);
}
function moveFocus(dx, dy) {
  const items = focusableElements();
  if (!items.length) return false;
  const current = document.activeElement && items.includes(document.activeElement) ? document.activeElement : null;
  if (!current) { items[0].focus(); return true; }
  const rect = current.getBoundingClientRect();
  const cx = rect.left + rect.width / 2, cy = rect.top + rect.height / 2;
  let best = null, bestScore = Infinity;
  for (const item of items) {
    if (item === current) continue;
    const r = item.getBoundingClientRect();
    const x = r.left + r.width / 2, y = r.top + r.height / 2;
    const vx = x - cx, vy = y - cy;
    if (dx && vx * dx <= 4) continue;
    if (dy && vy * dy <= 4) continue;
    const primary = dx ? Math.abs(vx) : Math.abs(vy);
    const orthogonal = dx ? Math.abs(vy) : Math.abs(vx);
    const score = primary + orthogonal * 1.9;
    if (score < bestScore) { bestScore = score; best = item; }
  }
  if (best) { best.focus(); return true; }
  return false;
}
document.addEventListener("keydown", (event) => {
  if (event.defaultPrevented) return;
  if (event.key === "Escape") { navigateBack(); event.preventDefault(); return; }
  const arrows = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
  if (!arrows[event.key]) return;
  if (isEditable(document.activeElement)) return;
  const [dx, dy] = arrows[event.key];
  if (moveFocus(dx, dy)) event.preventDefault();
});
function cycleSection(delta) {
  const order = ["library", "memory", "discs", "diagnostics"];
  const current = app.route.name === "game" ? "library" : app.route.name;
  const next = order[(order.indexOf(current) + delta + order.length) % order.length];
  navigate("/" + next);
}
function refreshCurrent() {
  if (app.route.name === "library") refreshLibrary();
  else if (app.route.name === "memory") { app.cards = null; ensureCards(true); }
  else if (app.route.name === "discs") { app.discs = null; ensureDiscs(true); }
  else if (app.route.name === "diagnostics") { app.probe = undefined; ensureProbe(true); }
  else if (settingsInView()) ensureSettings(settingsInView());
}
function activateFocused() {
  const el = document.activeElement;
  if (!el) { moveFocus(1, 0); return; }
  if (isEditable(el) && el.tagName !== "SELECT") return;
  el.click();
}
const padPrevious = new Map();
let padMoveAt = 0;
function pollGamepads(time) {
  const pads = navigator.getGamepads ? Array.from(navigator.getGamepads()).filter(Boolean) : [];
  const pad = pads[0];
  if (pad) {
    if (!app.gamepadSeen) {
      app.gamepadSeen = true;
      const help = $("input-help-text");
      if (help) help.textContent = "D-pad moves · Cross selects · Circle goes back · L1/R1 change section";
    }
    const pressed = (index) => !!(pad.buttons[index] && pad.buttons[index].pressed);
    const edge = (index) => {
      const key = pad.index + ":" + index;
      const was = padPrevious.get(key) === true;
      const is = pressed(index);
      padPrevious.set(key, is);
      return is && !was;
    };
    const axisX = Math.abs(pad.axes[0] || 0) > 0.55 ? Math.sign(pad.axes[0]) : 0;
    const axisY = Math.abs(pad.axes[1] || 0) > 0.55 ? Math.sign(pad.axes[1]) : 0;
    const dx = pressed(15) ? 1 : pressed(14) ? -1 : axisX;
    const dy = pressed(13) ? 1 : pressed(12) ? -1 : axisY;
    if ((dx || dy) && time >= padMoveAt) {
      moveFocus(dx, dy);
      padMoveAt = time + 190;
    }
    if (edge(0)) activateFocused();       // Cross / bottom button in the standard mapping
    if (edge(1)) navigateBack();          // Circle / right button
    if (edge(3)) refreshCurrent();        // Triangle / top button
    if (edge(4)) cycleSection(-1);        // L1
    if (edge(5)) cycleSection(1);         // R1
  } else if (app.gamepadSeen) {
    app.gamepadSeen = false;
    const help = $("input-help-text");
    if (help) help.textContent = "Arrows move · Enter selects · Esc goes back";
  }
  requestAnimationFrame(pollGamepads);
}

// Panel display preferences, local to this browser.
const PREFS_KEY = "caliza.panelPreferences";
const prefs = { textScale: 100, contrast: "auto", motion: "auto" };
function loadPrefs() {
  try {
    const saved = JSON.parse(localStorage.getItem(PREFS_KEY) || "{}");
    if ([100, 125, 150].includes(Number(saved.textScale))) prefs.textScale = Number(saved.textScale);
    if (["auto", "high"].includes(saved.contrast)) prefs.contrast = saved.contrast;
    if (["auto", "reduced"].includes(saved.motion)) prefs.motion = saved.motion;
  } catch (error) {}
}
function applyPrefs() {
  document.documentElement.style.fontSize = prefs.textScale === 100 ? "" : prefs.textScale + "%";
  if (prefs.contrast === "high") document.documentElement.dataset.contrast = "high";
  else delete document.documentElement.dataset.contrast;
  if (prefs.motion === "reduced") document.documentElement.dataset.motion = "reduced";
  else delete document.documentElement.dataset.motion;
  try { localStorage.setItem(PREFS_KEY, JSON.stringify(prefs)); } catch (error) {}
  const form = $("display-form");
  if (form) {
    const scale = form.querySelector('input[name="textScale"][value="' + prefs.textScale + '"]');
    const contrast = form.querySelector('input[name="contrast"][value="' + prefs.contrast + '"]');
    const motion = form.querySelector('input[name="motion"][value="' + prefs.motion + '"]');
    if (scale) scale.checked = true;
    if (contrast) contrast.checked = true;
    if (motion) motion.checked = true;
  }
}
function bindStatic() {
  const displayButton = $("display-preferences");
  const dialog = $("display-dialog");
  const form = $("display-form");
  if (displayButton && dialog) {
    displayButton.addEventListener("click", () => {
      applyPrefs();
      if (typeof dialog.showModal === "function") dialog.showModal();
      else dialog.setAttribute("open", "");
      const checked = dialog.querySelector("input:checked");
      if (checked) checked.focus();
    });
  }
  if (form) {
    form.addEventListener("change", () => {
      const data = new FormData(form);
      prefs.textScale = Number(data.get("textScale")) || 100;
      prefs.contrast = String(data.get("contrast") || "auto");
      prefs.motion = String(data.get("motion") || "auto");
      applyPrefs();
    });
  }
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) refreshState();
  });
}

loadPrefs();
applyPrefs();
bindStatic();
requestAnimationFrame(pollGamepads);

window.CALIZA = {
  start: bootstrap,
  render,
  app,
  api,
  navigate,
  refreshLibrary,
  flushSave,
  _test: { effectiveSetting, optionLabel, bytesHuman, cardSize, parseRoute },
};
if (!TEST_MODE) bootstrap();
})();
