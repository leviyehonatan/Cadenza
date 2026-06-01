/* ============================================================
   CADENZA WORKSTATION
   State-driven arranger app + CadenzaAPI + JuceBridge
   ============================================================ */
(function () {
  "use strict";

  /* ============================================================
     CONSTANTS (formerly magic numbers scattered through the file)
     ============================================================ */
  const KNOB_TRAVEL_PX = 150;     // pixels of vertical drag = full knob travel
  const KNOB_DEG_MIN   = -135;
  const KNOB_DEG_MAX   =  135;    // full sweep = 270deg

  const DB_MIN   = -60;
  const DB_MAX   =  12;
  const DB_RANGE = DB_MAX - DB_MIN;  // 72

  const BPM_MIN = 40;
  const BPM_MAX = 240;
  const BPM_DEFAULT = 120;
  const TRANSPOSE_MIN = -12;
  const TRANSPOSE_MAX =  12;
  const OCTAVE_MIN = -4;
  const OCTAVE_MAX =  4;
  const PAN_MIN = -50;
  const PAN_MAX =  50;

  const PIANO_LOW_MIDI  = 0;     // C-2
  const PIANO_HIGH_MIDI = 120;   // C8

  const VU_FPS_MS  = 130;        // approx 7.7Hz; rAF-throttled below
  const CPU_TICK_MS = 700;

  /* ============================================================
     UTILITIES
     ============================================================ */
  const $  = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => [...r.querySelectorAll(s)];
  const el = (tag, cls, html) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (html != null) n.innerHTML = html;
    return n;
  };
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

  /* ============================================================
     CENTRAL STATE
     ============================================================ */
  const State = {
    bpm: BPM_DEFAULT,
    transpose: 0,
    octave: 0,
    chord: "F",
    playing: false,
    recording: false,
    syncroStop: true,   // mirrors ArrangerState::syncroStopOnRelease (default ON)
    key: "C",
    selectedStyle: 1,
    stylePage: 1,
    activeChannels: { left: true, right1: false, right2: false, right3: false },
    activeBass: true,
    activeArranger: true,
    activeMemory: false,
    bankMemory: "Piano",
    styleMemory: 1,
    // volume in dB (DB_MIN..DB_MAX, 0 = unity), pan PAN_MIN..PAN_MAX
    channels: {
      left:   { volume: 0, pan: -8,  solo: false, mute: false, inst: "Acoustic Bass" },
      right1: { volume: 0, pan: 0,   solo: false, mute: false, inst: "Grand Piano" },
      right2: { volume: 0, pan: 14,  solo: false, mute: false, inst: "Warm Strings" },
      right3: { volume: 0, pan: -20, solo: false, mute: false, inst: "Synth Pad" },
      melody: { volume: 0, pan: 0,   solo: false, mute: false, inst: "Tenor Sax" },
      master: { volume: 0, pan: 0,   solo: false, mute: false, inst: "Stereo Out" },
    },
    parts: {
      style: "8 Beat Pop", song: "", part: "", audio: "",
      band: "DEMO", lyrics: "", pad: "", pattern: "",
    },
    factoryStyles: [],
    synthEngine: "Unknown",
    soundFontName: "None",
    soundFontPath: "",
    selectedPart: null,
    pads: [false, false, false, false],
    crossfade: 50,
  };

  /* ============================================================
     EVENT / BRIDGE PLUMBING
     ============================================================ */
  function emit(key, value) {
    document.dispatchEvent(new CustomEvent("cadenza:stateChange", { detail: { key, value } }));
  }

  window.JuceBridge = {
    onBpmChanged: (bpm) => { State.bpm = bpm; renderAll(); },
    onPlayStateChanged: (playing) => { setPlaying(playing, true); },
    onNoteReceived: (note) => { flashNote(note); },
    onNoteOff: (note) => { /* note released — piano key un-flash handled by pointer-up */ },
    onChordChanged: (chord) => { State.chord = chord; renderTransport(); },
    onPluginChanged: (name) => {
      const el = document.getElementById("pluginName");
      if (el) el.textContent = (name && name.length) ? name : "None";
    },
    onSongModeChanged: (active) => {
      State.songMode = !!active;
      const slot = document.querySelector('.part-slot[data-part="song"]');
      if (slot) {
        slot.classList.toggle("active", State.songMode);
        const pn = slot.querySelector("[data-pname]");
        if (pn) pn.textContent = State.songMode ? "▶ chart" : "";
      }
    },
    onFactoryStyles: (styles) => {
      State.factoryStyles = Array.isArray(styles) ? styles : [];
      if (State.factoryStyles.length && State.selectedStyle > State.factoryStyles.length) State.selectedStyle = 1;
      renderStyles();
    },
    onStyleLoaded: (style) => {
      if (style && style.name) {
        State.parts.style = style.name;
        const index = State.factoryStyles.findIndex(s => s.path === style.path || s.id === style.id);
        if (index >= 0) State.selectedStyle = index + 1;
      }
      renderStyles();
      renderParts();
    },
    onRuntimeState: (state) => {
      if (!state) return;
      if (Number.isFinite(state.bpm)) State.bpm = state.bpm;
      if (Number.isFinite(state.transpose)) State.transpose = state.transpose;
      if (Number.isFinite(state.octave)) State.octave = state.octave;
      if (typeof state.syncroStop === "boolean") State.syncroStop = state.syncroStop;
      if (typeof state.activeBass === "boolean") State.activeBass = state.activeBass;
      if (typeof state.activeArranger === "boolean") State.activeArranger = state.activeArranger;
      if (typeof state.activeMemory === "boolean") State.activeMemory = state.activeMemory;
      if (state.styleName) State.parts.style = state.styleName;
      if (typeof state.synthEngine === "string") State.synthEngine = state.synthEngine;
      if (typeof state.soundFontName === "string") State.soundFontName = state.soundFontName;
      if (typeof state.soundFontPath === "string") State.soundFontPath = state.soundFontPath;
      renderAll();
      renderSyncroStop();
    },
    postToJuce: (type, payload) => {
      if (window.__juce__ && typeof window.__juce__.postMessage === "function") {
        window.__juce__.postMessage(JSON.stringify({ type, payload }));
      } else {
        console.log("[JUCE Bridge]", type, payload);
      }
    },
  };
  const toJuce = (t, p) => window.JuceBridge.postToJuce(t, p || {});

  /* ------------------------------------------------------------
     commit(key, value, [juceType], [jucePayload])
     ------------------------------------------------------------
     Payload-shape rule:
       - SCALAR state (bpm, transpose, key, ...)     -> value is the primitive
       - KEYED  state (channels.X, pads[i], ...)     -> value is { id, value }
     Re-render policy: commit() invokes renderFor(key); the per-section
     renderer is responsible for touching only its own DOM.
     ------------------------------------------------------------ */
  function commit(key, value, juceType, jucePayload) {
    emit(key, value);
    if (juceType) toJuce(juceType, jucePayload != null ? jucePayload : value);
    renderFor(key);
  }

  /* ============================================================
     DOM REFS
     ============================================================ */
  const refs = {};

  /* ---------- main tabs ---------- */
  $$("#tabnav .tab").forEach(t => t.addEventListener("click", () => {
    $$("#tabnav .tab").forEach(x => x.classList.remove("active"));
    $$(".tabpanel").forEach(x => x.classList.remove("active"));
    t.classList.add("active");
    $("#tab-" + t.dataset.tab).classList.add("active");
  }));

  const fileMenuButton = $('[data-menu="file"]');
  const fileMenu = $("#fileMenu");
  if (fileMenuButton && fileMenu) {
    const closeFileMenu = () => {
      fileMenu.classList.remove("open");
      fileMenu.setAttribute("aria-hidden", "true");
    };
    fileMenuButton.addEventListener("click", e => {
      e.stopPropagation();
      const open = !fileMenu.classList.contains("open");
      fileMenu.classList.toggle("open", open);
      fileMenu.setAttribute("aria-hidden", open ? "false" : "true");
    });
    fileMenu.addEventListener("click", e => {
      const action = e.target.closest("[data-menu-action]")?.dataset.menuAction;
      if (!action) return;
      closeFileMenu();
      if (action === "openStyle") toJuce("openStyleFile", {});
      if (action === "openSoundFont") toJuce("openSoundFontFile", {});
      if (action === "openPlugin") toJuce("openPluginFile", {});
      if (action === "clearPlugin") toJuce("clearPlugin", {});
      if (action === "exportPlaybackDiagnostics") toJuce("exportPlaybackDiagnostics", {});
      if (action === "exit") toJuce("exitApp", {});
    });
    document.addEventListener("click", closeFileMenu);
  }

  /* ============================================================
     STYLES LIST
     ============================================================ */
  const styles = [
    ["Factory",       "Built-in styles · 04 Jan 2024"],
    ["Imported",      "User library · 16 Mar 2025"],
    ["Ballads",       "112 styles · 02 Feb 2024"],
    ["Latin & World", "88 styles · 02 Feb 2024"],
    ["Pop / Rock",    "146 styles · 02 Feb 2024"],
    ["Jazz & Swing",  "74 styles · 02 Feb 2024"],
    ["Dance / EDM",   "96 styles · 11 Aug 2024"],
    ["Orchestral",    "54 styles · 02 Feb 2024"],
  ];
  const styleList = $("#styleList");
  styles.forEach(([name, sub], i) => {
    const card = el("div", "style-card");
    card.dataset.idx = i + 1;
    card.innerHTML =
      `<div class="num">${i + 1}</div><div class="folder-ico"></div>` +
      `<div class="meta"><span class="name">${name}</span><span class="sub">${sub}</span></div>`;
    card.addEventListener("click", () => {
      State.selectedStyle = i + 1;
      commit("selectedStyle", i + 1, "selectStyle", { index: i + 1, name });
    });
    styleList.appendChild(card);
  });
  $$("#pager .pg-link").forEach(b => b.addEventListener("click", () => {
    if (b.dataset.pg === "p1") {
      $$("#pager .pg-link").forEach(x => x.classList.toggle("active", x === b));
    }
  }));
  $("#openStyleFileBtn")?.addEventListener("click", () => {
    toJuce("openStyleFile", {});
  });

  function renderStyleListItems() {
    if (!styleList) return;
    styleList.innerHTML = "";
    if (!State.factoryStyles.length) {
      const empty = el("div", "style-card disabled");
      empty.innerHTML =
        `<div class="num">--</div><div class="folder-ico"></div>` +
        `<div class="meta"><span class="name">No factory styles found</span><span class="sub">Use the folder button to open .sty or .cstyle</span></div>`;
      styleList.appendChild(empty);
      return;
    }

    State.factoryStyles.forEach((style, i) => {
      const card = el("div", "style-card");
      card.dataset.idx = i + 1;
      card.dataset.path = style.path || "";
      card.dataset.id = style.id || style.name || "";
      card.innerHTML =
        `<div class="num">${i + 1}</div><div class="folder-ico"></div>` +
        `<div class="meta"><span class="name"></span><span class="sub"></span></div>`;
      card.querySelector(".name").textContent = style.name || style.id || "Style";
      card.querySelector(".sub").textContent = `${(style.kind || "style").toUpperCase()} - Factory`;
      card.addEventListener("click", () => {
        State.selectedStyle = i + 1;
        State.parts.style = style.name || style.id || "Style";
        commit("selectedStyle", i + 1, "selectStyle", {
          index: i + 1,
          name: style.path || style.id || style.name,
          path: style.path || ""
        });
        renderParts();
      });
      styleList.appendChild(card);
    });
  }

  /* ============================================================
     PART SLOTS  (left col: style/part/band/pad · right: song/audio/lyrics/pattern)
     ============================================================ */
  const partDefs = [
    ["style",   "STYLE",   "#3a78c4", "♫"],
    ["song",    "SONG",    "#d4609a", "✱"],
    ["part",    "PART",    "#3fb5b0", "↻"],
    ["audio",   "AUDIO",   "#e0763a", "◉"],
    ["band",    "BAND",    "#6f86a8", "▤"],
    ["lyrics",  "LYRICS",  "#5a8fd0", "¶"],
    ["pad",     "PAD",     "#9a6cd4", "▦"],
    ["pattern", "PATTERN", "#3ab840", "↺"],
  ];
  const partsGrid = $("#partsGrid");
  partDefs.forEach(([key, label, color, glyph]) => {
    const slot = el("div", "part-slot");
    slot.dataset.part = key;
    slot.innerHTML =
      `<div class="part-ico" style="background:${color}">${glyph}</div>` +
      `<div class="pmeta"><span class="ptype">${label}</span>` +
      `<span class="pname" data-pname></span></div>`;
    slot.addEventListener("click", () => {
      State.selectedPart = (State.selectedPart === key) ? null : key;
      commit("selectedPart", State.selectedPart, "selectPart", { part: key });
      // The SONG slot drives song mode: tap to choose a chord chart (.csong),
      // tap again while active to turn song mode off.
      if (key === "song") {
        if (State.songMode) toJuce("songMode", { value: false });
        else toJuce("openSongFile");
      }
    });
    partsGrid.appendChild(slot);
  });

  /* ============================================================
     KNOB — draggable rotary
     ============================================================ */
  function makeKnob(node, { min, max, get, set, juceType, juceKey, juceExtra }) {
    function toDeg(v) {
      return KNOB_DEG_MIN + ((v - min) / (max - min)) * (KNOB_DEG_MAX - KNOB_DEG_MIN);
    }
    node._render = () => { node.style.setProperty("--deg", toDeg(get()).toFixed(1) + "deg"); };
    node._render();
    node.addEventListener("pointerdown", e => {
      e.preventDefault();
      const startY = e.clientY, startV = get();
      const range = max - min;
      node.setPointerCapture(e.pointerId);
      const move = ev => {
        const dv = (startY - ev.clientY) / KNOB_TRAVEL_PX * range;
        const nv = clamp(Math.round(startV + dv), min, max);
        set(nv);
        node._render();
        node.title = nv;
        emit(juceKey, nv);
        if (juceType) toJuce(juceType, { ...(juceExtra || {}), value: nv });
      };
      const up = () => {
        node.releasePointerCapture(e.pointerId);
        node.removeEventListener("pointermove", move);
        node.removeEventListener("pointerup", up);
      };
      node.addEventListener("pointermove", move);
      node.addEventListener("pointerup", up);
    });
    return node;
  }

  /* tempo knob */
  refs.tempoKnob = makeKnob($("#tempoKnob"), {
    min: BPM_MIN, max: BPM_MAX,
    get: () => State.bpm, set: v => { State.bpm = v; renderTransport(); },
    juceType: "bpm", juceKey: "bpm",
  });

  /* ============================================================
     FADER — draggable vertical
     ============================================================ */
  function dbToPct(db) { return clamp((db - DB_MIN) / DB_RANGE, 0, 1) * 100; }
  function pctToDb(p)  { return clamp(p / 100 * DB_RANGE + DB_MIN, DB_MIN, DB_MAX); }

  function makeFader(track, handle, { getPct, setPct, juceType, juceKey, juceExtra }) {
    function place() { handle.style.bottom = clamp(getPct(), 0, 100) + "%"; }
    place();
    track._place = place;
    const startDrag = e => {
      e.preventDefault();
      const rect = track.getBoundingClientRect();
      const apply = ev => {
        const pct = clamp((rect.bottom - ev.clientY) / rect.height * 100, 0, 100);
        setPct(pct); place();
        emit(juceKey, pct);
        if (juceType) toJuce(juceType, { ...(juceExtra || {}), value: Math.round(pct) });
      };
      apply(e);
      const up = () => {
        window.removeEventListener("pointermove", apply);
        window.removeEventListener("pointerup", up);
      };
      window.addEventListener("pointermove", apply);
      window.addEventListener("pointerup", up);
    };
    track.addEventListener("pointerdown", startDrag);
  }

  /* crossfade fader in player */
  (function () {
    const f = $('.vfader[data-fader="crossfade"]');
    if (f) makeFader(f, f.querySelector(".vhandle"), {
      getPct: () => State.crossfade,
      setPct: p => { State.crossfade = Math.round(p); },
      juceType: "crossfade", juceKey: "crossfade",
    });
  })();

  /* ============================================================
     MELODY MIXER (6 channels)
     ============================================================ */
  const dbMarks = [["12", 6], ["6", 16], ["0", 28], ["-6", 40], ["-18", 58], ["-36", 80], ["-inf", 97]];
  const chOrder = ["left", "right1", "right2", "right3", "melody", "master"];
  const chLabels = {
    left: "Left", right1: "Right 1", right2: "Right 2", right3: "Right 3",
    melody: "Melody", master: "Master"
  };
  let activeMixTab = "right1";

  const mm = $("#melodyMixer");
  chOrder.forEach(key => {
    const c = State.channels[key];
    const master = key === "master";
    const ch = el("div", "mx-ch");
    ch.dataset.ch = key;
    const scale = dbMarks.map(([l, t]) => `<span style="top:${t}%">${l}</span>`).join("");
    ch.innerHTML =
      `<div class="nm" data-tab>${chLabels[key]}</div>` +
      `<button class="pill si" aria-label="Select instrument for ${chLabels[key]}">${master ? "Master Bus" : "Select Instrument"}</button>` +
      `<div class="sm-row">` +
        `<button class="pill green sbtn" aria-pressed="false" aria-label="Solo ${chLabels[key]}">S</button>` +
        `<button class="pill amber mbtn" aria-pressed="false" aria-label="Mute ${chLabels[key]}">M</button>` +
      `</div>` +
      `<button class="pill arrow" aria-label="More options">▾</button>` +
      `<div class="pan"><div class="knob" data-pan tabindex="0" role="slider" aria-label="Pan ${chLabels[key]}"></div><span class="lbl">Pan</span></div>` +
      `<div class="meters" data-meters>` +
        `<div class="dbscale">${scale}</div>` +
        (master ? `<div class="clip-led" data-clip></div>` : "") +
        `<div class="vubar"><div class="lvl"></div></div>` +
        `<div class="vubar"><div class="lvl"></div></div>` +
        `<div class="fader-handle${master ? " master" : ""}" data-faderh></div>` +
      `</div>`;
    mm.appendChild(ch);

    // tab select
    ch.querySelector("[data-tab]").addEventListener("click", () => {
      activeMixTab = key;
      renderMixer();
      toJuce("selectChannel", { channel: key });
    });
    // solo / mute
    ch.querySelector(".sbtn").addEventListener("click", () => {
      c.solo = !c.solo;
      commit("solo", { id: key, value: c.solo }, "solo", { channel: key, value: c.solo });
    });
    ch.querySelector(".mbtn").addEventListener("click", () => {
      c.mute = !c.mute;
      commit("mute", { id: key, value: c.mute }, "mute", { channel: key, value: c.mute });
    });
    // pan knob — channel-scoped juceKey so listeners can tell channels apart
    makeKnob(ch.querySelector("[data-pan]"), {
      min: PAN_MIN, max: PAN_MAX,
      get: () => c.pan, set: v => { c.pan = v; },
      juceType: "pan", juceKey: "pan." + key,
      juceExtra: { channel: key },
    });
    // fader (drag over meters area)
    const meters = ch.querySelector("[data-meters]");
    const handle = ch.querySelector("[data-faderh]");
    makeFader(meters, handle, {
      getPct: () => dbToPct(c.volume),
      setPct: p => { c.volume = Math.round(pctToDb(p)); },
      juceType: "volume", juceKey: "volume." + key,
      juceExtra: { channel: key },
    });
  });

  /* mixer sub-tabs — visual-only swap for now.
     TODO: when wired to JUCE, switch the mixer view contents to reflect
     the chosen scope (Melody/Physical/Instruments/Lyrics). */
  $$("#mxSubtabs .mx-subtab").forEach(s => s.addEventListener("click", () => {
    $$("#mxSubtabs .mx-subtab").forEach(x => x.classList.remove("active"));
    s.classList.add("active");
  }));

  /* ============================================================
     MELODY ON/OFF + CHORDS + BANK MEMORIES
     ============================================================ */
  $$('#g-melodyon .pill').forEach(b => b.addEventListener("click", () => {
    const k = b.dataset.mel;
    State.activeChannels[k] = !State.activeChannels[k];
    commit("activeChannels", { id: k, value: State.activeChannels[k] },
           "melodyOnOff", { channel: k, value: State.activeChannels[k] });
  }));
  const chordSourceMap = { bass: "activeBass", arranger: "activeArranger", memory: "activeMemory" };
  $$('#g-chords .pill').forEach(b => b.addEventListener("click", () => {
    const k = b.dataset.chord;
    const stateKey = chordSourceMap[k];
    State[stateKey] = !State[stateKey];
    commit(stateKey, State[stateKey], "chordSource", { source: k, value: State[stateKey] });
  }));

  const banks = [
    ["Piano", "El Grand", "Rhodes", "FM Piano", "Digi Piano", "Rock Piano", "N. Guitar", "C. Guitar"],
    ["Dist Solo", "80's Lead", "Organ", "Alto Sax", "Tenor Sax", "Trumpet", "Power Pad", "Synth Stab"],
  ];
  const bankGrid = $("#bankGrid");
  banks.forEach(row => row.forEach(name => {
    const cell = el("div", "bank-cell");
    cell.dataset.bank = name;
    cell.innerHTML = `<button class="pill" aria-pressed="false">${name}</button><div class="led"></div>`;
    bankGrid.appendChild(cell);
    const bankButton = cell.querySelector(".pill");
    bankButton.title = "Set the live right-hand melody instrument";
    // Selecting a voice sets the live melody instrument (GM program on the
    // dedicated melody channel). Octave/style playback are unaffected.
    bankButton.addEventListener("click", () => {
      State.bankMemory = name;
      commit("bankMemory", name, "bankMemory", { name });
      renderBank();
    });
  }));

  /* ============================================================
     CONTROL STRIP: transpose / octave / tempo / player / pads / style mem
     ============================================================ */
  $$('[data-trans]').forEach(b => b.addEventListener("click", () => {
    State.transpose = clamp(State.transpose + (+b.dataset.trans), TRANSPOSE_MIN, TRANSPOSE_MAX);
    commit("transpose", State.transpose, "transpose", { value: State.transpose });
  }));
  $$('[data-oct]').forEach(b => b.addEventListener("click", () => {
    State.octave = clamp(State.octave + (+b.dataset.oct), OCTAVE_MIN, OCTAVE_MAX);
    console.log("[Cadenza] octave button", b.dataset.oct, "-> octave =", State.octave);
    commit("octave", State.octave, "octave", { value: State.octave });
  }));
  $$('[data-tempo]').forEach(b => b.addEventListener("click", () => {
    State.bpm = clamp(State.bpm + (+b.dataset.tempo), BPM_MIN, BPM_MAX);
    commit("bpm", State.bpm, "bpm", { value: State.bpm });
  }));
  $('#tapReset') && $('#tapReset').addEventListener("click", () => {
    State.bpm = BPM_DEFAULT;
    commit("bpm", BPM_DEFAULT, "bpm", { value: BPM_DEFAULT });
  });
  $$('[data-pad]').forEach(b => b.addEventListener("click", () => {
    const i = +b.dataset.pad;
    State.pads[i] = !State.pads[i];
    commit("pads", { id: i, value: State.pads[i] }, "pad", { index: i, value: State.pads[i] });
  }));
  $$('[data-stylem]').forEach(b => b.addEventListener("click", () => {
    State.styleMemory = +b.dataset.stylem;
    commit("styleMemory", State.styleMemory, "styleMemory", { slot: State.styleMemory });
  }));
  // visual toggles (lock, syncro start, etc.) — purely cosmetic, no state mirror
  $$('[data-toggle]').forEach(b => b.addEventListener("click", () => {
    const next = !b.classList.contains("is-on");
    b.classList.toggle("is-on", next);
    b.setAttribute("aria-pressed", next ? "true" : "false");
  }));

  /* ============================================================
     TRANSPORT DISPLAY interactions (BPM edit/scroll, transpose/octave scroll)
     ============================================================ */
  const bpmVal = $("#bpmVal");
  bpmVal.addEventListener("wheel", e => {
    e.preventDefault();
    State.bpm = clamp(State.bpm + (e.deltaY < 0 ? 1 : -1), BPM_MIN, BPM_MAX);
    commit("bpm", State.bpm, "bpm", { value: State.bpm });
  }, { passive: false });
  bpmVal.addEventListener("click", () => {
    bpmVal.contentEditable = "true"; bpmVal.focus();
    document.getSelection().selectAllChildren(bpmVal);
  });
  bpmVal.addEventListener("blur", () => {
    bpmVal.contentEditable = "false";
    const v = clamp(parseInt(bpmVal.textContent, 10) || BPM_DEFAULT, BPM_MIN, BPM_MAX);
    State.bpm = v;
    commit("bpm", v, "bpm", { value: v });
  });
  bpmVal.addEventListener("keydown", e => { if (e.key === "Enter") { e.preventDefault(); bpmVal.blur(); } });

  $("#transposeDisp").addEventListener("wheel", e => {
    e.preventDefault();
    State.transpose = clamp(State.transpose + (e.deltaY < 0 ? 1 : -1), TRANSPOSE_MIN, TRANSPOSE_MAX);
    commit("transpose", State.transpose, "transpose", { value: State.transpose });
  }, { passive: false });
  $("#octaveDisp").addEventListener("wheel", e => {
    e.preventDefault();
    State.octave = clamp(State.octave + (e.deltaY < 0 ? 1 : -1), OCTAVE_MIN, OCTAVE_MAX);
    commit("octave", State.octave, "octave", { value: State.octave });
  }, { passive: false });

  /* key dropdown */
  $("#keySel").addEventListener("change", e => {
    State.key = e.target.value;
    commit("key", State.key, "key", { value: State.key });
  });

  /* ============================================================
     PLAY / STOP / RECORD — rAF + visibility-aware meter animation
     TODO: when wired to JUCE, replace random VU/CPU values with the
     real feed from JuceBridge.onMeterUpdate(channelLevels) etc.
     ============================================================ */
  let meterRaf = 0;
  let lastVuTick = 0;
  let cpuTimer = null;

  function vuLoop(ts) {
    if (!State.playing) return;
    if (document.visibilityState !== "visible") {
      meterRaf = requestAnimationFrame(vuLoop);
      return;
    }
    if (ts - lastVuTick >= VU_FPS_MS) {
      lastVuTick = ts;
      $$(".vubar .lvl").forEach(l => { l.style.height = (22 + Math.random() * 60).toFixed(0) + "%"; });
      const clip = $("[data-clip]");
      if (clip) clip.classList.toggle("on", Math.random() > 0.85);
    }
    meterRaf = requestAnimationFrame(vuLoop);
  }
  function startMeters() {
    cancelAnimationFrame(meterRaf);
    lastVuTick = 0;
    meterRaf = requestAnimationFrame(vuLoop);
  }
  function stopMeters() {
    cancelAnimationFrame(meterRaf);
    $$(".vubar .lvl").forEach(l => l.style.height = "0%");
    const clip = $("[data-clip]"); if (clip) clip.classList.remove("on");
  }

  function setPlaying(p, fromHost) {
    State.playing = p;
    if (p) startMeters(); else stopMeters();
    clearInterval(cpuTimer); cpuTimer = null;
    if (p) {
      cpuTimer = setInterval(() => {
        $("#cpuVal").textContent = (6 + Math.random() * 9).toFixed(2) + "%";
      }, CPU_TICK_MS);
    } else {
      $("#cpuVal").textContent = "0.00%";
    }
    renderTransport();
    emit("playing", p);
    if (!fromHost) toJuce(p ? "play" : "stop", {});
  }
  function renderSyncroStop() {
    const btn = $("#sStopBtn");
    if (!btn) return;
    btn.classList.toggle("is-on", State.syncroStop);
    btn.setAttribute("aria-pressed", State.syncroStop ? "true" : "false");
  }
  $("#playBtn").addEventListener("click", () => setPlaying(!State.playing));
  $("#topPlay").addEventListener("click", () => setPlaying(true));
  $("#topStop").addEventListener("click", () => setPlaying(false));
  // S.Stop = toggle syncroStopOnRelease (stop backing when last chord note released).
  (function() {
    const btn = $("#sStopBtn");
    if (!btn) return;
    renderSyncroStop();
    btn.addEventListener("click", () => {
      State.syncroStop = !State.syncroStop;
      renderSyncroStop();
      toJuce("syncroStop", { value: State.syncroStop });
    });
  })();
  $("#recBtn").addEventListener("click", () => {
    State.recording = !State.recording;
    commit("recording", State.recording, "record", { value: State.recording });
  });

  /* ============================================================
     PIANO  (colorful per-octave tints + note events)
     keyByNote map is scoped per piano instance so multiple pianos
     can light up independently.
     ============================================================ */
  const noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  const octaveColors = {
    "-2": [120, 40, 180],  // deep violet
    "-1": [60,  80, 200],  // indigo
     "0": [20, 120, 200],  // cobalt blue
     "1": [20, 160, 120],  // teal
     "2": [40, 170,  50],  // emerald
     "3": [160, 160, 20],  // lime-gold
     "4": [200, 110,  20], // amber (chord/melody split)
     "5": [200,  50,  30], // coral
     "6": [180,  20,  80], // rose
     "7": [140,  20, 160], // violet
     "8": [80,   20, 180], // purple
  };

  // Multi-piano-aware map: pianoId -> Map<midi, element>
  const pianoMaps = new Map();

  function flashNote(midi, pianoId) {
    const targets = pianoId
      ? [pianoMaps.get(pianoId)]
      : [...pianoMaps.values()];
    for (const map of targets) {
      if (!map) continue;
      const k = map.get(midi);
      if (!k) continue;
      const cls = k.classList.contains("pkey-black")
        ? "lit"
        : (k.dataset.amber === "1" ? "lit-amber" : "lit");
      k.classList.add(cls);
      setTimeout(() => k.classList.remove("lit", "lit-amber"), 180);
    }
  }

  function buildPiano(containerId, labelId, lowMidi, highMidi) {
    const piano = $("#" + containerId);
    const labels = labelId ? $("#" + labelId) : null;
    if (!piano) return;
    piano.innerHTML = ""; if (labels) labels.innerHTML = "";

    const map = new Map();
    pianoMaps.set(containerId, map);

    const blackPC = { 1: 1, 3: 1, 6: 1, 8: 1, 10: 1 };
    let whiteCount = 0;
    for (let n = lowMidi; n <= highMidi; n++) if (!blackPC[n % 12]) whiteCount++;
    const whiteW = 100 / whiteCount;
    const splitMidi = lowMidi + Math.floor((highMidi - lowMidi) / 2);
    const blacks = [];
    let wi = 0;
    for (let n = lowMidi; n <= highMidi; n++) {
      const pc = n % 12, oct = Math.floor(n / 12) - 2;
      if (blackPC[pc]) { blacks.push({ x: wi * whiteW, midi: n }); continue; }
      const k = el("div", "pkey-white");
      k.dataset.midi = n;
      const rgb = octaveColors[oct] || [60, 90, 130];
      const amber = n < splitMidi;
      if (amber) k.dataset.amber = "1";
      k.style.setProperty("--tint", `rgba(${rgb[0]},${rgb[1]},${rgb[2]},.38)`);
      k.style.setProperty("--tint-strong", amber ? "rgba(224,120,32,.85)" : `rgba(${rgb[0]},${rgb[1]},${rgb[2]},.88)`);
      piano.appendChild(k);
      map.set(n, k);
      if (pc === 0 && labels) {
        const lab = el("span", null, "C" + oct);
        lab.style.left = (wi * whiteW) + "%";
        labels.appendChild(lab);
      }
      wi++;
    }
    const blackW = whiteW * 0.62;
    blacks.forEach(b => {
      const k = el("div", "pkey-black");
      k.dataset.midi = b.midi;
      k.style.left = (b.x - blackW / 2) + "%";
      k.style.width = blackW + "%";
      piano.appendChild(k);
      map.set(b.midi, k);
    });

    piano.addEventListener("pointerdown", e => {
      const key = e.target.closest(".pkey-white,.pkey-black");
      if (!key) return;
      const midi = +key.dataset.midi;
      const isBlack = key.classList.contains("pkey-black");
      const cls = (!isBlack && key.dataset.amber === "1") ? "lit-amber" : "lit";
      key.classList.add(cls);
      const oct = Math.floor(midi / 12) - 2;
      const detail = { note: midi, name: noteNames[midi % 12], octave: oct, velocity: 100, piano: containerId };
      document.dispatchEvent(new CustomEvent("cadenza:noteOn", { detail }));
      toJuce("noteOn", { note: midi, velocity: 100, piano: containerId });
      const clear = () => {
        key.classList.remove("lit", "lit-amber");
        document.dispatchEvent(new CustomEvent("cadenza:noteOff", { detail }));
        toJuce("noteOff", { note: midi, piano: containerId });
      };
      key.addEventListener("pointerup", clear, { once: true });
      key.addEventListener("pointerleave", clear, { once: true });
    });

    return map;
  }
  buildPiano("piano", "octaveLabels", PIANO_LOW_MIDI, PIANO_HIGH_MIDI);
  buildPiano("piano2", "octaveLabels2", PIANO_LOW_MIDI, PIANO_HIGH_MIDI);

  /* window controls */
  $$(".wincontrols button").forEach(b => b.addEventListener("click", e => {
    e.preventDefault();
    if (b.classList.contains("close")) toJuce("exitApp", {});
  }));

  /* ============================================================
     RENDER — per-section renderers
     Each renderer touches only its own DOM. renderFor(key) is the
     dispatcher: it maps a state key to the section(s) that depend on it.
     renderAll() is for initial paint and external (bridge) sync.
     ============================================================ */
  function renderTransport() {
    bpmVal.textContent = State.bpm;
    $("#transposeDisp .v").textContent = (State.transpose > 0 ? "+" : "") + State.transpose;
    $("#octaveDisp .v").textContent = (State.octave > 0 ? "+" : "") + State.octave;
    $("#chordVal").textContent = "(" + State.chord + ")";
    const sl = $("#statusLine");
    sl.textContent = State.playing ? "PLAYING" : "STOPPED";
    sl.classList.toggle("playing", State.playing);
    $("#playBtn").classList.toggle("is-on", State.playing);
    $("#topPlay").classList.toggle("is-on", State.playing);
    $("#playBtn").setAttribute("aria-pressed", State.playing ? "true" : "false");
    $("#recBtn").classList.toggle("is-on", State.recording);
    $("#recBtn").setAttribute("aria-pressed", State.recording ? "true" : "false");
    if ($("#keySel").value !== State.key) $("#keySel").value = State.key;
    if (refs.tempoKnob && refs.tempoKnob._render) refs.tempoKnob._render();
  }

  function renderStyles() {
    renderStyleListItems();
    $$("#styleList .style-card").forEach(c =>
      c.classList.toggle("active", +c.dataset.idx === State.selectedStyle));
  }

  function renderParts() {
    $$("#partsGrid .part-slot").forEach(s => {
      const k = s.dataset.part, name = State.parts[k];
      const pn = s.querySelector("[data-pname]");
      pn.textContent = name || "—";
      pn.classList.toggle("empty", !name);
      s.classList.toggle("sel", State.selectedPart === k);
    });
  }

  function renderMixer() {
    $$("#melodyMixer .mx-ch").forEach(ch => {
      const k = ch.dataset.ch, c = State.channels[k];
      ch.classList.toggle("sel", activeMixTab === k);
      const sBtn = ch.querySelector(".sbtn");
      const mBtn = ch.querySelector(".mbtn");
      sBtn.classList.toggle("is-on", c.solo);
      sBtn.setAttribute("aria-pressed", c.solo ? "true" : "false");
      mBtn.classList.toggle("is-on", c.mute);
      mBtn.setAttribute("aria-pressed", c.mute ? "true" : "false");
      const panK = ch.querySelector("[data-pan]"); if (panK._render) panK._render();
      const meters = ch.querySelector("[data-meters]"); if (meters._place) meters._place();
    });
  }

  function renderMelodyOnOff() {
    $$('#g-melodyon .pill').forEach(b => {
      const on = State.activeChannels[b.dataset.mel];
      b.classList.toggle("is-on", on);
      b.classList.toggle("green", on);
      b.setAttribute("aria-pressed", on ? "true" : "false");
    });
  }

  function renderChordSources() {
    $$('#g-chords .pill').forEach(b => {
      const on = State[chordSourceMap[b.dataset.chord]];
      b.classList.toggle("is-on", on);
      b.classList.toggle("green", on);
      b.setAttribute("aria-pressed", on ? "true" : "false");
    });
  }

  function renderBank() {
    $$("#bankGrid .bank-cell").forEach(cell => {
      const disabled = cell.querySelector(".pill")?.disabled;
      const on = !disabled && cell.dataset.bank === State.bankMemory;
      cell.querySelector(".led").classList.toggle("on", on);
      const pill = cell.querySelector(".pill");
      pill.classList.toggle("is-on", on);
      pill.setAttribute("aria-pressed", on ? "true" : "false");
    });
  }

  function renderPads() {
    $$('[data-pad]').forEach(b => {
      const on = State.pads[+b.dataset.pad];
      b.classList.toggle("is-on", on);
      b.setAttribute("aria-pressed", on ? "true" : "false");
    });
  }

  function renderStyleMem() {
    $$('[data-stylem]').forEach(b => {
      const on = +b.dataset.stylem === State.styleMemory;
      b.classList.toggle("is-on", on);
      b.classList.toggle("amber", on);
      b.setAttribute("aria-pressed", on ? "true" : "false");
    });
  }

  function renderEngineInfo() {
    const engine = $("#audioEngineName");
    const sf = $("#soundFontName");
    if (engine) engine.textContent = State.synthEngine || "Unknown";
    if (sf) {
      sf.textContent = State.soundFontName || "None";
      sf.title = State.soundFontPath || "";
    }
  }

  // dispatch: state key -> set of renderers
  const RENDER_MAP = {
    bpm:           [renderTransport],
    transpose:     [renderTransport],
    octave:        [renderTransport],
    chord:         [renderTransport],
    key:           [renderTransport],
    playing:       [renderTransport],
    recording:     [renderTransport],
    selectedStyle: [renderStyles],
    selectedPart:  [renderParts],
    solo:          [renderMixer],
    mute:          [renderMixer],
    activeChannels:[renderMelodyOnOff],
    activeBass:    [renderChordSources],
    activeArranger:[renderChordSources],
    activeMemory:  [renderChordSources],
    bankMemory:    [renderBank],
    pads:          [renderPads],
    styleMemory:   [renderStyleMem],
    synthEngine:   [renderEngineInfo],
    soundFontName: [renderEngineInfo],
  };
  function renderFor(key) {
    const fns = RENDER_MAP[key];
    if (!fns) { renderAll(); return; }
    for (const fn of fns) fn();
  }
  function renderAll() {
    renderTransport();
    renderStyles();
    renderParts();
    renderMixer();
    renderMelodyOnOff();
    renderChordSources();
    renderBank();
    renderPads();
    renderStyleMem();
    renderSyncroStop();
    renderEngineInfo();
  }

  /* ============================================================
     PUBLIC API
     ============================================================ */
  window.CadenzaAPI = {
    getState: () => State,
    setBPM: (val)       => { State.bpm = clamp(val, BPM_MIN, BPM_MAX); renderTransport(); },
    setTranspose: (val) => { State.transpose = clamp(val, TRANSPOSE_MIN, TRANSPOSE_MAX); renderTransport(); },
    setOctave: (val)    => { State.octave = clamp(val, OCTAVE_MIN, OCTAVE_MAX); renderTransport(); },
    setChord: (val)     => { State.chord = val; renderTransport(); },
    setKey: (val)       => { State.key = val; renderTransport(); },
    play: () => setPlaying(true),
    stop: () => setPlaying(false),
    setVolume: (channel, val) => {
      if (State.channels[channel]) { State.channels[channel].volume = val; renderMixer(); }
    },
    setPan: (channel, val) => {
      if (State.channels[channel]) {
        State.channels[channel].pan = clamp(val, PAN_MIN, PAN_MAX); renderMixer();
      }
    },
    setBankMemory: (name) => { State.bankMemory = name; renderBank(); },
    selectStyle: (idx)    => { State.selectedStyle = idx; renderStyles(); },
    flashNote: (midi, pianoId) => flashNote(midi, pianoId),
    onNoteOn:  (cb) => document.addEventListener("cadenza:noteOn", cb),
    onNoteOff: (cb) => document.addEventListener("cadenza:noteOff", cb),
    onStateChange: (cb) => document.addEventListener("cadenza:stateChange", cb),
  };

  /* initial paint */
  renderAll();

  // Tell the C++ host the page is ready. The host will push current chord/state
  // back to the UI. Deferred to 'load' so the JUCE native integration is wired up.
  window.addEventListener("load", () => toJuce("pageReady", {}));
})();
