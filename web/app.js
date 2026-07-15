/* Anytone Codeplug Studio — frontend */

const MODE = ["Analog", "Digital", "A+D TX A", "D+A TX D"];
const POWER = ["Low", "Mid", "High", "Turbo"];
const CALL_TYPE = ["Private", "Group", "All"];

let state = {
  codeplug: emptyCodeplug(),
  schema: null,
  imageLoaded: false,
  view: "dashboard",
  optionalSection: null,
  aprsTab: "identity",
  gpsTab: "receiver",
  roamingTab: "channels",
  hotkeysTab: "side",
  signalingTab: "dtmf",
  encryptionTab: "dmr",
  selected: {
    channels: -1, contacts: -1, zones: -1, "scan-lists": -1, "radio-ids": -1, "rx-groups": -1,
    "aprs-filters": 0, "aprs-zones": 0,
    "roam-ch": 0, "roam-zone": 0, hotkeys: 0,
    "dtmf-contacts": 0, "tone2-id": 0, "tone2-fn": 0, "tone5-id": 0, "tone5-fn": 0,
    "enc-dmr": 0, "enc-arc4": 0,
  },
  dirty: false,
};

function emptyCodeplug() {
  return {
    model: "",
    fw_version: "",
    bands: 0,
    schema_firmware: "",
    settings: { intro_line1: "", intro_line2: "" },
    optional_settings: {},
    radio_ids: [],
    contacts: [],
    channels: [],
    zones: [],
    scan_lists: [],
    rx_groups: [],
  };
}

async function api(method, path, body) {
  const opts = { method, headers: {} };
  if (body !== undefined) {
    opts.headers["Content-Type"] = "application/json";
    opts.body = typeof body === "string" ? body : JSON.stringify(body);
  }
  const res = await fetch(path, opts);
  const text = await res.text();
  let data;
  try { data = JSON.parse(text); } catch { data = { raw: text }; }
  if (!res.ok) throw new Error(data.error || data.raw || res.statusText);
  return data;
}

function setStatus(msg) {
  document.getElementById("status").textContent = msg;
}

function markDirty() {
  state.dirty = true;
  updateStatusLine();
}

function updateStatusLine() {
  const cp = state.codeplug;
  const bits = [];
  if (cp.model) bits.push(cp.model);
  if (cp.fw_version) bits.push(cp.fw_version);
  if (cp.schema_firmware) bits.push(`map ${cp.schema_firmware}`);
  bits.push(`${cp.channels.length} ch`);
  bits.push(`${cp.contacts.length} contacts`);
  bits.push(`${cp.zones.length} zones`);
  bits.push(`${(cp.rx_groups || []).length} rxg`);
  if (state.dirty) bits.push("unsaved edits");
  setStatus(bits.filter(Boolean).join(" · ") || "No codeplug loaded");
}

function nextIndex(list) {
  if (!list.length) return 0;
  return Math.max(...list.map((x) => x.index)) + 1;
}

/** Remap channel slot numbers inside zones / scan lists. */
function applyChannelIndexRemap(oldToNew) {
  if (!oldToNew.size) return;
  const map = (v) => (oldToNew.has(v) ? oldToNew.get(v) : v);
  for (const z of state.codeplug.zones || []) {
    z.channels = (z.channels || []).map(map);
  }
  for (const s of state.codeplug.scan_lists || []) {
    s.members = (s.members || []).map(map);
  }
}

/** Bump every channel slot >= minIndex by +1 (and refs). */
function bumpChannelSlotsFrom(minIndex) {
  const oldToNew = new Map();
  for (const ch of state.codeplug.channels) {
    if (ch.index >= minIndex) oldToNew.set(ch.index, ch.index + 1);
  }
  for (const ch of state.codeplug.channels) {
    const neu = oldToNew.get(ch.index);
    if (neu !== undefined) ch.index = neu;
  }
  applyChannelIndexRemap(oldToNew);
}

/** Swap two channel array rows and their radio slots; keep zone/scan refs valid. */
function moveChannel(arrIdx, delta) {
  const list = state.codeplug.channels;
  const j = arrIdx + delta;
  if (arrIdx < 0 || j < 0 || j >= list.length) return false;
  const a = list[arrIdx];
  const b = list[j];
  const ia = a.index;
  const ib = b.index;
  list[arrIdx] = b;
  list[j] = a;
  a.index = ib;
  b.index = ia;
  const swap = (v) => (v === ia ? ib : v === ib ? ia : v);
  for (const z of state.codeplug.zones || []) {
    z.channels = (z.channels || []).map(swap);
  }
  for (const s of state.codeplug.scan_lists || []) {
    s.members = (s.members || []).map(swap);
  }
  state.selected.channels = j;
  return true;
}

function newBlankChannel(index) {
  return {
    index,
    name: "New CH",
    rx_mhz: 146.520,
    tx_mhz: 146.520,
    mode: 0,
    power: 2,
    bandwidth_wide: false,
    color_code: 1,
    timeslot: 1,
    contact_index: -1,
    radio_id_index: -1,
    scan_list_index: -1,
    rx_group_index: -1,
    rx_only: false,
    admit: 0,
    rx_ctcss: 0,
    tx_ctcss: 0,
    rx_dcs: 0,
    tx_dcs: 0,
  };
}

/** Insert a new channel after the selected row (or at end). */
function insertChannelAfterSelected() {
  const list = state.codeplug.channels;
  const sel = state.selected.channels;
  if (sel < 0 || sel >= list.length) {
    const index = nextIndex(list);
    list.push(newBlankChannel(index));
    state.selected.channels = list.length - 1;
    return;
  }
  const newIndex = list[sel].index + 1;
  bumpChannelSlotsFrom(newIndex);
  list.splice(sel + 1, 0, newBlankChannel(newIndex));
  state.selected.channels = sel + 1;
}

function bumpZoneSlotsFrom(minIndex) {
  for (const z of state.codeplug.zones) {
    if (z.index >= minIndex) z.index += 1;
  }
}

function moveZone(arrIdx, delta) {
  const list = state.codeplug.zones;
  const j = arrIdx + delta;
  if (arrIdx < 0 || j < 0 || j >= list.length) return false;
  const a = list[arrIdx];
  const b = list[j];
  const ia = a.index;
  const ib = b.index;
  list[arrIdx] = b;
  list[j] = a;
  a.index = ib;
  b.index = ia;
  state.selected.zones = j;
  return true;
}

function insertZoneAfterSelected() {
  const list = state.codeplug.zones;
  const sel = state.selected.zones;
  if (sel < 0 || sel >= list.length) {
    const index = nextIndex(list);
    list.push({ index, name: "Zone", channels: [] });
    state.selected.zones = list.length - 1;
    return;
  }
  const newIndex = list[sel].index + 1;
  bumpZoneSlotsFrom(newIndex);
  list.splice(sel + 1, 0, { index: newIndex, name: "Zone", channels: [] });
  state.selected.zones = sel + 1;
}

function mhz(n) {
  return Number(n).toFixed(5).replace(/0+$/, "").replace(/\.$/, ".0");
}

/* ---------- views ---------- */

function render() {
  const el = document.getElementById("content");
  const v = state.view;
  if (v === "dashboard") el.innerHTML = viewDashboard();
  else if (v === "radio-ids") el.innerHTML = viewRadioIds();
  else if (v === "settings") el.innerHTML = viewSettings();
  else if (v === "optional") el.innerHTML = viewOptional();
  else if (v === "gps") el.innerHTML = viewGps();
  else if (v === "aprs") el.innerHTML = viewAprs();
  else if (v === "roaming") el.innerHTML = viewRoaming();
  else if (v === "hotkeys") el.innerHTML = viewHotkeys();
  else if (v === "signaling") el.innerHTML = viewSignaling();
  else if (v === "encryption") el.innerHTML = viewEncryption();
  else if (v === "contacts") el.innerHTML = viewContacts();
  else if (v === "rx-groups") el.innerHTML = viewRxGroups();
  else if (v === "channels") el.innerHTML = viewChannels();
  else if (v === "zones") el.innerHTML = viewZones();
  else if (v === "scan-lists") el.innerHTML = viewScanLists();
  bindViewHandlers();
  updateStatusLine();
}

function viewDashboard() {
  const cp = state.codeplug;
  return `
    <div class="panel-head">
      <div>
        <h1>Overview</h1>
        <p>Program the radio by category — same building blocks as Anytone CPS:
        talk groups first, then channels, then zones and scan lists.</p>
      </div>
    </div>
    <div class="stats">
      <div class="stat"><div class="n">${cp.radio_ids.length}</div><div class="l">Radio IDs</div></div>
      <div class="stat"><div class="n">${cp.contacts.length}</div><div class="l">Talk groups / contacts</div></div>
      <div class="stat"><div class="n">${cp.channels.length}</div><div class="l">Channels</div></div>
      <div class="stat"><div class="n">${cp.zones.length}</div><div class="l">Zones</div></div>
      <div class="stat"><div class="n">${cp.scan_lists.length}</div><div class="l">Scan lists</div></div>
    </div>
    <div class="empty">
      ${cp.model
        ? `<strong>${cp.model}</strong> firmware ${cp.fw_version || "?"} — pick a category on the left to edit.`
        : "Read from the radio or open a <span class='mono'>.atcp</span> backup to begin."}
    </div>`;
}

function viewSettings() {
  const s = state.codeplug.settings;
  return `
    <div class="panel-head">
      <div>
        <h1>Boot &amp; Display</h1>
        <p>Startup text. Full optional settings (CPS 4.00) are under <strong>Optional Settings</strong>.</p>
      </div>
    </div>
    <form class="form" id="settings-form" style="max-width:32rem">
      <label>Intro line 1
        <input name="intro_line1" maxlength="16" value="${esc(s.intro_line1)}" />
      </label>
      <label>Intro line 2
        <input name="intro_line2" maxlength="16" value="${esc(s.intro_line2)}" />
      </label>
      <div class="form-actions">
        <button class="btn primary" type="submit">Apply</button>
      </div>
    </form>`;
}

function schemaElement(name) {
  if (!state.schema?.elements) return null;
  return state.schema.elements.find((e) => e.name === name) || null;
}

function viewOptional() {
  const opt = state.codeplug.optional_settings || {};
  const hide = new Set([
    "APRS settings", "DMR APRS message", "APRS filter", "GPS roaming zone",
    "Roaming channel", "Roaming zone",
    "Hot-Key Setting",
    "DTMF Settings", "DTMF Contact",
    "2-Tone Settings", "2-Tone Id", "Two-Tone function", "2-Tone Id bitmap", "2-Tone function bitmap",
    "5-Tone settings", "5-tone ID", "5-Tone function", "5-Tone id bitmap",
    "DMR Encryption Key", "ARC4 encryption key", "ARC4 key bitmap",
    "AES encryption key bank", "AES encryption key bitmap",
    "Alarm Settings", "DMR Alarm Extension",
  ]);
  let names = Object.keys(opt).filter((k) => {
    if (hide.has(k)) return false;
    const v = opt[k];
    return v && typeof v === "object" && !Array.isArray(v);
  });
  if (!names.length) {
    names = (state.schema?.categories?.find((c) => c.id === "optional_settings")?.elements || [])
      .filter((n) => !hide.has(n));
  }
  if (!names.length) {
    return `<div class="panel-head"><div><h1>Optional Settings</h1>
      <p>Read the radio to load FW 4.00 optional settings from the d878uv2 schema.</p></div></div>
      <div class="empty">No optional settings loaded yet.</div>`;
  }
  const section = state.optionalSection && names.includes(state.optionalSection)
    ? state.optionalSection
    : names[0];
  state.optionalSection = section;
  const fields = opt[section] || {};
  const schemaEl = schemaElement(section);
  const fieldDefs = schemaEl?.fields || Object.keys(fields).map((k) => ({ name: k, type: "int" }));

  const nav = names.map((n) =>
    `<button class="nav-item ${n === section ? "active" : ""}" data-opt="${esc(n)}" type="button">${esc(n)}</button>`
  ).join("");

  const inputs = fieldDefs.map((f) => schemaFieldInput(f, fields[f.name])).join("");

  return `
    <div class="panel-head">
      <div>
        <h1>Optional Settings</h1>
        <p>Generated from <span class="mono">d878uv2/v4.00</span> schema. GPS, APRS, roaming, and signaling have dedicated pages.</p>
      </div>
    </div>
    <div class="editor optional-editor">
      <div class="opt-nav">${nav}</div>
      <form class="form" id="optional-form">
        <h2>${esc(section)}</h2>
        <div class="form-grid">${inputs || "<p class='empty'>No fields in this block</p>"}</div>
        <div class="form-actions"><button class="btn primary" type="submit">Apply section</button></div>
      </form>
    </div>`;
}

/* Friendly labels for schema field names (storage keys stay unchanged). */
const FIELD_LABELS = {
  "Enable GPS": "GPS power",
  "Enable GPS test": "Self-test GPS at boot",
  "GPS Modes": "Constellation",
  "Enable GPS roaming.": "Use GPS roaming zones",
  "Enable RX DMR APRS Positions": "Show received DMR APRS positions",
  "Sent (DMR) APRS message.": "Transmit DMR APRS message text",
  "Source call": "Your callsign",
  "Source SSID": "Your SSID",
  "Destination call": "Destination callsign",
  "Destination SSID": "Destination SSID",
  "APRS symbol table": "Symbol table",
  "APRS symbol": "Map symbol",
  "APRS path string 0-14h": "Path (first half)",
  "APRS path 15h-38h": "Path (second half)",
  "APRS message": "Status / comment text",
  "Manual TX interval": "Manual TX interval (seconds)",
  "Automatic TX interval": "Auto TX interval (×30 seconds)",
  "APRS display duration": "On-screen display time",
  "APRS monitor enable": "Monitor own APRS TX",
  "Fixed location index": "Fixed location slot",
  "Fixed altitude": "Fixed altitude",
  "FM APRS TX delay": "TX delay",
  "FM APRS sub tone type": "Sub-tone type",
  "FM APRS TX CTCSS tone": "CTCSS",
  "FM APRS DCS code": "DCS code",
  "FM APRS transmit power": "Power",
  "FM APRS bandwidth": "Bandwidth",
  "Prewave delay": "Pre-wave delay",
  "Repeater activation delay": "Repeater activation delay",
  "Enable AX.25 CRC check": "Require valid AX.25 CRC",
  "DMR APRS message": "Message text",
  "Roaming support": "APRS roaming support",
  "Zone enable": "Enabled",
  "Roaming zone index": "Roaming zone #",
  "Latitude degree": "Latitude °",
  "Latitude minutes": "Latitude ′",
  "Latitude centi-minute": "Latitude ″ (×0.01′)",
  "Latitude hemisphere": "Lat hemisphere",
  "Longitude degrees": "Longitude °",
  "Longitude minutes": "Longitude ′",
  "Longitude centi-minutes": "Longitude ″ (×0.01′)",
  "Longitude hemisphere": "Lon hemisphere",
  "NEMA report flag": "NMEA report",
};

function friendlyFieldLabel(name) {
  if (FIELD_LABELS[name]) return FIELD_LABELS[name];
  return name.replace(/\.$/, "").replace(/\s+/g, " ").trim();
}

function looksLikeToggle(f) {
  if (f.type === "enum" && Array.isArray(f.items) && f.items.length) return false;
  if (f.type === "string") return false;
  const n = (f.name || "").toLowerCase();
  return n.startsWith("enable ") || n.endsWith(" enable") || n.includes("enable ");
}

function schemaFieldInput(f, val, nameOverride) {
  const name = nameOverride || f.name;
  const label = friendlyFieldLabel(f.name);
  const hint = f.brief ? `<span class="hint">${esc(f.brief)}</span>` : "";

  if (f.type === "enum" && Array.isArray(f.items) && f.items.length) {
    const opts = f.items.map((it) =>
      `<option value="${it.value}" ${Number(val) === Number(it.value) ? "selected" : ""}>${esc(it.name)}</option>`
    ).join("");
    return `<label>${esc(label)}${hint}<select name="${esc(name)}">${opts}</select></label>`;
  }
  if (looksLikeToggle(f)) {
    const on = Number(val) ? 1 : 0;
    return `<label>${esc(label)}${hint}
      <select name="${esc(name)}">
        <option value="0" ${on === 0 ? "selected" : ""}>Off</option>
        <option value="1" ${on === 1 ? "selected" : ""}>On</option>
      </select></label>`;
  }
  if (f.type === "string") {
    return `<label>${esc(label)}<input name="${esc(name)}" value="${esc(val ?? "")}" /></label>`;
  }
  return `<label>${esc(label)}${hint}
    <input class="mono" name="${esc(name)}" type="number" value="${val ?? 0}" /></label>`;
}

function ensureOptional() {
  if (!state.codeplug.optional_settings) state.codeplug.optional_settings = {};
  return state.codeplug.optional_settings;
}

function optBlock(name) {
  const opt = ensureOptional();
  if (!opt[name] || typeof opt[name] !== "object" || Array.isArray(opt[name])) {
    opt[name] = {};
  }
  return opt[name];
}

function optBank(name, count, blank) {
  const opt = ensureOptional();
  if (!Array.isArray(opt[name])) {
    opt[name] = Array.from({ length: count }, () => ({ ...blank }));
  }
  while (opt[name].length < count) opt[name].push({ ...blank });
  return opt[name];
}

function bcdToMhz(v) {
  /* Anytone BCD frequencies are Hz/10 */
  return (Number(v) * 10) / 1e6;
}
function mhzToBcd(mhz) {
  return Math.round((Number(mhz) * 1e6) / 10);
}

function fieldDef(elName, fieldName) {
  return schemaElement(elName)?.fields?.find((f) => f.name === fieldName);
}

function viewGps() {
  const opt = state.codeplug.optional_settings || {};
  const has = !!(opt["General Settings"] || opt["GPS roaming zone"]);
  const tabs = [
    ["receiver", "Receiver"],
    ["zones", "Roaming zones"],
  ];
  let body = "";
  if (!has) {
    body = `<div class="empty">Read the radio (or open a codeplug) to load GPS settings.</div>`;
  } else if (state.gpsTab === "zones") {
    body = viewGpsZones();
  } else {
    body = viewGpsReceiver();
  }
  return `
    <div class="panel-head">
      <div>
        <h1>GPS</h1>
        <p>Receiver power and constellation. Separate from APRS — turn GPS on here if you want position for beacons or roaming.</p>
      </div>
    </div>
    ${tabBar("gpsTab", tabs)}
    ${body}`;
}

function viewGpsReceiver() {
  const gs = optBlock("General Settings");
  const gse = optBlock("General Settings Extension");
  const fields = [
    ["General Settings", "Enable GPS"],
    ["General Settings Extension", "GPS Modes"],
    ["General Settings", "Enable GPS test"],
    ["General Settings Extension", "Enable GPS roaming."],
  ];
  const inputs = fields.map(([sec, name]) => {
    const def = fieldDef(sec, name) || { name, type: "int" };
    const src = sec === "General Settings" ? gs : gse;
    return schemaFieldInput(def, src[name], `${sec}::${name}`);
  }).join("");
  return `<form class="form" id="gps-receiver-form">
    <h2>GPS receiver</h2>
    <p class="hint-block">These controls only enable the radio’s GNSS. Packet APRS settings are under <strong>APRS</strong>.</p>
    <div class="form-grid">${inputs}</div>
    <div class="form-actions"><button class="btn primary" type="submit">Apply</button></div>
  </form>`;
}

function viewGpsZones() {
  return viewAprsZones(); /* same bank editor; kept name for minimal churn */
}

function viewAprs() {
  const opt = state.codeplug.optional_settings || {};
  const has = !!(opt["APRS settings"] || opt["DMR APRS message"] || opt["General Settings"]);
  const tabs = [
    ["identity", "Call & path"],
    ["fm", "FM APRS"],
    ["dmr", "DMR APRS"],
    ["filters", "RX filters"],
    ["options", "Options"],
  ];
  const tabNav = tabs.map(([id, label]) =>
    `<button type="button" class="tab ${state.aprsTab === id ? "active" : ""}" data-tab-key="aprsTab" data-tab-id="${id}">${label}</button>`
  ).join("");

  let body = "";
  if (!has) {
    body = `<div class="empty">Read the radio (or open a codeplug) to load APRS settings.</div>`;
  } else if (state.aprsTab === "identity") body = viewAprsIdentity();
  else if (state.aprsTab === "fm") body = viewAprsFm();
  else if (state.aprsTab === "dmr") body = viewAprsDmr();
  else if (state.aprsTab === "filters") body = viewAprsFilters();
  else if (state.aprsTab === "options") body = viewAprsOptions();

  return `
    <div class="panel-head">
      <div>
        <h1>APRS</h1>
        <p>FM (AX.25) and DMR APRS messaging. GPS power lives under <strong>GPS</strong> — enable it there if you want live position beacons.</p>
      </div>
    </div>
    <div class="tabs">${tabNav}</div>
    ${body}`;
}

function viewAprsOptions() {
  const gs = optBlock("General Settings");
  const fields = [
    ["General Settings", "Enable RX DMR APRS Positions"],
    ["General Settings", "Sent (DMR) APRS message."],
  ];
  const inputs = fields.map(([sec, name]) => {
    const def = fieldDef(sec, name) || { name, type: "int" };
    return schemaFieldInput(def, gs[name], `${sec}::${name}`);
  }).join("");
  return `<form class="form" id="aprs-options-form">
    <h2>APRS behavior</h2>
    <p class="hint-block">These affect APRS messaging only — not whether the GPS receiver is powered.</p>
    <div class="form-grid">${inputs}</div>
    <div class="form-actions"><button class="btn primary" type="submit">Apply</button></div>
  </form>`;
}

/* APRS symbol codes are stored as ASCII char values (e.g. '/' = 47, '>' = 62). */
const APRS_SYMBOL_TABLES = [
  { code: 47, label: "Primary (/) — normal icons" },
  { code: 92, label: "Alternate (\\) — overlays / alt icons" },
];

const APRS_SSID_CHOICES = [
  { v: 0, label: "0 — primary station" },
  { v: 1, label: "1 — generic / extra" },
  { v: 2, label: "2 — generic / extra" },
  { v: 3, label: "3 — generic / extra" },
  { v: 4, label: "4 — generic / extra" },
  { v: 5, label: "5 — generic / extra" },
  { v: 6, label: "6 — generic / extra" },
  { v: 7, label: "7 — handheld / portable (common)" },
  { v: 8, label: "8 — boat / maritime (or second HT)" },
  { v: 9, label: "9 — mobile / vehicle (common)" },
  { v: 10, label: "10 — internet / APRS-IS" },
  { v: 11, label: "11 — aircraft / balloon" },
  { v: 12, label: "12 — one-way tracker / APRStt" },
  { v: 13, label: "13 — weather station" },
  { v: 14, label: "14 — truck / full-time tracker" },
  { v: 15, label: "15 — digipeater / relay" },
];

const APRS_PATH_PRESETS = [
  { path: "WIDE1-1", label: "WIDE1-1 — urban / local (recommended HT)" },
  { path: "WIDE1-1,WIDE2-1", label: "WIDE1-1,WIDE2-1 — suburban / rural" },
  { path: "WIDE2-1", label: "WIDE2-1 — one hop only" },
  { path: "", label: "Direct — no digipeat path" },
];

const APRS_SYMBOLS_PRIMARY = [
  { ch: ">", name: "Car" },
  { ch: "[", name: "Person / jogger (HT)" },
  { ch: "b", name: "Bicycle" },
  { ch: "v", name: "Van" },
  { ch: "k", name: "Truck" },
  { ch: "j", name: "Jeep" },
  { ch: "u", name: "Bus" },
  { ch: "R", name: "RV / recreational" },
  { ch: "-", name: "House / QTH" },
  { ch: "s", name: "Ship / boat" },
  { ch: "Y", name: "Yacht" },
  { ch: "^", name: "Aircraft" },
  { ch: "g", name: "Glider" },
  { ch: "'", name: "Aircraft (small)" },
  { ch: "O", name: "Balloon" },
  { ch: "S", name: "Satellite" },
  { ch: "W", name: "Water station / weather" },
  { ch: "_", name: "Weather station" },
  { ch: "#", name: "Digipeater" },
  { ch: "&", name: "HF gateway / IGate" },
  { ch: "r", name: "Repeater" },
  { ch: "n", name: "Node" },
  { ch: "I", name: "TCP/IP / IGate" },
  { ch: "K", name: "School" },
  { ch: "h", name: "Hospital" },
  { ch: "c", name: "Motorcycle" },
  { ch: "*", name: "Snowmobile" },
  { ch: "p", name: "Dog" },
  { ch: "f", name: "Fire truck" },
  { ch: "a", name: "Ambulance" },
  { ch: "!", name: "Police / sheriff" },
];

const APRS_SYMBOLS_ALT = [
  { ch: ">", name: "Car (alt / overlay)" },
  { ch: "v", name: "Van (alt)" },
  { ch: "k", name: "Truck (alt)" },
  { ch: "s", name: "Ship (alt)" },
  { ch: "^", name: "Aircraft (alt)" },
  { ch: "#", name: "Digipeater (green star)" },
  { ch: "&", name: "IGate (black diamond)" },
  { ch: "-", name: "House (HF)" },
  { ch: "_", name: "Weather (alt)" },
  { ch: "O", name: "Balloon (alt)" },
  { ch: "n", name: "Node (alt)" },
];

function aprsCharCode(v) {
  const n = Number(v);
  return Number.isFinite(n) ? n : 0;
}

function aprsCodeLabel(code) {
  const ch = code >= 32 && code < 127 ? String.fromCharCode(code) : "?";
  return `${ch}  (${code})`;
}

function selectOptions(items, current, valueKey, labelFn, extra) {
  const cur = Number(current);
  const seen = new Set(items.map((it) => Number(it[valueKey])));
  let html = items.map((it) => {
    const v = Number(it[valueKey]);
    return `<option value="${v}" ${v === cur ? "selected" : ""}>${esc(labelFn(it))}</option>`;
  }).join("");
  if (!seen.has(cur) && Number.isFinite(cur)) {
    html = `<option value="${cur}" selected>${esc(extra ? extra(cur) : `Current (${cur})`)}</option>` + html;
  }
  return html;
}

function viewAprsIdentity() {
  const a = optBlock("APRS settings");
  const table = aprsCharCode(a["APRS symbol table"] ?? 47);
  const sym = aprsCharCode(a["APRS symbol"] ?? 62);
  const ssid = Number(a["Source SSID"] ?? 7);
  const path1 = a["APRS path string 0-14h"] ?? "";
  const path2 = a["APRS path 15h-38h"] ?? "";
  const fullPath = `${path1 || ""}${path2 || ""}`;

  const tableOpts = selectOptions(
    APRS_SYMBOL_TABLES, table, "code",
    (it) => it.label,
    (c) => `Custom table ${aprsCodeLabel(c)}`
  );

  const symList = table === 92 ? APRS_SYMBOLS_ALT : APRS_SYMBOLS_PRIMARY;
  const symOpts = selectOptions(
    symList.map((s) => ({ code: s.ch.charCodeAt(0), name: s.name, ch: s.ch })),
    sym, "code",
    (it) => `${it.ch} — ${it.name}`,
    (c) => `Custom ${aprsCodeLabel(c)}`
  );

  const ssidOpts = selectOptions(
    APRS_SSID_CHOICES, ssid, "v",
    (it) => it.label,
    (c) => `SSID ${c}`
  );

  const pathPresetOpts = APRS_PATH_PRESETS.map((p) =>
    `<option value="${esc(p.path)}" ${fullPath === p.path ? "selected" : ""}>${esc(p.label)}</option>`
  ).join("");
  const pathPresetExtra = APRS_PATH_PRESETS.some((p) => p.path === fullPath)
    ? ""
    : `<option value="__keep__" selected>Current — ${esc(fullPath || "(empty)")}</option>`;

  const glyph = sym >= 32 && sym < 127 ? String.fromCharCode(sym) : "?";
  const tableGlyph = table >= 32 && table < 127 ? String.fromCharCode(table) : "?";

  const chipList = (table === 92 ? APRS_SYMBOLS_ALT : APRS_SYMBOLS_PRIMARY);
  const chips = chipList.map((s) => {
    const code = s.ch.charCodeAt(0);
    const active = code === sym ? "active" : "";
    return `<button type="button" class="sym-chip ${active}" data-aprs-sym="${code}" title="${esc(s.name)}">
      <span class="sym-glyph">${esc(s.ch)}</span><span class="sym-name">${esc(s.name)}</span>
    </button>`;
  }).join("");

  const timing = ["Manual TX interval", "Automatic TX interval", "APRS display duration",
    "APRS monitor enable", "Fixed location index", "Fixed altitude"];
  const timingInputs = timing.map((n) => {
    const def = fieldDef("APRS settings", n) || { name: n, type: "int" };
    return schemaFieldInput(def, a[n]);
  }).join("");

  const dest = ["Destination call", "Destination SSID"];
  const destInputs = dest.map((n) => {
    const def = fieldDef("APRS settings", n) || { name: n, type: n.includes("call") ? "string" : "int" };
    return schemaFieldInput(def, a[n]);
  }).join("");

  const flags = ["Status report flag", "NEMA report flag", "Weather report flag", "Message flag",
    "Item flag", "Object flag", "Mic-E flag", "Position Flag", "Other flag"];
  const flagInputs = flags.map((n) => {
    const def = fieldDef("APRS settings", n) || { name: n, type: "int" };
    return schemaFieldInput(def, a[n]);
  }).join("");

  return `<form class="form" id="aprs-identity-form">
    <h2>Your station</h2>
    <p class="hint-block">Callsign and SSID go out in every beacon. Destination <span class="mono">APAT81</span> is Anytone’s APRS ID — leave it unless you know you need something else.</p>
    <div class="form-grid">
      <label>Your callsign
        <input name="Source call" value="${esc(a["Source call"] ?? "")}" maxlength="6" class="mono" /></label>
      <label>Your SSID
        <select name="Source SSID">${ssidOpts}</select></label>
      ${destInputs}
    </div>

    <h2>Map symbol <span class="sym-preview mono" title="table + symbol">${esc(tableGlyph)}${esc(glyph)}</span></h2>
    <p class="hint-block">Pick a table, then an icon. Values are stored as ASCII codes (what Anytone CPS uses).</p>
    <div class="form-grid">
      <label>Symbol table
        <select name="APRS symbol table" id="aprs-sym-table">${tableOpts}</select></label>
      <label>Map symbol
        <select name="APRS symbol" id="aprs-sym-select">${symOpts}</select></label>
    </div>
    <div class="sym-chip-grid" id="aprs-sym-chips">${chips}</div>

    <h2>Path &amp; comment</h2>
    <div class="form-grid">
      <label>Path preset
        <select id="aprs-path-preset">${pathPresetExtra}${pathPresetOpts}</select></label>
      <label>Status / comment text
        <input name="APRS message" value="${esc(a["APRS message"] ?? "")}" /></label>
      <label>Path (first half)
        <input class="mono" name="APRS path string 0-14h" id="aprs-path-1" value="${esc(path1)}" /></label>
      <label>Path (second half)
        <input class="mono" name="APRS path 15h-38h" id="aprs-path-2" value="${esc(path2)}" /></label>
    </div>

    <h2>Beacon timing</h2>
    <div class="form-grid">${timingInputs}</div>
    <h2>Report flags</h2>
    <div class="form-grid">${flagInputs}</div>
    <div class="form-actions"><button class="btn primary" type="submit">Apply</button></div>
  </form>`;
}

function viewAprsFm() {
  const a = optBlock("APRS settings");
  const names = [
    "FM APRS TX delay", "FM APRS sub tone type", "FM APRS TX CTCSS tone", "FM APRS DCS code",
    "FM APRS transmit power", "FM APRS bandwidth", "Prewave delay",
    "Repeater activation delay", "Enable AX.25 CRC check",
  ];
  const inputs = names.map((n) => {
    const def = fieldDef("APRS settings", n);
    if (!def) return "";
    return schemaFieldInput(def, a[n]);
  }).join("");
  const freqRows = Array.from({ length: 8 }, (_, i) => {
    const key = `FM APRS Frequency [${i}]`;
    const mhzVal = a[key] != null ? bcdToMhz(a[key]).toFixed(5) : "";
    return `<tr>
      <td class="mono">${i + 1}</td>
      <td><input class="mono" name="${esc(key)}" type="number" step="0.00001" value="${esc(mhzVal)}" /></td>
    </tr>`;
  }).join("");
  return `<form class="form" id="aprs-fm-form">
    <h2>FM (AX.25) APRS</h2>
    <div class="form-grid">${inputs}</div>
    <h2>FM APRS frequencies (MHz)</h2>
    <p class="hint-block">US/Canada VHF APRS is <span class="mono">144.390</span>. China often ships <span class="mono">144.640</span>.</p>
    <div class="form-actions" style="margin-bottom:0.65rem">
      <button class="btn" type="button" data-aprs-freq="144.390">Fill all → 144.390 (US)</button>
      <button class="btn" type="button" data-aprs-freq="144.640">Fill all → 144.640 (CN)</button>
    </div>
    <div class="table-wrap"><table>
      <thead><tr><th>#</th><th>Frequency</th></tr></thead>
      <tbody>${freqRows}</tbody>
    </table></div>
    <div class="form-actions"><button class="btn primary" type="submit">Apply</button></div>
  </form>`;
}

function viewAprsDmr() {
  const a = optBlock("APRS settings");
  const msg = optBlock("DMR APRS message");
  const msgDef = fieldDef("DMR APRS message", "DMR APRS message") || { name: "DMR APRS message", type: "string" };
  const rows = Array.from({ length: 8 }, (_, i) => {
    const ch = a[`Channel index [${i}]`] ?? 0;
    const id = a[`DMR destination ID [${i}]`] ?? 0;
    const ct = a[`Call type [${i}]`] ?? 1;
    const ts = a[`Time slot [${i}]`] ?? 0;
    const ctDef = fieldDef("APRS settings", `Call type [${i}]`);
    const tsDef = fieldDef("APRS settings", `Time slot [${i}]`);
    const ctOpts = (ctDef?.items || [
      { value: 0, name: "Private" }, { value: 1, name: "Group" }, { value: 2, name: "All" },
    ]).map((it) => `<option value="${it.value}" ${Number(ct) === Number(it.value) ? "selected" : ""}>${esc(it.name)}</option>`).join("");
    const tsOpts = (tsDef?.items || [
      { value: 0, name: "Channel slot" }, { value: 1, name: "TS1" }, { value: 2, name: "TS2" },
    ]).map((it) => `<option value="${it.value}" ${Number(ts) === Number(it.value) ? "selected" : ""}>${esc(it.name)}</option>`).join("");
    return `<tr>
      <td class="mono">${i + 1}</td>
      <td><input class="mono" name="Channel index [${i}]" type="number" value="${ch}" /></td>
      <td><input class="mono" name="DMR destination ID [${i}]" type="number" value="${id}" /></td>
      <td><select name="Call type [${i}]">${ctOpts}</select></td>
      <td><select name="Time slot [${i}]">${tsOpts}</select></td>
    </tr>`;
  }).join("");
  const extra = ["Roaming support"].map((n) => {
    const def = fieldDef("APRS settings", n);
    return def ? schemaFieldInput(def, a[n]) : "";
  }).join("");
  return `<form class="form" id="aprs-dmr-form">
    <h2>DMR APRS</h2>
    <div class="form-grid">
      ${schemaFieldInput(msgDef, msg["DMR APRS message"])}
      ${extra}
    </div>
    <h2>DMR APRS channels (8)</h2>
    <p class="hint-block">Channel index: 0-based memory, or 4000=VFO A, 4001=VFO B, 4002=Current.</p>
    <div class="table-wrap"><table>
      <thead><tr><th>#</th><th>Channel</th><th>Dest ID</th><th>Call type</th><th>Time slot</th></tr></thead>
      <tbody>${rows}</tbody>
    </table></div>
    <div class="form-actions"><button class="btn primary" type="submit">Apply</button></div>
  </form>`;
}

function viewAprsFilters() {
  const blank = { "Enable filter": 0, Call: "", SSID: 16 };
  const list = optBank("APRS filter", 32, blank);
  const i = Math.max(0, Math.min(state.selected["aprs-filters"] || 0, list.length - 1));
  state.selected["aprs-filters"] = i;
  const rows = list.map((f, idx) => `
    <tr data-i="${idx}" class="${i === idx ? "selected" : ""}">
      <td class="mono">${idx + 1}</td>
      <td>${Number(f["Enable filter"]) ? "On" : "Off"}</td>
      <td class="mono">${esc(f.Call || "")}</td>
      <td class="mono">${f.SSID === 16 || f.SSID === "16" ? "—" : f.SSID}</td>
    </tr>`).join("");
  const f = list[i];
  const defs = schemaElement("APRS filter")?.fields || [];
  const inputs = defs.map((d) => schemaFieldInput(d, f[d.name])).join("");
  return `
    <div class="editor">
      <div class="table-wrap"><table>
        <thead><tr><th>#</th><th>Enable</th><th>Call</th><th>SSID</th></tr></thead>
        <tbody id="aprs-filters-table">${rows}</tbody>
      </table></div>
      <form class="form" id="aprs-filter-form">
        <h2>Filter ${i + 1}</h2>
        <div class="form-grid">${inputs}</div>
        <div class="form-actions"><button class="btn primary" type="submit">Apply</button></div>
      </form>
    </div>`;
}

function viewAprsZones() {
  const blank = {
    "Zone enable": 0, "Roaming zone index": 255,
    "Latitude degree": 0, "Latitude minutes": 0, "Latitude centi-minute": 0, "Latitude hemisphere": 0,
    "Longitude degrees": 0, "Longitude minutes": 0, "Longitude centi-minutes": 0, "Longitude hemisphere": 0,
    Radius: 0,
  };
  const list = optBank("GPS roaming zone", 32, blank);
  const i = Math.max(0, Math.min(state.selected["aprs-zones"] || 0, list.length - 1));
  state.selected["aprs-zones"] = i;
  const rows = list.map((z, idx) => {
    const on = Number(z["Zone enable"]);
    const lat = `${z["Latitude degree"]}°${z["Latitude minutes"]}.${String(z["Latitude centi-minute"]).padStart(2, "0")}${Number(z["Latitude hemisphere"]) ? "S" : "N"}`;
    return `<tr data-i="${idx}" class="${i === idx ? "selected" : ""}">
      <td class="mono">${idx + 1}</td>
      <td>${on ? "On" : "Off"}</td>
      <td class="mono">${z["Roaming zone index"]}</td>
      <td class="mono">${lat}</td>
      <td class="mono">${z.Radius}</td>
    </tr>`;
  }).join("");
  const z = list[i];
  const defs = schemaElement("GPS roaming zone")?.fields || [];
  const inputs = defs.map((d) => schemaFieldInput(d, z[d.name])).join("");
  return `
    <div class="editor">
      <div class="table-wrap"><table>
        <thead><tr><th>#</th><th>Enable</th><th>Zone</th><th>Latitude</th><th>Radius</th></tr></thead>
        <tbody id="aprs-zones-table">${rows}</tbody>
      </table></div>
      <form class="form" id="aprs-zone-form">
        <h2>GPS roaming zone ${i + 1}</h2>
        <div class="form-grid">${inputs}</div>
        <div class="form-actions"><button class="btn primary" type="submit">Apply</button></div>
      </form>
    </div>`;
}

/* ---------- shared schema section helpers ---------- */

function tabBar(tabKey, tabs) {
  return `<div class="tabs">${tabs.map(([id, label]) =>
    `<button type="button" class="tab ${state[tabKey] === id ? "active" : ""}" data-tab-key="${tabKey}" data-tab-id="${id}">${label}</button>`
  ).join("")}</div>`;
}

function bankCount(elName, fallback) {
  return schemaElement(elName)?.bank_count || fallback;
}

function blankFromSchema(elName) {
  const defs = schemaElement(elName)?.fields || [];
  const o = {};
  for (const f of defs) {
    if (f.type === "string") o[f.name] = "";
    else if (f.default != null) o[f.name] = Number(f.default);
    else o[f.name] = 0;
  }
  return o;
}

function viewBankEditor({ element, selectedKey, tableId, formId, columns, rowHtml, title }) {
  const count = bankCount(element, 1);
  const list = optBank(element, count, blankFromSchema(element));
  let i = state.selected[selectedKey];
  if (i == null || i < 0) i = 0;
  i = Math.max(0, Math.min(i, list.length - 1));
  state.selected[selectedKey] = i;
  const rows = list.map((item, idx) =>
    `<tr data-i="${idx}" class="${i === idx ? "selected" : ""}">${rowHtml(item, idx)}</tr>`
  ).join("");
  const item = list[i];
  const defs = schemaElement(element)?.fields || [];
  /* Cap form fields for huge definitions */
  const formDefs = defs.length > 80 ? defs.slice(0, 80) : defs;
  const inputs = formDefs.map((d) => schemaFieldInput(d, item[d.name])).join("");
  return `
    <div class="editor">
      <div class="table-wrap"><table>
        <thead><tr>${columns.map((c) => `<th>${c}</th>`).join("")}</tr></thead>
        <tbody id="${tableId}">${rows}</tbody>
      </table></div>
      <form class="form" id="${formId}" data-bank-element="${esc(element)}" data-bank-selected="${esc(selectedKey)}">
        <h2>${esc(title)} ${i + 1} / ${count}</h2>
        <div class="form-grid">${inputs}</div>
        <div class="form-actions"><button class="btn primary" type="submit">Apply</button></div>
      </form>
    </div>`;
}

function viewSchemaForm({ element, formId, title, fieldFilter }) {
  const block = Array.isArray(ensureOptional()[element]) ? null : optBlock(element);
  if (!block) {
    return `<div class="empty">${esc(element)} is a list — use the table editor tab.</div>`;
  }
  let defs = schemaElement(element)?.fields || Object.keys(block).map((k) => ({ name: k, type: "int" }));
  if (fieldFilter) defs = defs.filter(fieldFilter);
  const inputs = defs.map((d) => schemaFieldInput(d, block[d.name])).join("");
  return `<form class="form" id="${formId}" data-schema-element="${esc(element)}">
    <h2>${esc(title || element)}</h2>
    <div class="form-grid">${inputs || "<p class='empty'>No fields</p>"}</div>
    <div class="form-actions"><button class="btn primary" type="submit">Apply</button></div>
  </form>`;
}

function viewRoaming() {
  const opt = state.codeplug.optional_settings || {};
  const has = !!(opt["Roaming channel"] || opt["Roaming zone"]);
  const tabs = [["channels", "Channels"], ["zones", "Zones"]];
  let body;
  if (!has) {
    body = `<div class="empty">Read the radio to load roaming channels and zones.</div>`;
  } else if (state.roamingTab === "zones") {
    body = viewBankEditor({
      element: "Roaming zone",
      selectedKey: "roam-zone",
      tableId: "roam-zone-table",
      formId: "roam-zone-form",
      columns: ["#", "Name", "Members"],
      title: "Roaming zone",
      rowHtml: (z, idx) => {
        const members = Object.keys(z).filter((k) => k.startsWith("Roaming channel index")).filter((k) => {
          const v = Number(z[k]);
          return !Number.isNaN(v) && v !== 0xff && v !== 255;
        }).length;
        return `<td class="mono">${idx + 1}</td><td>${esc(z.Name || "")}</td><td class="mono">${members}</td>`;
      },
    });
  } else {
    body = viewBankEditor({
      element: "Roaming channel",
      selectedKey: "roam-ch",
      tableId: "roam-ch-table",
      formId: "roam-ch-form",
      columns: ["#", "Name", "RX MHz", "TX MHz", "CC"],
      title: "Roaming channel",
      rowHtml: (ch, idx) => {
        const rx = ch["RX frequency"] != null ? bcdToMhz(ch["RX frequency"]).toFixed(4) : "—";
        const tx = ch["TX frequency"] != null ? bcdToMhz(ch["TX frequency"]).toFixed(4) : "—";
        return `<td class="mono">${idx + 1}</td><td>${esc(ch.Name || "")}</td>
          <td class="mono">${rx}</td><td class="mono">${tx}</td>
          <td class="mono">${ch["Color code override"] ?? "—"}</td>`;
      },
    });
  }
  return `
    <div class="panel-head"><div>
      <h1>Roaming</h1>
      <p>Site roaming channels and zones — same structure as Windows CPS Roaming.</p>
    </div></div>
    ${tabBar("roamingTab", tabs)}
    ${body}`;
}

function viewHotkeys() {
  const opt = state.codeplug.optional_settings || {};
  const has = !!(opt["General Settings"] || opt["Hot-Key Setting"]);
  const tabs = [["side", "Side keys"], ["hot", "Number-pad hotkeys"]];
  let body;
  if (!has) {
    body = `<div class="empty">Read the radio to load button and hotkey assignments.</div>`;
  } else if (state.hotkeysTab === "hot") {
    body = viewBankEditor({
      element: "Hot-Key Setting",
      selectedKey: "hotkeys",
      tableId: "hotkeys-table",
      formId: "hotkeys-form",
      columns: ["#", "Type", "Menu / contact"],
      title: "Hotkey",
      rowHtml: (h, idx) => {
        const typeDef = fieldDef("Hot-Key Setting", "Type");
        const typeName = typeDef?.items?.find((it) => Number(it.value) === Number(h.Type))?.name || h.Type;
        const detail = Number(h.Type) === 1
          ? (fieldDef("Hot-Key Setting", "Menu Item")?.items?.find((it) => Number(it.value) === Number(h["Menu Item"]))?.name || h["Menu Item"])
          : `contact ${h["Contact Index"]}`;
        return `<td class="mono">${idx + 1}</td><td>${esc(typeName)}</td><td>${esc(detail)}</td>`;
      },
    });
  } else {
    const keys = [
      "PF1 Short Press Function", "PF1 Long Press Function",
      "PF2 Short Press Function", "PF2 Long Press Function",
      "PF3 Short Press Function", "PF3 Long Press Function",
      "P1 Short Press Function", "P1 Long Press Function",
      "P2 Short Press Function", "P2 Long Press Function",
      "Long Press Duration", "Key tone volume", "Enable key tone",
    ];
    body = viewSchemaForm({
      element: "General Settings",
      formId: "hotkeys-side-form",
      title: "Programmable side keys",
      fieldFilter: (f) => keys.includes(f.name),
    });
  }
  return `
    <div class="panel-head"><div>
      <h1>Buttons &amp; Hotkeys</h1>
      <p>PF/P side-key functions and 0–9 / * / # hotkey slots.</p>
    </div></div>
    ${tabBar("hotkeysTab", tabs)}
    ${body}`;
}

function viewSignaling() {
  const opt = state.codeplug.optional_settings || {};
  const has = !!(opt["DTMF Settings"] || opt["2-Tone Settings"] || opt["5-Tone settings"]);
  const tabs = [
    ["dtmf", "DTMF"],
    ["dtmf-contacts", "DTMF contacts"],
    ["tone2", "2-Tone"],
    ["tone5", "5-Tone"],
  ];
  let body;
  if (!has) {
    body = `<div class="empty">Read the radio to load DTMF / 2-Tone / 5-Tone settings.</div>`;
  } else if (state.signalingTab === "dtmf-contacts") {
    body = viewBankEditor({
      element: "DTMF Contact",
      selectedKey: "dtmf-contacts",
      tableId: "dtmf-contacts-table",
      formId: "dtmf-contacts-form",
      columns: ["#", "Name", "Length"],
      title: "DTMF contact",
      rowHtml: (c, idx) =>
        `<td class="mono">${idx + 1}</td><td>${esc(c.Name || "")}</td><td class="mono">${c["Number Length"] ?? ""}</td>`,
    });
  } else if (state.signalingTab === "tone2") {
    body = `
      ${viewSchemaForm({ element: "2-Tone Settings", formId: "tone2-settings-form", title: "2-Tone settings" })}
      <h2 class="subhead">2-Tone IDs</h2>
      ${viewBankEditor({
        element: "2-Tone Id",
        selectedKey: "tone2-id",
        tableId: "tone2-id-table",
        formId: "tone2-id-form",
        columns: ["#", "Name", "Tone 1", "Tone 2"],
        title: "2-Tone ID",
        rowHtml: (t, idx) =>
          `<td class="mono">${idx + 1}</td><td>${esc(t.Name || "")}</td>
           <td class="mono">${t["First tone frequency"] ?? ""}</td>
           <td class="mono">${t["Second tone frequency"] ?? ""}</td>`,
      })}
      <h2 class="subhead">2-Tone functions</h2>
      ${viewBankEditor({
        element: "Two-Tone function",
        selectedKey: "tone2-fn",
        tableId: "tone2-fn-table",
        formId: "tone2-fn-form",
        columns: ["#", "Name", "Response"],
        title: "2-Tone function",
        rowHtml: (t, idx) =>
          `<td class="mono">${idx + 1}</td><td>${esc(t["Function name"] || "")}</td>
           <td class="mono">${t.Response ?? ""}</td>`,
      })}`;
  } else if (state.signalingTab === "tone5") {
    body = `
      ${viewSchemaForm({ element: "5-Tone settings", formId: "tone5-settings-form", title: "5-Tone settings" })}
      <h2 class="subhead">5-Tone IDs</h2>
      ${viewBankEditor({
        element: "5-tone ID",
        selectedKey: "tone5-id",
        tableId: "tone5-id-table",
        formId: "tone5-id-form",
        columns: ["#", "Standard", "Length"],
        title: "5-Tone ID",
        rowHtml: (t, idx) =>
          `<td class="mono">${idx + 1}</td><td class="mono">${t.Standard ?? ""}</td>
           <td class="mono">${t["ID length"] ?? ""}</td>`,
      })}
      <h2 class="subhead">5-Tone functions</h2>
      ${viewBankEditor({
        element: "5-Tone function",
        selectedKey: "tone5-fn",
        tableId: "tone5-fn-table",
        formId: "tone5-fn-form",
        columns: ["#", "Code", "Response"],
        title: "5-Tone function",
        rowHtml: (t, idx) =>
          `<td class="mono">${idx + 1}</td><td class="mono">${t["Function code"] ?? ""}</td>
           <td class="mono">${t.Response ?? ""}</td>`,
      })}`;
  } else {
    body = viewSchemaForm({ element: "DTMF Settings", formId: "dtmf-settings-form", title: "DTMF settings" });
  }
  return `
    <div class="panel-head"><div>
      <h1>Signaling</h1>
      <p>DTMF, 2-Tone, and 5-Tone encode/decode — matching Windows CPS Signaling.</p>
    </div></div>
    ${tabBar("signalingTab", tabs)}
    ${body}`;
}

function viewEncryption() {
  const opt = state.codeplug.optional_settings || {};
  const has = !!(opt["DMR Encryption Key"] || opt["ARC4 encryption key"] || opt["Alarm Settings"]);
  const tabs = [
    ["dmr", "DMR keys"],
    ["arc4", "ARC4"],
    ["alarm", "Alarm / emergency"],
    ["aes", "AES"],
  ];
  let body;
  if (!has) {
    body = `<div class="empty">Read the radio to load encryption and alarm settings.</div>`;
  } else if (state.encryptionTab === "arc4") {
    body = viewBankEditor({
      element: "ARC4 encryption key",
      selectedKey: "enc-arc4",
      tableId: "enc-arc4-table",
      formId: "enc-arc4-form",
      columns: ["#", "Key ID", "Key bits"],
      title: "ARC4 key",
      rowHtml: (k, idx) =>
        `<td class="mono">${idx + 1}</td><td class="mono">${k["Key id"] ?? ""}</td>
         <td class="mono">${k["Key bits"] ?? ""}</td>`,
    });
  } else if (state.encryptionTab === "alarm") {
    body = `
      ${viewSchemaForm({ element: "Alarm Settings", formId: "alarm-form", title: "Alarm settings" })}
      ${opt["DMR Alarm Extension"]
        ? viewSchemaForm({ element: "DMR Alarm Extension", formId: "alarm-dmr-form", title: "DMR alarm extension" })
        : ""}`;
  } else if (state.encryptionTab === "aes") {
    const bmp = opt["AES encryption key bitmap"];
    body = `<div class="form">
      <h2>AES keys</h2>
      <p class="hint-block">AES key material is a large binary bank. Enable bits are under Optional Settings → AES encryption key bitmap${bmp ? " (loaded)" : ""}. Full key editing stays in Optional Settings for now.</p>
      <button type="button" class="btn" id="goto-aes-optional">Open Optional Settings</button>
    </div>`;
  } else {
    body = viewBankEditor({
      element: "DMR Encryption Key",
      selectedKey: "enc-dmr",
      tableId: "enc-dmr-table",
      formId: "enc-dmr-form",
      columns: ["#", "Key"],
      title: "DMR encryption key",
      rowHtml: (k, idx) =>
        `<td class="mono">${idx + 1}</td><td class="mono">${k.Key ?? ""}</td>`,
    });
  }
  return `
    <div class="panel-head"><div>
      <h1>Encryption</h1>
      <p>DMR basic keys, ARC4, AES bitmap, and emergency/alarm options.</p>
    </div></div>
    ${tabBar("encryptionTab", tabs)}
    ${body}`;
}

function viewRadioIds() {
  const rows = state.codeplug.radio_ids.map((r, i) => `
    <tr data-i="${i}" class="${state.selected["radio-ids"] === i ? "selected" : ""}">
      <td class="mono">${r.index + 1}</td>
      <td>${esc(r.name)}</td>
      <td class="mono">${r.number}</td>
    </tr>`).join("");
  const sel = state.codeplug.radio_ids[state.selected["radio-ids"]];
  return `
    <div class="panel-head">
      <div>
        <h1>Radio IDs</h1>
        <p>Your DMR ID(s). Most operators need only one — request it at radioid.net.</p>
      </div>
      <button class="btn primary" id="add-radio-id">Add radio ID</button>
    </div>
    <div class="editor">
      <div class="table-wrap"><table>
        <thead><tr><th>#</th><th>Name</th><th>DMR ID</th></tr></thead>
        <tbody id="radio-ids-table">${rows || `<tr><td colspan="3">No radio IDs yet</td></tr>`}</tbody>
      </table></div>
      ${sel ? radioIdForm(sel) : `<div class="empty">Select a radio ID</div>`}
    </div>`;
}

function radioIdForm(r) {
  return `<form class="form" id="radio-id-form">
    <h2>Edit radio ID</h2>
    <div class="form-grid">
      <label>Slot index (0-based)<input class="mono" name="index" type="number" min="0" max="249" value="${r.index}" /></label>
      <label>DMR ID<input class="mono" name="number" type="number" min="1" max="16777215" value="${r.number}" /></label>
      <label style="grid-column:1/-1">Name<input name="name" maxlength="16" value="${esc(r.name)}" /></label>
    </div>
    <div class="form-actions">
      <button class="btn primary" type="submit">Apply</button>
      <button class="btn danger" type="button" id="del-radio-id">Delete</button>
    </div>
  </form>`;
}

function viewContacts() {
  const rows = state.codeplug.contacts.map((c, i) => `
    <tr data-i="${i}" class="${state.selected.contacts === i ? "selected" : ""}">
      <td class="mono">${c.index + 1}</td>
      <td>${esc(c.name)}</td>
      <td class="mono">${c.number}</td>
      <td>${CALL_TYPE[c.type] || c.type}</td>
    </tr>`).join("");
  const sel = state.codeplug.contacts[state.selected.contacts];
  return `
    <div class="panel-head">
      <div>
        <h1>Talk Groups / Contacts</h1>
        <p>Digital contacts used by channels. Create group-call talk groups here before building channels.</p>
      </div>
      <button class="btn primary" id="add-contact">Add contact</button>
    </div>
    <div class="editor">
      <div class="table-wrap"><table>
        <thead><tr><th>#</th><th>Name</th><th>ID</th><th>Type</th></tr></thead>
        <tbody id="contacts-table">${rows || `<tr><td colspan="4">No contacts yet</td></tr>`}</tbody>
      </table></div>
      ${sel ? contactForm(sel) : `<div class="empty">Select a contact</div>`}
    </div>`;
}

function contactForm(c) {
  return `<form class="form" id="contact-form">
    <h2>Edit contact</h2>
    <div class="form-grid">
      <label>Slot index<input class="mono" name="index" type="number" min="0" value="${c.index}" /></label>
      <label>Call type
        <select name="type">
          ${CALL_TYPE.map((t, i) => `<option value="${i}" ${c.type === i ? "selected" : ""}>${t}</option>`).join("")}
        </select>
      </label>
      <label>Talk group / ID<input class="mono" name="number" type="number" min="0" value="${c.number}" /></label>
      <label>Alert
        <select name="alert">
          <option value="0" ${c.alert === 0 ? "selected" : ""}>None</option>
          <option value="1" ${c.alert === 1 ? "selected" : ""}>Ring</option>
          <option value="2" ${c.alert === 2 ? "selected" : ""}>Online</option>
        </select>
      </label>
      <label style="grid-column:1/-1">Name<input name="name" maxlength="16" value="${esc(c.name)}" /></label>
    </div>
    <div class="form-actions">
      <button class="btn primary" type="submit">Apply</button>
      <button class="btn danger" type="button" id="del-contact">Delete</button>
    </div>
  </form>`;
}

function viewChannels() {
  const rows = state.codeplug.channels.map((ch, i) => {
    const dig = ch.mode === 1 || ch.mode === 3;
    return `<tr data-i="${i}" class="${state.selected.channels === i ? "selected" : ""}">
      <td class="mono">${ch.index + 1}</td>
      <td>${esc(ch.name)}</td>
      <td><span class="tag ${dig ? "digital" : "analog"}">${MODE[ch.mode] || ch.mode}</span></td>
      <td class="mono">${mhz(ch.rx_mhz)}</td>
      <td class="mono">${mhz(ch.tx_mhz)}</td>
      <td>${POWER[ch.power] || ch.power}</td>
      <td class="mono">${dig ? `CC${ch.color_code} TS${ch.timeslot}` : "—"}</td>
    </tr>`;
  }).join("");
  const sel = state.codeplug.channels[state.selected.channels];
  return `
    <div class="panel-head">
      <div>
        <h1>Channels</h1>
        <p>Analog and DMR memories. Assign a contact, radio ID, scan list, and RX group on digital channels.
           Use Insert / Move to place a channel with a group instead of only appending at the end.</p>
      </div>
      <div class="actions">
        <button class="btn" id="insert-channel" ${state.selected.channels < 0 ? "disabled" : ""}>Insert after selected</button>
        <button class="btn primary" id="add-channel">Add at end</button>
      </div>
    </div>
    <div class="editor">
      <div class="table-wrap"><table>
        <thead><tr><th>#</th><th>Name</th><th>Type</th><th>RX</th><th>TX</th><th>Power</th><th>DMR</th></tr></thead>
        <tbody id="channels-table">${rows || `<tr><td colspan="7">No channels yet</td></tr>`}</tbody>
      </table></div>
      ${sel ? channelForm(sel) : `<div class="empty">Select a channel</div>`}
    </div>`;
}

function contactOptions(selected) {
  const opts = [`<option value="-1">— None —</option>`];
  for (const c of state.codeplug.contacts) {
    opts.push(`<option value="${c.index}" ${selected === c.index ? "selected" : ""}>${esc(c.name)} (${c.number})</option>`);
  }
  return opts.join("");
}

function radioIdOptions(selected) {
  const opts = [`<option value="-1">— Default —</option>`];
  for (const r of state.codeplug.radio_ids) {
    opts.push(`<option value="${r.index}" ${selected === r.index ? "selected" : ""}>${esc(r.name)} (${r.number})</option>`);
  }
  return opts.join("");
}

function scanOptions(selected) {
  const opts = [`<option value="-1">— None —</option>`];
  for (const s of state.codeplug.scan_lists) {
    opts.push(`<option value="${s.index}" ${selected === s.index ? "selected" : ""}>${esc(s.name)}</option>`);
  }
  return opts.join("");
}

function rxGroupOptions(selected) {
  const opts = [`<option value="-1">— None —</option>`];
  for (const g of state.codeplug.rx_groups || []) {
    opts.push(`<option value="${g.index}" ${selected === g.index ? "selected" : ""}>${esc(g.name)}</option>`);
  }
  return opts.join("");
}

function channelForm(ch) {
  const i = state.selected.channels;
  const atTop = i <= 0;
  const atBottom = i < 0 || i >= state.codeplug.channels.length - 1;
  return `<form class="form" id="channel-form">
    <h2>Edit channel</h2>
    <div class="form-grid">
      <label>Slot index<input class="mono" name="index" type="number" min="0" max="3999" value="${ch.index}" /></label>
      <label>Name<input name="name" maxlength="16" value="${esc(ch.name)}" /></label>
      <label>Mode
        <select name="mode">${MODE.map((m, i) => `<option value="${i}" ${ch.mode === i ? "selected" : ""}>${m}</option>`).join("")}</select>
      </label>
      <label>Power
        <select name="power">${POWER.map((p, i) => `<option value="${i}" ${ch.power === i ? "selected" : ""}>${p}</option>`).join("")}</select>
      </label>
      <label>RX MHz<input class="mono" name="rx_mhz" type="number" step="0.00001" value="${ch.rx_mhz}" /></label>
      <label>TX MHz<input class="mono" name="tx_mhz" type="number" step="0.00001" value="${ch.tx_mhz}" /></label>
      <label>Color code<input class="mono" name="color_code" type="number" min="0" max="15" value="${ch.color_code}" /></label>
      <label>Time slot
        <select name="timeslot">
          <option value="1" ${ch.timeslot === 1 ? "selected" : ""}>TS1</option>
          <option value="2" ${ch.timeslot === 2 ? "selected" : ""}>TS2</option>
        </select>
      </label>
      <label>TX contact<select name="contact_index">${contactOptions(ch.contact_index)}</select></label>
      <label>Radio ID<select name="radio_id_index">${radioIdOptions(ch.radio_id_index)}</select></label>
      <label>Scan list<select name="scan_list_index">${scanOptions(ch.scan_list_index)}</select></label>
      <label>RX group list<select name="rx_group_index">${rxGroupOptions(ch.rx_group_index)}</select></label>
      <label>RX CTCSS<input class="mono" name="rx_ctcss" type="number" step="0.1" value="${ch.rx_ctcss || 0}" /></label>
      <label>TX CTCSS<input class="mono" name="tx_ctcss" type="number" step="0.1" value="${ch.tx_ctcss || 0}" /></label>
      <label><span>RX only</span><select name="rx_only">
        <option value="0" ${!ch.rx_only ? "selected" : ""}>No</option>
        <option value="1" ${ch.rx_only ? "selected" : ""}>Yes</option>
      </select></label>
      <label>Bandwidth
        <select name="bandwidth_wide">
          <option value="0" ${!ch.bandwidth_wide ? "selected" : ""}>12.5 kHz</option>
          <option value="1" ${ch.bandwidth_wide ? "selected" : ""}>25 kHz</option>
        </select>
      </label>
    </div>
    <div class="form-actions">
      <button class="btn" type="button" id="move-channel-up" ${atTop ? "disabled" : ""}>↑ Move up</button>
      <button class="btn" type="button" id="move-channel-down" ${atBottom ? "disabled" : ""}>↓ Move down</button>
      <button class="btn primary" type="submit">Apply</button>
      <button class="btn danger" type="button" id="del-channel">Delete</button>
    </div>
  </form>`;
}

function viewZones() {
  const rows = state.codeplug.zones.map((z, i) => `
    <tr data-i="${i}" class="${state.selected.zones === i ? "selected" : ""}">
      <td class="mono">${z.index + 1}</td>
      <td>${esc(z.name)}</td>
      <td class="mono">${z.channels.length}</td>
    </tr>`).join("");
  const sel = state.codeplug.zones[state.selected.zones];
  return `
    <div class="panel-head">
      <div>
        <h1>Zones</h1>
        <p>Zones group channels for the channel knob / menu. A channel is only usable on the radio if it belongs to a zone.
           Use Insert / Move to reorder the zone list.</p>
      </div>
      <div class="actions">
        <button class="btn" id="insert-zone" ${state.selected.zones < 0 ? "disabled" : ""}>Insert after selected</button>
        <button class="btn primary" id="add-zone">Add at end</button>
      </div>
    </div>
    <div class="editor">
      <div class="table-wrap"><table>
        <thead><tr><th>#</th><th>Name</th><th>Members</th></tr></thead>
        <tbody id="zones-table">${rows || `<tr><td colspan="3">No zones yet</td></tr>`}</tbody>
      </table></div>
      ${sel ? zoneForm(sel) : `<div class="empty">Select a zone</div>`}
    </div>`;
}

function zoneForm(z) {
  const i = state.selected.zones;
  const atTop = i <= 0;
  const atBottom = i < 0 || i >= state.codeplug.zones.length - 1;
  const memberSet = new Set(z.channels);
  const available = state.codeplug.channels
    .filter((ch) => !memberSet.has(ch.index))
    .map((ch) => `<option value="${ch.index}">${ch.index + 1}: ${esc(ch.name)}</option>`)
    .join("");
  const members = z.channels.map((ci) => {
    const ch = state.codeplug.channels.find((c) => c.index === ci);
    const label = ch ? `${ci + 1}: ${ch.name}` : `${ci + 1}`;
    return `<option value="${ci}">${esc(label)}</option>`;
  }).join("");
  return `<form class="form" id="zone-form">
    <h2>Edit zone</h2>
    <label>Name<input name="name" maxlength="16" value="${esc(z.name)}" /></label>
    <label>Slot index<input class="mono" name="index" type="number" min="0" max="249" value="${z.index}" /></label>
    <div class="form-grid">
      <label>Available channels<select id="zone-available" multiple size="10">${available}</select></label>
      <label>Zone members<select id="zone-members" name="channels" multiple size="10">${members}</select></label>
    </div>
    <div class="form-actions">
      <button class="btn" type="button" id="move-zone-up" ${atTop ? "disabled" : ""}>↑ Move up</button>
      <button class="btn" type="button" id="move-zone-down" ${atBottom ? "disabled" : ""}>↓ Move down</button>
      <button class="btn" type="button" id="zone-add">Add →</button>
      <button class="btn" type="button" id="zone-remove">← Remove</button>
      <button class="btn primary" type="submit">Apply</button>
      <button class="btn danger" type="button" id="del-zone">Delete</button>
    </div>
  </form>`;
}

function viewScanLists() {
  const rows = state.codeplug.scan_lists.map((s, i) => `
    <tr data-i="${i}" class="${state.selected["scan-lists"] === i ? "selected" : ""}">
      <td class="mono">${s.index + 1}</td>
      <td>${esc(s.name)}</td>
      <td class="mono">${s.members.length}</td>
    </tr>`).join("");
  const sel = state.codeplug.scan_lists[state.selected["scan-lists"]];
  return `
    <div class="panel-head">
      <div>
        <h1>Scan Lists</h1>
        <p>Channels scanned when scan is started from a channel that references this list.</p>
      </div>
      <button class="btn primary" id="add-scan">Add scan list</button>
    </div>
    <div class="editor">
      <div class="table-wrap"><table>
        <thead><tr><th>#</th><th>Name</th><th>Members</th></tr></thead>
        <tbody id="scan-lists-table">${rows || `<tr><td colspan="3">No scan lists yet</td></tr>`}</tbody>
      </table></div>
      ${sel ? scanForm(sel) : `<div class="empty">Select a scan list</div>`}
    </div>`;
}

function scanForm(s) {
  const memberSet = new Set(s.members);
  const available = state.codeplug.channels
    .filter((ch) => !memberSet.has(ch.index))
    .map((ch) => `<option value="${ch.index}">${ch.index + 1}: ${esc(ch.name)}</option>`)
    .join("");
  const members = s.members.map((ci) => {
    const ch = state.codeplug.channels.find((c) => c.index === ci);
    const label = ch ? `${ci + 1}: ${ch.name}` : `${ci + 1}`;
    return `<option value="${ci}">${esc(label)}</option>`;
  }).join("");
  return `<form class="form" id="scan-form">
    <h2>Edit scan list</h2>
    <label>Name<input name="name" maxlength="16" value="${esc(s.name)}" /></label>
    <label>Slot index<input class="mono" name="index" type="number" min="0" max="249" value="${s.index}" /></label>
    <div class="form-grid">
      <label>Available<select id="scan-available" multiple size="10">${available}</select></label>
      <label>Members<select id="scan-members" multiple size="10">${members}</select></label>
    </div>
    <div class="form-actions">
      <button class="btn" type="button" id="scan-add">Add →</button>
      <button class="btn" type="button" id="scan-remove">← Remove</button>
      <button class="btn primary" type="submit">Apply</button>
      <button class="btn danger" type="button" id="del-scan">Delete</button>
    </div>
  </form>`;
}

function viewRxGroups() {
  const list = state.codeplug.rx_groups || [];
  const rows = list.map((g, i) => `
    <tr data-i="${i}" class="${state.selected["rx-groups"] === i ? "selected" : ""}">
      <td class="mono">${g.index + 1}</td>
      <td>${esc(g.name)}</td>
      <td class="mono">${g.members.length}</td>
    </tr>`).join("");
  const sel = list[state.selected["rx-groups"]];
  return `
    <div class="panel-head">
      <div>
        <h1>RX Group Lists</h1>
        <p>Talk groups a digital channel can receive. Assign a list on each digital channel.</p>
      </div>
      <button class="btn primary" id="add-rxg">Add RX group</button>
    </div>
    <div class="editor">
      <div class="table-wrap"><table>
        <thead><tr><th>#</th><th>Name</th><th>Members</th></tr></thead>
        <tbody id="rx-groups-table">${rows || `<tr><td colspan="3">No RX groups yet</td></tr>`}</tbody>
      </table></div>
      ${sel ? rxGroupForm(sel) : `<div class="empty">Select an RX group list</div>`}
    </div>`;
}

function rxGroupForm(g) {
  const memberSet = new Set(g.members);
  const available = state.codeplug.contacts
    .filter((c) => !memberSet.has(c.index))
    .map((c) => `<option value="${c.index}">${esc(c.name)} (${c.number})</option>`)
    .join("");
  const members = g.members.map((ci) => {
    const c = state.codeplug.contacts.find((x) => x.index === ci);
    const label = c ? `${c.name} (${c.number})` : `#${ci}`;
    return `<option value="${ci}">${esc(label)}</option>`;
  }).join("");
  return `<form class="form" id="rxg-form">
    <h2>Edit RX group</h2>
    <label>Name<input name="name" maxlength="16" value="${esc(g.name)}" /></label>
    <label>Slot index<input class="mono" name="index" type="number" min="0" max="249" value="${g.index}" /></label>
    <div class="form-grid">
      <label>Available contacts<select id="rxg-available" multiple size="10">${available}</select></label>
      <label>Members<select id="rxg-members" multiple size="10">${members}</select></label>
    </div>
    <div class="form-actions">
      <button class="btn" type="button" id="rxg-add">Add →</button>
      <button class="btn" type="button" id="rxg-remove">← Remove</button>
      <button class="btn primary" type="submit">Apply</button>
      <button class="btn danger" type="button" id="del-rxg">Delete</button>
    </div>
  </form>`;
}

function esc(s) {
  return String(s ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/"/g, "&quot;");
}

function formData(form) {
  const fd = new FormData(form);
  const o = {};
  for (const [k, v] of fd.entries()) o[k] = v;
  return o;
}

function bindViewHandlers() {
  const pick = (tableId, key) => {
    const t = document.getElementById(tableId);
    if (!t) return;
    t.onclick = (e) => {
      const tr = e.target.closest("tr[data-i]");
      if (!tr) return;
      state.selected[key] = Number(tr.dataset.i);
      render();
    };
  };
  pick("radio-ids-table", "radio-ids");
  pick("contacts-table", "contacts");
  pick("channels-table", "channels");
  pick("zones-table", "zones");
  pick("scan-lists-table", "scan-lists");
  pick("rx-groups-table", "rx-groups");

  const settings = document.getElementById("settings-form");
  if (settings) {
    settings.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(settings);
      state.codeplug.settings.intro_line1 = o.intro_line1.slice(0, 16);
      state.codeplug.settings.intro_line2 = o.intro_line2.slice(0, 16);
      markDirty();
      render();
    };
  }

  document.querySelectorAll("[data-opt]").forEach((btn) => {
    btn.onclick = () => {
      state.optionalSection = btn.dataset.opt;
      render();
    };
  });
  const optForm = document.getElementById("optional-form");
  if (optForm) {
    optForm.onsubmit = (e) => {
      e.preventDefault();
      const section = state.optionalSection;
      if (!section) return;
      if (!state.codeplug.optional_settings) state.codeplug.optional_settings = {};
      if (!state.codeplug.optional_settings[section]) state.codeplug.optional_settings[section] = {};
      const o = formData(optForm);
      const schemaEl = schemaElement(section);
      for (const [k, v] of Object.entries(o)) {
        const def = schemaEl?.fields?.find((f) => f.name === k);
        if (def?.type === "string") state.codeplug.optional_settings[section][k] = v;
        else state.codeplug.optional_settings[section][k] = Number(v);
      }
      markDirty();
      setStatus(`Updated ${section}`);
    };
  }

  document.querySelectorAll("[data-aprs-tab]").forEach((btn) => {
    btn.onclick = () => {
      state.aprsTab = btn.dataset.aprsTab;
      render();
    };
  });

  const applyCrossSectionForm = (formId, statusMsg) => {
    const form = document.getElementById(formId);
    if (!form) return;
    form.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(form);
      for (const [k, v] of Object.entries(o)) {
        const [sec, name] = k.split("::");
        if (!sec || !name) continue;
        const block = optBlock(sec);
        const def = fieldDef(sec, name);
        block[name] = def?.type === "string" ? v : Number(v);
      }
      markDirty();
      setStatus(statusMsg);
      render();
    };
  };
  applyCrossSectionForm("gps-receiver-form", "GPS settings updated");
  applyCrossSectionForm("aprs-options-form", "APRS options updated");

  const applyAprsFields = (form, target, schemaName, transform) => {
    if (!form) return;
    form.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(form);
      const defs = schemaElement(schemaName)?.fields || [];
      for (const [k, v] of Object.entries(o)) {
        if (k.includes("::")) continue;
        const def = defs.find((f) => f.name === k);
        let val = def?.type === "string" ? v : Number(v);
        if (transform) val = transform(k, val, v);
        target[k] = val;
      }
      markDirty();
      setStatus("APRS settings updated");
      render();
    };
  };

  applyAprsFields(document.getElementById("aprs-identity-form"), optBlock("APRS settings"), "APRS settings");
  applyAprsFields(document.getElementById("aprs-fm-form"), optBlock("APRS settings"), "APRS settings", (k, val, raw) => {
    if (k.startsWith("FM APRS Frequency")) return mhzToBcd(raw);
    return val;
  });

  /* APRS identity helpers — chip grid, path preset, table swap (no server restart needed). */
  const symChips = document.getElementById("aprs-sym-chips");
  const symSelect = document.getElementById("aprs-sym-select");
  const symTable = document.getElementById("aprs-sym-table");
  if (symChips && symSelect) {
    const syncChips = (code) => {
      const c = String(code);
      symChips.querySelectorAll(".sym-chip").forEach((el) => {
        el.classList.toggle("active", el.dataset.aprsSym === c);
      });
    };
    const rebuildSymbolChoices = (tableCode, keepSym) => {
      const list = Number(tableCode) === 92 ? APRS_SYMBOLS_ALT : APRS_SYMBOLS_PRIMARY;
      const cur = Number(keepSym);
      symSelect.innerHTML = "";
      const codes = new Set();
      for (const s of list) {
        const code = s.ch.charCodeAt(0);
        codes.add(code);
        symSelect.add(new Option(`${s.ch} — ${s.name}`, String(code), code === cur, code === cur));
      }
      if (!codes.has(cur)) {
        const ch = cur >= 32 && cur < 127 ? String.fromCharCode(cur) : "?";
        symSelect.add(new Option(`Custom ${ch} (${cur})`, String(cur), true, true), 0);
      }
      symChips.innerHTML = list.map((s) => {
        const code = s.ch.charCodeAt(0);
        const active = code === cur ? "active" : "";
        return `<button type="button" class="sym-chip ${active}" data-aprs-sym="${code}" title="${esc(s.name)}">
          <span class="sym-glyph">${esc(s.ch)}</span><span class="sym-name">${esc(s.name)}</span>
        </button>`;
      }).join("");
      const preview = document.querySelector(".sym-preview");
      if (preview) {
        const tg = Number(tableCode) >= 32 && Number(tableCode) < 127 ? String.fromCharCode(Number(tableCode)) : "?";
        const sg = cur >= 32 && cur < 127 ? String.fromCharCode(cur) : "?";
        preview.textContent = `${tg}${sg}`;
      }
    };
    symChips.onclick = (e) => {
      const btn = e.target.closest("[data-aprs-sym]");
      if (!btn) return;
      const code = btn.dataset.aprsSym;
      if (![...symSelect.options].some((o) => o.value === code)) {
        const ch = Number(code) >= 32 && Number(code) < 127 ? String.fromCharCode(Number(code)) : "?";
        symSelect.add(new Option(`${ch} — selected`, code, true, true));
      }
      symSelect.value = code;
      syncChips(code);
      const preview = document.querySelector(".sym-preview");
      const tableEl = document.getElementById("aprs-sym-table");
      if (preview && tableEl) {
        const tg = Number(tableEl.value) >= 32 && Number(tableEl.value) < 127
          ? String.fromCharCode(Number(tableEl.value)) : "?";
        const sg = Number(code) >= 32 && Number(code) < 127 ? String.fromCharCode(Number(code)) : "?";
        preview.textContent = `${tg}${sg}`;
      }
    };
    symSelect.onchange = () => {
      syncChips(symSelect.value);
      const preview = document.querySelector(".sym-preview");
      const tableEl = document.getElementById("aprs-sym-table");
      if (preview && tableEl) {
        const tg = Number(tableEl.value) >= 32 && Number(tableEl.value) < 127
          ? String.fromCharCode(Number(tableEl.value)) : "?";
        const sg = Number(symSelect.value) >= 32 && Number(symSelect.value) < 127
          ? String.fromCharCode(Number(symSelect.value)) : "?";
        preview.textContent = `${tg}${sg}`;
      }
    };
    if (symTable) {
      symTable.onchange = () => {
        rebuildSymbolChoices(symTable.value, symSelect.value);
      };
    }
  } else if (symTable) {
    symTable.onchange = () => {
      const a = optBlock("APRS settings");
      a["APRS symbol table"] = Number(symTable.value);
      if (symSelect) a["APRS symbol"] = Number(symSelect.value);
      markDirty();
      render();
    };
  }
  const pathPreset = document.getElementById("aprs-path-preset");
  if (pathPreset) {
    pathPreset.onchange = () => {
      if (pathPreset.value === "__keep__") return;
      const p1 = document.getElementById("aprs-path-1");
      const p2 = document.getElementById("aprs-path-2");
      if (p1) p1.value = pathPreset.value;
      if (p2) p2.value = "";
    };
  }
  document.querySelectorAll("[data-aprs-freq]").forEach((btn) => {
    btn.onclick = () => {
      const mhz = btn.dataset.aprsFreq;
      const form = document.getElementById("aprs-fm-form");
      if (!form) return;
      for (let i = 0; i < 8; i++) {
        const inp = form.querySelector(`[name="FM APRS Frequency [${i}]"]`);
        if (inp) inp.value = Number(mhz).toFixed(5);
      }
      setStatus(`Filled FM APRS frequencies with ${mhz} MHz — click Apply to keep`);
    };
  });
  const dmrForm = document.getElementById("aprs-dmr-form");
  if (dmrForm) {
    dmrForm.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(dmrForm);
      const aprs = optBlock("APRS settings");
      const msg = optBlock("DMR APRS message");
      for (const [k, v] of Object.entries(o)) {
        if (k === "DMR APRS message") msg[k] = v;
        else aprs[k] = Number(v);
      }
      markDirty();
      setStatus("DMR APRS updated");
      render();
    };
  }

  pick("aprs-filters-table", "aprs-filters");
  const filtForm = document.getElementById("aprs-filter-form");
  if (filtForm) {
    filtForm.onsubmit = (e) => {
      e.preventDefault();
      const list = optBank("APRS filter", 32, { "Enable filter": 0, Call: "", SSID: 16 });
      const i = state.selected["aprs-filters"] || 0;
      const o = formData(filtForm);
      const defs = schemaElement("APRS filter")?.fields || [];
      for (const [k, v] of Object.entries(o)) {
        const def = defs.find((f) => f.name === k);
        list[i][k] = def?.type === "string" ? v : Number(v);
      }
      markDirty();
      render();
    };
  }

  pick("aprs-zones-table", "aprs-zones");
  const zoneForm = document.getElementById("aprs-zone-form");
  if (zoneForm) {
    zoneForm.onsubmit = (e) => {
      e.preventDefault();
      const list = optBank("GPS roaming zone", 32, {});
      const i = state.selected["aprs-zones"] || 0;
      const o = formData(zoneForm);
      const defs = schemaElement("GPS roaming zone")?.fields || [];
      for (const [k, v] of Object.entries(o)) {
        const def = defs.find((f) => f.name === k);
        list[i][k] = def?.type === "string" ? v : Number(v);
      }
      markDirty();
      render();
    };
  }

  document.querySelectorAll("[data-tab-key]").forEach((btn) => {
    btn.onclick = () => {
      state[btn.dataset.tabKey] = btn.dataset.tabId;
      render();
    };
  });

  const bindSchemaForm = (form) => {
    if (!form) return;
    form.onsubmit = (e) => {
      e.preventDefault();
      const elName = form.dataset.schemaElement;
      if (!elName) return;
      const block = optBlock(elName);
      const o = formData(form);
      const defs = schemaElement(elName)?.fields || [];
      for (const [k, v] of Object.entries(o)) {
        const def = defs.find((f) => f.name === k);
        block[k] = def?.type === "string" ? v : Number(v);
      }
      markDirty();
      setStatus(`Updated ${elName}`);
      render();
    };
  };
  document.querySelectorAll("form[data-schema-element]").forEach(bindSchemaForm);

  const bindBankForm = (form) => {
    if (!form) return;
    form.onsubmit = (e) => {
      e.preventDefault();
      const elName = form.dataset.bankElement;
      const selKey = form.dataset.bankSelected;
      if (!elName || !selKey) return;
      const list = optBank(elName, bankCount(elName, 1), blankFromSchema(elName));
      const i = state.selected[selKey] || 0;
      const o = formData(form);
      const defs = schemaElement(elName)?.fields || [];
      for (const [k, v] of Object.entries(o)) {
        const def = defs.find((f) => f.name === k);
        list[i][k] = def?.type === "string" ? v : Number(v);
      }
      markDirty();
      setStatus(`Updated ${elName} #${i + 1}`);
      render();
    };
  };
  document.querySelectorAll("form[data-bank-element]").forEach(bindBankForm);

  [
    ["roam-ch-table", "roam-ch"],
    ["roam-zone-table", "roam-zone"],
    ["hotkeys-table", "hotkeys"],
    ["dtmf-contacts-table", "dtmf-contacts"],
    ["tone2-id-table", "tone2-id"],
    ["tone2-fn-table", "tone2-fn"],
    ["tone5-id-table", "tone5-id"],
    ["tone5-fn-table", "tone5-fn"],
    ["enc-dmr-table", "enc-dmr"],
    ["enc-arc4-table", "enc-arc4"],
  ].forEach(([tid, key]) => pick(tid, key));

  const gotoAes = document.getElementById("goto-aes-optional");
  if (gotoAes) {
    gotoAes.onclick = () => {
      state.view = "optional";
      state.optionalSection = "AES encryption key bitmap";
      document.querySelectorAll(".nav-item").forEach((b) => b.classList.remove("active"));
      document.querySelector('.nav-item[data-view="optional"]')?.classList.add("active");
      render();
    };
  }

  const addRid = document.getElementById("add-radio-id");
  if (addRid) addRid.onclick = () => {
    const index = nextIndex(state.codeplug.radio_ids);
    state.codeplug.radio_ids.push({ index, name: "ID", number: 0 });
    state.selected["radio-ids"] = state.codeplug.radio_ids.length - 1;
    markDirty(); render();
  };
  const ridForm = document.getElementById("radio-id-form");
  if (ridForm) {
    ridForm.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(ridForm);
      const r = state.codeplug.radio_ids[state.selected["radio-ids"]];
      r.index = Number(o.index); r.name = o.name.slice(0, 16); r.number = Number(o.number);
      markDirty(); render();
    };
    document.getElementById("del-radio-id").onclick = () => {
      state.codeplug.radio_ids.splice(state.selected["radio-ids"], 1);
      state.selected["radio-ids"] = -1; markDirty(); render();
    };
  }

  const addC = document.getElementById("add-contact");
  if (addC) addC.onclick = () => {
    const index = nextIndex(state.codeplug.contacts);
    state.codeplug.contacts.push({ index, name: "TG", number: 9, type: 1, alert: 0 });
    state.selected.contacts = state.codeplug.contacts.length - 1;
    markDirty(); render();
  };
  const cForm = document.getElementById("contact-form");
  if (cForm) {
    cForm.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(cForm);
      const c = state.codeplug.contacts[state.selected.contacts];
      c.index = Number(o.index); c.name = o.name.slice(0, 16);
      c.number = Number(o.number); c.type = Number(o.type); c.alert = Number(o.alert);
      markDirty(); render();
    };
    document.getElementById("del-contact").onclick = () => {
      state.codeplug.contacts.splice(state.selected.contacts, 1);
      state.selected.contacts = -1; markDirty(); render();
    };
  }

  const addCh = document.getElementById("add-channel");
  if (addCh) addCh.onclick = () => {
    const index = nextIndex(state.codeplug.channels);
    state.codeplug.channels.push(newBlankChannel(index));
    state.selected.channels = state.codeplug.channels.length - 1;
    markDirty(); render();
  };
  const insertCh = document.getElementById("insert-channel");
  if (insertCh) insertCh.onclick = () => {
    insertChannelAfterSelected();
    markDirty(); render();
  };
  const chForm = document.getElementById("channel-form");
  if (chForm) {
    chForm.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(chForm);
      const ch = state.codeplug.channels[state.selected.channels];
      Object.assign(ch, {
        index: Number(o.index), name: o.name.slice(0, 16),
        mode: Number(o.mode), power: Number(o.power),
        rx_mhz: Number(o.rx_mhz), tx_mhz: Number(o.tx_mhz),
        color_code: Number(o.color_code), timeslot: Number(o.timeslot),
        contact_index: Number(o.contact_index), radio_id_index: Number(o.radio_id_index),
        scan_list_index: Number(o.scan_list_index),
        rx_group_index: Number(o.rx_group_index),
        rx_ctcss: Number(o.rx_ctcss), tx_ctcss: Number(o.tx_ctcss),
        rx_only: o.rx_only === "1", bandwidth_wide: o.bandwidth_wide === "1",
      });
      markDirty(); render();
    };
    document.getElementById("del-channel").onclick = () => {
      state.codeplug.channels.splice(state.selected.channels, 1);
      state.selected.channels = -1; markDirty(); render();
    };
    const up = document.getElementById("move-channel-up");
    if (up) up.onclick = () => {
      if (moveChannel(state.selected.channels, -1)) { markDirty(); render(); }
    };
    const down = document.getElementById("move-channel-down");
    if (down) down.onclick = () => {
      if (moveChannel(state.selected.channels, 1)) { markDirty(); render(); }
    };
  }

  wireMoveList("zone-available", "zone-members", "zone-add", "zone-remove");
  const zForm = document.getElementById("zone-form");
  if (zForm) {
    zForm.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(zForm);
      const z = state.codeplug.zones[state.selected.zones];
      z.index = Number(o.index); z.name = o.name.slice(0, 16);
      z.channels = [...document.getElementById("zone-members").options].map((x) => Number(x.value));
      markDirty(); render();
    };
    const delZ = document.getElementById("del-zone");
    if (delZ) delZ.onclick = () => {
      state.codeplug.zones.splice(state.selected.zones, 1);
      state.selected.zones = -1; markDirty(); render();
    };
    const upZ = document.getElementById("move-zone-up");
    if (upZ) upZ.onclick = () => {
      if (moveZone(state.selected.zones, -1)) { markDirty(); render(); }
    };
    const downZ = document.getElementById("move-zone-down");
    if (downZ) downZ.onclick = () => {
      if (moveZone(state.selected.zones, 1)) { markDirty(); render(); }
    };
  }
  const addZ = document.getElementById("add-zone");
  if (addZ) addZ.onclick = () => {
    const index = nextIndex(state.codeplug.zones);
    state.codeplug.zones.push({ index, name: "Zone", channels: [] });
    state.selected.zones = state.codeplug.zones.length - 1;
    markDirty(); render();
  };
  const insertZ = document.getElementById("insert-zone");
  if (insertZ) insertZ.onclick = () => {
    insertZoneAfterSelected();
    markDirty(); render();
  };

  wireMoveList("scan-available", "scan-members", "scan-add", "scan-remove");
  const sForm = document.getElementById("scan-form");
  if (sForm) {
    sForm.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(sForm);
      const s = state.codeplug.scan_lists[state.selected["scan-lists"]];
      s.index = Number(o.index); s.name = o.name.slice(0, 16);
      s.members = [...document.getElementById("scan-members").options].map((x) => Number(x.value));
      markDirty(); render();
    };
    document.getElementById("del-scan").onclick = () => {
      state.codeplug.scan_lists.splice(state.selected["scan-lists"], 1);
      state.selected["scan-lists"] = -1; markDirty(); render();
    };
  }
  const addS = document.getElementById("add-scan");
  if (addS) addS.onclick = () => {
    const index = nextIndex(state.codeplug.scan_lists);
    state.codeplug.scan_lists.push({ index, name: "Scan", members: [] });
    state.selected["scan-lists"] = state.codeplug.scan_lists.length - 1;
    markDirty(); render();
  };

  wireMoveList("rxg-available", "rxg-members", "rxg-add", "rxg-remove");
  const gForm = document.getElementById("rxg-form");
  if (gForm) {
    gForm.onsubmit = (e) => {
      e.preventDefault();
      const o = formData(gForm);
      const g = state.codeplug.rx_groups[state.selected["rx-groups"]];
      g.index = Number(o.index); g.name = o.name.slice(0, 16);
      g.members = [...document.getElementById("rxg-members").options].map((x) => Number(x.value));
      markDirty(); render();
    };
    document.getElementById("del-rxg").onclick = () => {
      state.codeplug.rx_groups.splice(state.selected["rx-groups"], 1);
      state.selected["rx-groups"] = -1; markDirty(); render();
    };
  }
  const addG = document.getElementById("add-rxg");
  if (addG) addG.onclick = () => {
    if (!state.codeplug.rx_groups) state.codeplug.rx_groups = [];
    const index = nextIndex(state.codeplug.rx_groups);
    state.codeplug.rx_groups.push({ index, name: "RX Group", members: [] });
    state.selected["rx-groups"] = state.codeplug.rx_groups.length - 1;
    markDirty(); render();
  };
}

function wireMoveList(availId, memId, addId, remId) {
  const add = document.getElementById(addId);
  const rem = document.getElementById(remId);
  if (!add || !rem) return;
  add.onclick = () => {
    const a = document.getElementById(availId);
    const m = document.getElementById(memId);
    [...a.selectedOptions].forEach((opt) => m.add(opt));
  };
  rem.onclick = () => {
    const a = document.getElementById(availId);
    const m = document.getElementById(memId);
    [...m.selectedOptions].forEach((opt) => a.add(opt));
  };
}

/* ---------- top-level actions ---------- */

document.querySelectorAll(".nav-item[data-view]").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".nav-item").forEach((b) => b.classList.remove("active"));
    btn.classList.add("active");
    state.view = btn.dataset.view;
    render();
  });
});

document.getElementById("btn-read").onclick = async () => {
  const started = Date.now();
  const tick = setInterval(() => {
    const sec = Math.floor((Date.now() - started) / 1000);
    setStatus(`Reading radio… ${sec}s elapsed (FW map — often 2–5 min)`);
  }, 500);
  setStatus("Reading radio… 0s elapsed (FW map — often 2–5 min)");
  try {
    const data = await api("POST", "/api/radio/read");
    state.codeplug = data.codeplug;
    if (!state.codeplug.optional_settings) state.codeplug.optional_settings = {};
    if (!state.codeplug.rx_groups) state.codeplug.rx_groups = [];
    state.imageLoaded = true;
    state.dirty = false;
    render();
    const sec = Math.floor((Date.now() - started) / 1000);
    setStatus(`Read OK in ${sec}s — ${state.codeplug.model} · ${(state.codeplug.rx_groups || []).length} RX groups · map ${state.codeplug.schema_firmware || "?"}`);
  } catch (e) {
    setStatus(`Read failed: ${e.message}`);
  } finally {
    clearInterval(tick);
  }
};

document.getElementById("btn-write").onclick = async () => {
  if (!confirm("Write the current codeplug to the radio? This overwrites radio memory.")) return;
  const started = Date.now();
  const tick = setInterval(() => {
    const sec = Math.floor((Date.now() - started) / 1000);
    setStatus(`Writing radio… ${sec}s elapsed`);
  }, 500);
  setStatus("Writing radio… 0s elapsed");
  try {
    await api("POST", "/api/radio/write", state.codeplug);
    state.dirty = false;
    updateStatusLine();
    const sec = Math.floor((Date.now() - started) / 1000);
    setStatus(`Write complete in ${sec}s`);
  } catch (e) {
    setStatus(`Write failed: ${e.message}`);
  } finally {
    clearInterval(tick);
  }
};

document.getElementById("file-atcp").onchange = async (e) => {
  const file = e.target.files?.[0];
  if (!file) return;
  const buf = await file.arrayBuffer();
  setStatus(`Importing ${file.name} (${buf.byteLength} bytes)…`);
  try {
    const res = await fetch("/api/import", {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream" },
      body: buf,
    });
    const data = await res.json();
    if (!res.ok || !data.ok) throw new Error(data.error || "import failed");
    if (!data.codeplug) throw new Error("import returned no codeplug");
    state.codeplug = data.codeplug;
    if (!state.codeplug.optional_settings) state.codeplug.optional_settings = {};
    state.imageLoaded = true;
    state.dirty = false;
    state.selected = {
      "radio-ids": 0, contacts: 0, channels: 0, zones: 0,
      "scan-lists": 0, "rx-groups": 0,
      "aprs-filters": 0, "aprs-zones": 0,
      "roam-zone": 0, "roam-ch": 0, hotkeys: 0,
      "dtmf-contacts": 0, "tone2-id": 0, "tone2-fn": 0,
      "tone5-id": 0, "tone5-fn": 0, "enc-arc4": 0, "enc-dmr": 0,
    };
    const nch = (state.codeplug.channels || []).length;
    const nct = (state.codeplug.contacts || []).length;
    const nz = (state.codeplug.zones || []).length;
    render();
    setStatus(`Loaded ${file.name}: ${nch} channels, ${nct} contacts, ${nz} zones`);
  } catch (err) {
    setStatus(`Import failed: ${err.message}`);
  } finally {
    e.target.value = "";
  }
};

document.getElementById("btn-save-atcp").onclick = async () => {
  try {
    await api("PUT", "/api/codeplug", state.codeplug);
    const res = await fetch("/api/export");
    if (!res.ok) throw new Error("export failed");
    const blob = await res.blob();
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `${state.codeplug.model || "codeplug"}.atcp`;
    a.click();
    state.dirty = false;
    updateStatusLine();
  } catch (e) {
    setStatus(`Save failed: ${e.message}`);
  }
};

/* Load schema + in-memory codeplug */
Promise.all([
  api("GET", "/api/schema").catch(() => null),
  api("GET", "/api/codeplug").catch(() => ({ codeplug: null })),
]).then(([schema, data]) => {
  if (schema) state.schema = schema;
  if (data.codeplug) {
    state.codeplug = data.codeplug;
    if (!state.codeplug.optional_settings) state.codeplug.optional_settings = {};
    state.imageLoaded = true;
  }
  render();
});
