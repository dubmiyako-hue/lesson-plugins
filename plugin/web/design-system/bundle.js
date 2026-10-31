/* @ds-bundle: {"format":4,"namespace":"PRAYROOM","components":[{"name":"Knob"},{"name":"Dial"},{"name":"Drum"},{"name":"Tape"},{"name":"Encoder"},{"name":"Fader"},{"name":"Toggle"},{"name":"Key"},{"name":"Badge"},{"name":"Tag"},{"name":"Bracket"},{"name":"Device"},{"name":"Scrim"},{"name":"Lamp"},{"name":"Switch"},{"name":"Hint"},{"name":"Segmented"},{"name":"VSlide"},{"name":"Stepper"},{"name":"OverviewTabs"},{"name":"Meter"},{"name":"Graph"},{"name":"Envelope"},{"name":"PitchPath"},{"name":"Panel"},{"name":"PluginWindow"},{"name":"Viewer"},{"name":"Timeline"}]} */
(function () {
  "use strict";

  // ---------- helpers ----------
  function el(tag, cls, parent) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (parent) parent.appendChild(e);
    return e;
  }
  function svg(tag, attrs, parent) {
    var e = document.createElementNS("http://www.w3.org/2000/svg", tag);
    for (var k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }
  function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }
  function cssVar(node, name) { return getComputedStyle(node).getPropertyValue(name).trim(); }

  // value <-> normalized 0..1 (linear or log)
  function makeScale(o) {
    var min = o.min, max = o.max, log = o.scale === "log" && min > 0;
    return {
      toNorm: function (v) {
        return log ? Math.log(v / min) / Math.log(max / min) : (v - min) / (max - min);
      },
      fromNorm: function (n) {
        n = clamp(n, 0, 1);
        return log ? min * Math.pow(max / min, n) : min + n * (max - min);
      }
    };
  }

  function defaultFormat(o) {
    var dec = o.decimals == null ? 1 : o.decimals;
    return function (v) {
      if (o.unit === "Hz" && Math.abs(v) >= 1000) return { text: (v / 1000).toFixed(v >= 10000 ? 1 : 2), unit: "kHz" };
      if (o.unit === "%") return { text: v.toFixed(0), unit: "%" };
      var t = v.toFixed(dec);
      if (o.signed && v > 0) t = "+" + t;
      return { text: t, unit: o.unit || "" };
    };
  }

  function writeValue(node, f) {
    node.textContent = "";
    node.appendChild(document.createTextNode(f.text));
    if (f.unit) { var u = el("span", "pr-unit", node); u.textContent = f.unit; }
  }

  // Shared continuous-parameter behaviour (Knob, Fader).
  // Vertical drag, Shift/Ctrl = fine, double-click or Alt+click = default, wheel, arrow keys.
  function continuous(o, target, pixelsForFullRange, render) {
    var sc = makeScale(o);
    var def = o.default != null ? o.default : o.value;
    var state = { value: clamp(o.value != null ? o.value : def, Math.min(o.min, o.max), Math.max(o.min, o.max)) };
    var fmt = o.format || defaultFormat(o);

    function set(v, silent) {
      var n = clamp(sc.toNorm(v), 0, 1);
      v = sc.fromNorm(n);
      if (o.step) v = Math.round(v / o.step) * o.step;
      state.value = v;
      target.setAttribute("aria-valuenow", String(v));
      var f = fmt(v);
      target.setAttribute("aria-valuetext", f.text + (f.unit ? " " + f.unit : ""));
      render(n, f);
      if (!silent && o.onChange) o.onChange(v);
    }

    var drag = null;
    target.addEventListener("pointerdown", function (e) {
      if (o.disabled) return;
      if (e.altKey) { set(def); return; }
      target.setPointerCapture(e.pointerId);
      drag = { y: e.clientY, x: e.clientX, n: sc.toNorm(state.value) };
      if (o.onGestureStart) o.onGestureStart();
      e.preventDefault();
    });
    target.addEventListener("pointermove", function (e) {
      if (!drag) return;
      var fine = e.shiftKey || e.ctrlKey ? 10 : 1;
      // vertical drag everywhere; a control that slides sideways (Tape) also takes dx, with its
      // own sign, and one that lies on its side (a horizontal Fader) takes dx alone
      var dn = ((o.dragY == null ? 1 : o.dragY) * (drag.y - e.clientY) + (o.dragX || 0) * (e.clientX - drag.x)) / (pixelsForFullRange() * fine);
      drag.y = e.clientY; drag.x = e.clientX;
      drag.n = clamp(drag.n + dn, 0, 1);
      set(sc.fromNorm(drag.n));
    });
    function end() { if (drag) { drag = null; if (o.onGestureEnd) o.onGestureEnd(); } }
    target.addEventListener("pointerup", end);
    target.addEventListener("pointercancel", end);
    target.addEventListener("dblclick", function () { if (!o.disabled) set(def); });
    target.addEventListener("wheel", function (e) {
      if (o.disabled) return;
      e.preventDefault();
      var step = (e.shiftKey ? 0.002 : 0.02) * (e.deltaY < 0 ? 1 : -1);
      set(sc.fromNorm(sc.toNorm(state.value) + step));
    }, { passive: false });
    target.addEventListener("keydown", function (e) {
      var n = sc.toNorm(state.value), s = e.shiftKey ? 0.001 : 0.01, hit = true;
      if (e.key === "ArrowUp" || e.key === "ArrowRight") n += s;
      else if (e.key === "ArrowDown" || e.key === "ArrowLeft") n -= s;
      else if (e.key === "PageUp") n += 0.1;
      else if (e.key === "PageDown") n -= 0.1;
      else if (e.key === "Home") n = 0;
      else if (e.key === "End") n = 1;
      else if (e.key === "Delete" || e.key === "Backspace") { set(def); e.preventDefault(); return; }
      else hit = false;
      if (hit) { e.preventDefault(); set(sc.fromNorm(n)); }
    });

    target.tabIndex = o.disabled ? -1 : 0;
    target.setAttribute("role", "slider");
    target.setAttribute("aria-valuemin", String(o.min));
    target.setAttribute("aria-valuemax", String(o.max));
    if (o.label) target.setAttribute("aria-label", o.label);

    set(state.value, true);
    return { state: state, set: set };
  }

  function controlColumn(host, o) {
    var col = el("div", "pr-ctl" + (o.disabled ? " pr-ctl--disabled" : ""), host);
    if (o.label) el("div", "pr-ctl__label", col).textContent = o.label;
    return col;
  }

  // ---------- Rotary controls: shared drawing ----------
  // PRAYROOM never paints highlights. Relief comes from a grey height map (#808080 = the panel plane)
  // lit by one light from the top-left (feDiffuseLighting), laid over flat colour with soft-light.
  // Print gets a little ink spread; everything gets paper grain.
  var START = -150, END = 150; // degrees, 0 = 12 o'clock
  function polar(cx, cy, r, deg) {
    var a = (deg - 90) * Math.PI / 180;
    return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
  }
  function arcPath(cx, cy, r, a0, a1) {
    if (Math.abs(a1 - a0) < 0.01) return "";
    var lo = Math.min(a0, a1), hi = Math.max(a0, a1);
    var p0 = polar(cx, cy, r, lo), p1 = polar(cx, cy, r, hi);
    return "M" + p0[0] + " " + p0[1] + " A" + r + " " + r + " 0 " + (hi - lo > 180 ? 1 : 0) + " 1 " + p1[0] + " " + p1[1];
  }
  var rotSeq = 0;
  function materialDefs(s, id, W, H, o) {
    var defs = svg("defs", {}, s), m = Math.max(W, H);
    var lit = svg("filter", { id: id + "lit", filterUnits: "userSpaceOnUse", x: -m, y: -m, width: 3 * m, height: 3 * m, "color-interpolation-filters": "sRGB" }, defs);
    svg("feColorMatrix", { "in": "SourceGraphic", type: "matrix", values: "0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  1 0 0 0 0", result: "h0" }, lit);
    svg("feGaussianBlur", { "in": "h0", stdDeviation: o.blur, result: "h" }, lit);
    var dl = svg("feDiffuseLighting", { "in": "h", surfaceScale: o.relief, diffuseConstant: 0.87, "lighting-color": "#ffffff" }, lit);
    svg("feDistantLight", { azimuth: 225, elevation: 35 }, dl);
    var ink = svg("filter", { id: id + "ink", x: "-5%", y: "-5%", width: "110%", height: "110%" }, defs);
    svg("feTurbulence", { type: "fractalNoise", baseFrequency: 0.9, numOctaves: 1, seed: 3, result: "t" }, ink);
    svg("feDisplacementMap", { "in": "SourceGraphic", in2: "t", scale: o.ink || 0.6, xChannelSelector: "R", yChannelSelector: "G" }, ink);
    var gr = svg("filter", { id: id + "grain", x: 0, y: 0, width: "100%", height: "100%" }, defs);
    svg("feTurbulence", { type: "fractalNoise", baseFrequency: 0.85, numOctaves: 2, seed: 11, stitchTiles: "stitch" }, gr);
    svg("feColorMatrix", { values: "0 0 0 0 0.5  0 0 0 0 0.5  0 0 0 0 0.5  0 0 0 " + (o.grain || 0.1) + " 0" }, gr);
    var sh = svg("filter", { id: id + "shadow", x: "-50%", y: "-50%", width: "200%", height: "200%" }, defs);
    svg("feGaussianBlur", { stdDeviation: o.shadowBlur }, sh);
    // light and grain stay inside the object: no square of grain or blend on the panel around it
    var cp = svg("clipPath", { id: id + "clip" }, defs);
    svg("circle", { cx: o.cx, cy: o.cx, r: o.clipR }, cp);
    return defs;
  }
  // returns the group to draw the height map into
  function lightLayer(s, id, W, H) {
    var outer = svg("g", { "class": "pr-light", style: "mix-blend-mode: soft-light", "clip-path": "url(#" + id + "clip)" }, s);
    var g = svg("g", { filter: "url(#" + id + "lit)" }, outer);
    svg("rect", { x: -W, y: -H, width: 3 * W, height: 3 * H, fill: "#808080" }, g);
    return g;
  }
  function grainLayer(s, id, W, H) {
    svg("rect", { x: 0, y: 0, width: W, height: H, filter: "url(#" + id + "grain)", style: "mix-blend-mode: multiply", "clip-path": "url(#" + id + "clip)", "pointer-events": "none" }, s);
  }
  // labels printed at the majors: explicit list, or formatted values at even steps
  function printed(fmt, v) { return fmt(v).text.replace(/\.0+$/, ""); }
  function scaleText(o, n, sc, fmt) {
    if (o.scaleLabels) return o.scaleLabels;
    var out = [];
    for (var i = 0; i < n; i++) out.push(printed(fmt, sc.fromNorm(i / (n - 1))));
    return out;
  }
  function rotaryShell(host, o, cls, W, H) {
    var col = controlColumn(host, o);
    var box = el("div", "pr-rotary " + cls + " pr-focusable", col);
    box.style.width = W + "px"; box.style.height = H + "px";
    var s = svg("svg", { width: W, height: H, viewBox: "0 0 " + W + " " + H, "aria-hidden": "true" }, box);
    return { col: col, box: box, s: s };
  }
  function controller(col, ctl) {
    return {
      el: col,
      get value() { return ctl.state.value; },
      set value(v) { ctl.set(v, true); },
      setValue: function (v, silent) { ctl.set(v, silent !== false); },
      destroy: function () { col.remove(); }
    };
  }

  // ---------- Knob: the matte chickenhead over a scale printed on the panel ----------
  var KNOB_D = { lg: 112, md: 80, sm: 56, xs: 56 };
  // xs is the sm drawing shown at knob-xs (40 px): scaled whole, so its ticks and relief thin with it
  var KNOB_SHOWN = { xs: 40 };
  var HEADS = { ink: "--knob-head-ink", paper: "--knob-head-paper", indigo: "--knob-head-indigo", ochre: "--knob-head-ochre" };
  var HEAD_PATH = "M0 -68 C8 -63 13 -40 16 -16 C18 3 16 19 0 23 C-16 19 -18 3 -16 -16 C-13 -40 -8 -63 0 -68 Z"; // drawn for a 113 px plate radius

  function Knob(host, o) {
    o = Object.assign({ min: 0, max: 1, value: 0, size: "md" }, o);
    o.head = o.head || o.cap || "ink";
    var D = KNOB_D[o.size] || 80, c = D / 2, R = D / 2 - 1, k = R / 113;
    var id = "prk" + (++rotSeq);
    var sh = rotaryShell(host, o, "pr-knob pr-knob--" + o.size, D, D), s = sh.s;
    if (KNOB_SHOWN[o.size]) {
      var shown = KNOB_SHOWN[o.size];
      sh.box.style.width = sh.box.style.height = shown + "px";
      s.setAttribute("width", shown); s.setAttribute("height", shown);
    }
    materialDefs(s, id, D, D, { blur: Math.max(1, 3.2 * k), relief: 7, shadowBlur: Math.max(1.5, 6 * k), ink: 0.5, cx: c, clipR: R });

    // the scale, printed straight on the panel: no plate and no ring, so the panel shows through
    var print = svg("g", { "class": "pr-knob__print", filter: "url(#" + id + "ink)" }, s);
    var sc = makeScale(o), fmt = o.format || defaultFormat(o);
    var lg = o.size === "lg", sm = o.size === "sm" || o.size === "xs";
    var tIn = R * (lg ? 0.67 : 0.74), tMaj = R * (lg ? 0.80 : 0.95), tMin = R * (lg ? 0.735 : 0.84);
    var minors = lg ? 50 : sm ? 10 : 20;
    for (var i = 0; i <= minors; i++) {
      var a = START + (END - START) * i / minors, major = i % (minors / 10) === 0;
      if (!major && sm) continue;
      var p0 = polar(c, c, tIn, a), p1 = polar(c, c, major ? tMaj : tMin, a);
      svg("line", { x1: p0[0], y1: p0[1], x2: p1[0], y2: p1[1], "stroke-width": major ? (sm ? 1.2 : 1.5) : 0.7 }, print);
    }
    if (o.bipolar) {
      var pc = polar(c, c, R * 0.97, 0);
      svg("circle", { cx: pc[0], cy: pc[1], r: 1.6, "class": "pr-knob__mark" }, print);
    }
    if (lg && o.scaleLabels !== false) {
      var labels = o.scaleLabels || ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"];
      labels.forEach(function (t, n) {
        var q = polar(c, c, R * 0.885, START + (END - START) * n / (labels.length - 1));
        var tx = svg("text", { x: q[0], y: q[1], "text-anchor": "middle", "dominant-baseline": "central", "class": "pr-knob__num" }, print);
        tx.textContent = t;
      });
    }

    // cast shadow, body, relief
    var shadow = svg("g", { opacity: 0.38, transform: "translate(" + 5 * k + " " + 9 * k + ")" }, s);
    var shadowRot = svg("g", {}, svg("g", { filter: "url(#" + id + "shadow)" }, shadow));
    svg("circle", { r: 34 * k, "class": "pr-knob__shadow" }, shadowRot);
    svg("path", { d: HEAD_PATH, transform: "scale(" + k + ")", "class": "pr-knob__shadow" }, shadowRot);
    var body = svg("g", {}, s);
    svg("circle", { r: 34 * k, "class": "pr-knob__skirt" }, body);
    var nk = Math.max(16, Math.min(48, Math.round(2 * Math.PI * 34 * k / 3)));
    for (var j = 0; j < nk; j++) {
      var q0 = polar(0, 0, 30 * k, j * 360 / nk), q1 = polar(0, 0, 34 * k, j * 360 / nk);
      svg("line", { x1: q0[0], y1: q0[1], x2: q1[0], y2: q1[1], "class": "pr-knob__knurl", "stroke-width": Math.max(0.8, 1.3 * k) }, body);
    }
    svg("path", { d: HEAD_PATH, transform: "scale(" + k + ")", style: "fill:var(" + (HEADS[o.head] || HEADS.ink) + ")" }, body);
    svg("rect", { x: -1.4 * k - 0.3, y: -63 * k, width: 2.8 * k + 0.6, height: 44 * k, rx: 1.4 * k + 0.3, "class": o.head === "paper" ? "pr-knob__inlay pr-knob__inlay--dark" : "pr-knob__inlay" }, body);
    var hm = svg("g", {}, lightLayer(s, id, D, D));
    svg("circle", { r: 34 * k, fill: "#c8c8c8" }, hm);
    svg("path", { d: HEAD_PATH, transform: "scale(" + k + ")", fill: "#ffffff" }, hm);
    // no grain of its own: over the whole disc it would shade the panel round the knob

    var valueEl = el("div", "pr-ctl__value", sh.col);
    var ctl = continuous(o, sh.box, function () { return 200; }, function (n, f) {
      var tr = "translate(" + c + " " + c + ") rotate(" + (START + (END - START) * n) + ")";
      body.setAttribute("transform", tr); hm.setAttribute("transform", tr); shadowRot.setAttribute("transform", tr);
      writeValue(valueEl, f);
    });
    return controller(sh.col, ctl);
  }

  // ---------- Dial: the hero. A printed disc turns under a fixed index, read through a loupe ----------
  function Dial(host, o) {
    o = Object.assign({ min: 0, max: 1, value: 0, size: 220 }, o);
    var Wd = o.size, c = 120, id = "prd" + (++rotSeq), k = Wd / 240;
    var col = controlColumn(host, o);
    var box = el("div", "pr-rotary pr-dial pr-focusable", col);
    box.style.width = Wd + "px"; box.style.height = Wd + "px";
    var s = svg("svg", { width: Wd, height: Wd, viewBox: "0 0 240 240", "aria-hidden": "true" }, box);
    var defs = materialDefs(s, id, 240, 240, { blur: 2.2, relief: 5, shadowBlur: 6, ink: 0.8, cx: 120, clipR: 108 });
    var clip = svg("clipPath", { id: id + "lens" }, defs);
    svg("circle", { cx: 120, cy: 44, r: 21 }, clip);
    var vg = svg("radialGradient", { id: id + "vig", cx: 0.5, cy: 0.5, r: 0.5 }, defs);
    svg("stop", { offset: 0.7, "stop-color": "#000000", "stop-opacity": 0 }, vg);
    svg("stop", { offset: 1, "stop-color": "#000000", "stop-opacity": 0.28 }, vg);

    svg("circle", { cx: c, cy: c, r: 108, "class": "pr-dial__bezel" }, s);
    svg("circle", { cx: c + 3, cy: c + 6, r: 100, "class": "pr-knob__shadow", opacity: 0.5, filter: "url(#" + id + "shadow)" }, s);
    var disc = svg("g", { id: id + "disc" }, s);
    svg("circle", { cx: c, cy: c, r: 101, "class": "pr-dial__rim" }, disc);
    for (var i = 0; i < 120; i++) {
      var p0 = polar(c, c, 96.5, i * 3), p1 = polar(c, c, 101, i * 3);
      svg("line", { x1: p0[0], y1: p0[1], x2: p1[0], y2: p1[1], "class": "pr-dial__grip" }, disc);
    }
    svg("circle", { cx: c, cy: c, r: 96, "class": "pr-dial__face" }, disc);
    var print = svg("g", { "class": "pr-dial__print", filter: "url(#" + id + "ink)" }, disc);
    for (var t = 0; t <= 100; t++) {
      var a = START + (END - START) * t / 100, major = t % 10 === 0, mid = t % 5 === 0;
      var q0 = polar(c, c, 94, a), q1 = polar(c, c, major ? 80 : mid ? 85 : 89, a);
      svg("line", { x1: q0[0], y1: q0[1], x2: q1[0], y2: q1[1], "stroke-width": major ? 1.3 : 0.6 }, print);
    }
    var sc = makeScale(o), fmt = o.format || defaultFormat(o);
    scaleText(o, 11, sc, fmt).forEach(function (txt, n) {
      var a = START + (END - START) * n / 10, q = polar(c, c, 70, a);
      var tx = svg("text", { x: q[0], y: q[1], transform: "rotate(" + a + " " + q[0] + " " + q[1] + ")", "text-anchor": "middle", "dominant-baseline": "central", "class": "pr-dial__num" }, print);
      tx.textContent = txt;
    });
    svg("circle", { cx: c, cy: c, r: 58, fill: "none", "stroke-width": 0.6 }, print);
    if (o.zone) {
      svg("path", { d: arcPath(c, c, 54, START + (END - START) * o.zone[0], START + (END - START) * o.zone[1]), "class": "pr-dial__zone" }, print);
    }
    var brand = svg("text", { x: c, y: c + 30, "text-anchor": "middle", "class": "pr-dial__brand" }, print);
    brand.textContent = "PRAYROOM";
    svg("circle", { cx: c, cy: c, r: 5, "class": "pr-dial__axle" }, disc);
    svg("line", { x1: c, y1: c - 104, x2: c, y2: c - 46, "class": "pr-dial__index" }, s);
    svg("circle", { cx: 124, cy: 50, r: 23, "class": "pr-knob__shadow", opacity: 0.32, filter: "url(#" + id + "shadow)" }, s);
    var lens = svg("g", { "clip-path": "url(#" + id + "lens)" }, s);
    svg("use", { href: "#" + id + "disc", transform: "translate(120 44) scale(1.8) translate(-120 -44)" }, lens);
    svg("line", { x1: c, y1: 16, x2: c, y2: 72, "class": "pr-dial__index", "stroke-width": 1.6 }, lens);
    svg("circle", { cx: 120, cy: 44, r: 21, fill: "url(#" + id + "vig)" }, lens);
    svg("circle", { cx: 120, cy: 44, r: 22.5, "class": "pr-dial__loupe" }, s);
    var lt = lightLayer(s, id, 240, 240);
    var hmDisc = svg("g", {}, lt);
    svg("circle", { cx: c, cy: c, r: 101, fill: "#d8d8d8" }, hmDisc);
    svg("circle", { cx: c, cy: c, r: 96, fill: "#c8c8c8" }, hmDisc);
    svg("circle", { cx: 120, cy: 44, r: 24.5, fill: "#ffffff" }, lt);
    svg("circle", { cx: 120, cy: 44, r: 21, fill: "#808080" }, lt);
    grainLayer(s, id, 240, 240);

    var valueEl = el("div", "pr-ctl__value", col);
    var ctl = continuous(o, box, function () { return 360; }, function (n, f) {
      var tr = "rotate(" + (-(START + (END - START) * n)) + " " + c + " " + c + ")";
      disc.setAttribute("transform", tr); hmDisc.setAttribute("transform", tr);
      writeValue(valueEl, f);
    });
    return controller(col, ctl);
  }

  // ---------- Drum: a vertical drum turning behind a slot ----------
  // Row i sits at (i - value) * STEP degrees on the drum and is projected flat: y = R sin, height x cos.
  function Drum(host, o) {
    o = Object.assign({ min: 0, max: 1, value: 0, steps: 100, height: 220, width: 150 }, o);
    var STEP = 3.6, R = (o.height - 36) / 2;
    var col = controlColumn(host, o);
    var box = el("div", "pr-drum pr-focusable", col);
    box.style.width = o.width + "px"; box.style.height = o.height + "px";
    var skin = el("div", "pr-drum__skin", box);
    var sc = makeScale(o), fmt = o.format || defaultFormat(o);
    var rows = [];
    for (var i = 0; i <= o.steps; i++) {
      var major = i % 10 === 0, mid = i % 5 === 0;
      var r = el("div", "pr-drum__row", skin);
      var tk = el("span", "pr-drum__tick" + (major ? " is-major" : mid ? " is-mid" : ""), r);
      if (mid) { var lb = el("span", "pr-drum__num" + (major ? " is-major" : ""), r); lb.textContent = printed(fmt, sc.fromNorm(i / o.steps)); }
      rows.push(r);
    }
    el("div", "pr-drum__shade", box);
    el("div", "pr-index pr-index--h", box);
    el("div", "pr-index__arrow", box);
    el("div", "pr-drum__frame", box);
    var valueEl = el("div", "pr-ctl__value", col);
    var ctl = continuous(o, box, function () { return o.steps * STEP * Math.PI / 180 * R; }, function (n, f) {
      var cur = n * o.steps;
      for (var i = 0; i < rows.length; i++) {
        var th = (i - cur) * STEP * Math.PI / 180;
        if (Math.abs(th) > 86 * Math.PI / 180) { rows[i].style.opacity = "0"; continue; }
        rows[i].style.opacity = "1";
        rows[i].style.transform = "translateY(" + (R * Math.sin(th)).toFixed(2) + "px) scaleY(" + Math.cos(th).toFixed(3) + ")";
      }
      writeValue(valueEl, f);
    });
    return controller(col, ctl);
  }

  // ---------- Tape: a printed tape sliding behind a window ----------
  function Tape(host, o) {
    o = Object.assign({ min: 0, max: 1, value: 0, steps: 100, width: 300, px: 9 }, o);
    o.dragX = -1; // grab the tape: pulling it left raises the value
    var pad = o.width, Wt = o.steps * o.px + pad * 2, id = "prt" + (++rotSeq);
    var col = controlColumn(host, o);
    var box = el("div", "pr-tape pr-focusable", col);
    box.style.width = o.width + "px";
    var strip = el("div", "pr-tape__strip", box);
    var s = svg("svg", { width: Wt, height: 72, viewBox: "0 0 " + Wt + " 72", "aria-hidden": "true" }, strip);
    var f0 = svg("filter", { id: id + "ink" }, svg("defs", {}, s));
    svg("feTurbulence", { type: "fractalNoise", baseFrequency: 0.9, seed: 3, result: "t" }, f0);
    svg("feDisplacementMap", { "in": "SourceGraphic", in2: "t", scale: 0.8, xChannelSelector: "R", yChannelSelector: "G" }, f0);
    svg("rect", { width: Wt, height: 72, "class": "pr-tape__paper" }, s);
    var print = svg("g", { "class": "pr-tape__print", filter: "url(#" + id + "ink)" }, s);
    var sc = makeScale(o), fmt = o.format || defaultFormat(o);
    for (var i = 0; i <= o.steps; i++) {
      var x = pad + i * o.px, major = i % 10 === 0, mid = i % 5 === 0;
      svg("line", { x1: x, y1: major ? 34 : mid ? 40 : 44, x2: x, y2: 56, "stroke-width": major ? 1.3 : 0.7 }, print);
      if (major) { var t = svg("text", { x: x, y: 26, "text-anchor": "middle", "class": "pr-tape__num" }, print); t.textContent = printed(fmt, sc.fromNorm(i / o.steps)); }
    }
    svg("line", { x1: pad, y1: 58, x2: pad + o.steps * o.px, y2: 58, "stroke-width": 0.8 }, print);
    if (o.zone) svg("rect", { x: pad + o.zone[0] * o.steps * o.px, y: 62, width: (o.zone[1] - o.zone[0]) * o.steps * o.px, height: 3, "class": "pr-tape__zone" }, print);
    el("div", "pr-tape__shade", box);
    el("div", "pr-index pr-index--v", box);
    var valueEl = el("div", "pr-ctl__value", col);
    var ctl = continuous(o, box, function () { return o.steps * o.px; }, function (n, f) {
      strip.style.transform = "translateX(" + (o.width / 2 - pad - n * o.steps * o.px).toFixed(1) + "px)";
      writeValue(valueEl, f);
    });
    return controller(col, ctl);
  }

  // ---------- Encoder: a rubber knob in a well, its value shown by a ring of LEDs ----------
  var ENC_D = { lg: 160, md: 120, sm: 88 };
  function Encoder(host, o) {
    o = Object.assign({ min: 0, max: 1, value: 0, size: "md" }, o);
    var Wd = ENC_D[o.size] || 120, c = 120, id = "pre" + (++rotSeq);
    var col = controlColumn(host, o);
    var box = el("div", "pr-rotary pr-encoder pr-focusable", col);
    box.style.width = Wd + "px"; box.style.height = Wd + "px";
    var s = svg("svg", { width: Wd, height: Wd, viewBox: "0 0 240 240", "aria-hidden": "true" }, box);
    var defs = materialDefs(s, id, 240, 240, { blur: 2.6, relief: 6, shadowBlur: 6, grain: 0.14, cx: 120, clipR: 100 });
    var glow = svg("filter", { id: id + "glow", x: "-200%", y: "-200%", width: "500%", height: "500%" }, defs);
    svg("feGaussianBlur", { stdDeviation: 3.2, result: "b" }, glow);
    var mg = svg("feMerge", {}, glow); svg("feMergeNode", { "in": "b" }, mg); svg("feMergeNode", { "in": "SourceGraphic" }, mg);
    svg("circle", { cx: c, cy: c, r: 100, "class": "pr-encoder__well" }, s);
    var leds = [];
    for (var i = 0; i < 25; i++) {
      var q = polar(c, c, 84, START + (END - START) * i / 24);
      leds.push(svg("circle", { cx: q[0], cy: q[1], r: Math.max(3.3, 3.3 * 120 / Wd), "class": "pr-encoder__led" }, s));
    }
    svg("circle", { cx: c + 4, cy: c + 8, r: 62, "class": "pr-knob__shadow", opacity: 0.6, filter: "url(#" + id + "shadow)" }, s);
    svg("circle", { cx: c, cy: c, r: 62, "class": "pr-encoder__rubber" }, s);
    var turn = svg("g", {}, s);
    for (var j = 0; j < 72; j++) {
      var p0 = polar(c, c, 55, j * 5), p1 = polar(c, c, 61.5, j * 5);
      svg("line", { x1: p0[0], y1: p0[1], x2: p1[0], y2: p1[1], "class": "pr-encoder__grip" }, turn);
    }
    svg("circle", { cx: c, cy: c, r: 46, "class": "pr-encoder__dish" }, s);
    var dot = svg("g", {}, s);
    var dq = polar(c, c, 38, 0);
    svg("circle", { cx: dq[0], cy: dq[1], r: 3, "class": "pr-encoder__dot" }, dot);
    var lt = lightLayer(s, id, 240, 240);
    svg("circle", { cx: c, cy: c, r: 100, fill: "#3a3a3a" }, lt);
    svg("circle", { cx: c, cy: c, r: 62, fill: "#ffffff" }, lt);
    svg("circle", { cx: c, cy: c, r: 46, fill: "#c4c4c4" }, lt);
    svg("circle", { cx: c, cy: c, r: 30, fill: "#b4b4b4" }, lt);
    grainLayer(s, id, 240, 240);

    var sc = makeScale(o);
    var originN = o.bipolar ? clamp(sc.toNorm(o.bipolarCenter != null ? o.bipolarCenter : (o.min + o.max) / 2), 0, 1) : 0;
    var valueEl = el("div", "pr-ctl__value", col);
    var ctl = continuous(o, box, function () { return 200; }, function (n, f) {
      var tr = "rotate(" + (START + (END - START) * n) + " " + c + " " + c + ")";
      turn.setAttribute("transform", tr); dot.setAttribute("transform", tr);
      var lo = Math.min(originN, n), hi = Math.max(originN, n);
      leds.forEach(function (L, i) {
        var t = i / 24, on = t >= lo - 1e-9 && t <= hi + 1e-9 && !(o.bipolar && Math.abs(n - originN) < 1 / 48 && Math.abs(t - originN) > 1e-9);
        L.setAttribute("class", on ? "pr-encoder__led is-on" : "pr-encoder__led");
        if (on) L.setAttribute("filter", "url(#" + id + "glow)"); else L.removeAttribute("filter");
      });
      writeValue(valueEl, f);
    });
    return controller(col, ctl);
  }

  // ---------- Fader ----------
  function Fader(host, o) {
    o = Object.assign({ min: 0, max: 1, value: 0, height: 120 }, o);
    if (o.orientation === "horizontal") return hFader(host, o);
    var col = controlColumn(host, o);
    var fader = el("div", "pr-fader pr-focusable", col);
    fader.style.height = o.height + "px";
    var track = el("div", "pr-fader__track", fader);
    var fill = el("div", "pr-fader__fill", track);
    var thumb = el("div", "pr-fader__thumb", fader);
    var valueEl = el("div", "pr-ctl__value", col);
    fader.setAttribute("aria-orientation", "vertical");
    var travel = function () { return o.height - 12; };
    var ctl = continuous(o, fader, travel, function (n, f) {
      thumb.style.top = (6 + (1 - n) * travel()) + "px";
      fill.style.height = (n * 100) + "%";
      writeValue(valueEl, f);
    });
    return {
      el: col,
      get value() { return ctl.state.value; },
      set value(v) { ctl.set(v, true); },
      setValue: function (v, silent) { ctl.set(v, silent !== false); },
      destroy: function () { col.remove(); }
    };
  }

  // A Fader on its side, for a track's volume in a timeline head: its name is spoken only
  // (the track's name is beside it), its value at the right; `mark` draws a line at a value
  // (0 dB). It drags sideways, relative to where it was grabbed.
  function hFader(host, o) {
    o = Object.assign({ dragX: 1, dragY: 0 }, o);
    var row = el("div", "pr-hfader", host);
    var fader = el("div", "pr-fader pr-fader--h pr-focusable", row);
    var track = el("div", "pr-fader__track", fader);
    var fill = el("div", "pr-fader__fill", track);
    var mark = o.mark != null ? el("div", "pr-fader__mark", fader) : null;
    var thumb = el("div", "pr-fader__thumb", fader);
    var valueEl = el("div", "pr-ctl__value", row);
    fader.setAttribute("aria-orientation", "horizontal");
    var sc = makeScale(o);
    if (mark) mark.style.left = clamp(sc.toNorm(o.mark), 0, 1) * 100 + "%";
    var ctl = continuous(o, fader, function () { return fader.clientWidth || 120; }, function (n, f) {
      thumb.style.left = n * 100 + "%";
      fill.style.width = n * 100 + "%";
      writeValue(valueEl, f);
    });
    return {
      el: row,
      get value() { return ctl.state.value; },
      set value(v) { ctl.set(v, true); },
      setValue: function (v, silent) { ctl.set(v, silent !== false); },
      destroy: function () { row.remove(); }
    };
  }

  // ---------- Track colours ----------
  // A colour of its own for each track of a mixing app, for its head's strip and its
  // waveform: equal hue steps from yellow (70) to magenta (330), so none is taken for 朱
  // (hue 29). One lightness for every theme, at least 3:1 on the clip in light and dark.
  function trackColor(i, n) {
    var h = n > 1 ? 70 + 260 * i / (n - 1) : 200;
    return "oklch(0.58 0.1 " + h + ")";
  }

  // ---------- Glyph: the shape of a filter, for a band's switch and selector ----------
  var GLYPHS = {
    hp: "M1 11 C5 11 6 3 9 3 L17 3",
    ls: "M1 3 L5 3 C8 3 9 8 12 8 L17 8",
    bell: "M1 9 L5 9 C7 9 8 2 9 2 C10 2 11 9 13 9 L17 9",
    hs: "M1 8 L6 8 C9 8 10 3 13 3 L17 3",
    lp: "M1 3 L9 3 C12 3 13 11 17 11"
  };
  function glyph(kind, parent) {
    var s = svg("svg", { "class": "pr-glyph", viewBox: "0 0 18 12", "aria-hidden": "true" }, parent);
    svg("path", { d: GLYPHS[kind] }, s);
    return s;
  }

  // An LED lit in a colour other than 朱: the colour stands in for active, the lit face is
  // it made lighter at the same hue, its spill a translucent copy of it.
  function tint(node, color) {
    node.style.setProperty("--active", color);
    node.style.setProperty("--led-lit", "oklch(from " + color + " calc(l + 0.16) c h)");
    node.style.setProperty("--led-glow", "oklch(from " + color + " l c h / 0.65)");
  }

  // ---------- Toggle ----------
  function Toggle(host, o) {
    o = Object.assign({ pressed: false }, o);
    var b = el("button", "pr-toggle pr-focusable", host);
    b.type = "button";
    el("span", "pr-toggle__led", b).setAttribute("aria-hidden", "true");
    if (o.glyph) glyph(o.glyph, b);
    el("span", "pr-toggle__label", b).textContent = o.label || "";
    if (o.name) b.setAttribute("aria-label", o.name);
    function setColor(c) { b.classList.add("pr-toggle--tinted"); tint(b, c); }
    if (o.color) setColor(o.color);
    var pressed = !!o.pressed;
    function render() { b.setAttribute("aria-pressed", pressed ? "true" : "false"); }
    b.addEventListener("click", function () { pressed = !pressed; render(); if (o.onChange) o.onChange(pressed); });
    if (o.disabled) b.disabled = true;
    render();
    return {
      el: b,
      get pressed() { return pressed; },
      set pressed(v) { pressed = !!v; render(); },
      setColor: setColor,
      destroy: function () { b.remove(); }
    };
  }

  // ---------- Key ----------
  // A key without an LED: an action (play, expand, close, a dialog's buttons), or a state
  // the caller holds and shows with `pressed` (mute, solo). It does not toggle itself.
  function Key(host, o) {
    o = Object.assign({ label: "" }, o);
    var b = el("button", "pr-key pr-focusable" + (o.icon && !o.label ? " pr-key--icon" : "") + (o.primary ? " pr-key--primary" : ""), host);
    b.type = "button";
    var mark = null;
    function setIcon(name) {
      if (mark) mark.remove();
      mark = name ? el("i", "pr-icon pr-icon--" + name) : null;
      if (mark) { mark.setAttribute("aria-hidden", "true"); b.insertBefore(mark, b.firstChild); }
    }
    setIcon(o.icon);
    if (o.label) el("span", null, b).textContent = o.label;
    if (o.name) b.setAttribute("aria-label", o.name);
    function setPressed(v) { if (v == null) b.removeAttribute("aria-pressed"); else b.setAttribute("aria-pressed", v ? "true" : "false"); }
    setPressed(o.pressed);
    if (o.disabled) b.disabled = true;
    b.addEventListener("click", function () { if (o.onClick) o.onClick(); });
    return {
      el: b,
      set pressed(v) { setPressed(v); },
      set disabled(v) { b.disabled = !!v; },
      setIcon: setIcon,
      destroy: function () { b.remove(); }
    };
  }

  // ---------- Badge ----------
  // A mode named on a graph, in ink: placed by the caller (a corner of the graph)
  function Badge(host, o) {
    var e = el("span", "pr-badge", host);
    e.textContent = (o && o.label) || "";
    if (o && o.hidden) e.hidden = true;
    return { el: e, set hidden(v) { e.hidden = !!v; }, destroy: function () { e.remove(); } };
  }

  // ---------- Tag ----------
  // What a knob's value is set for, in a frame under the knob (not filled: filled, it would
  // read as a state, which is Badge's). Off, it keeps its room, as it sits in the knobs' flow.
  function Tag(host, o) {
    o = o || {};
    var e = el("span", "pr-tag", host);
    var label = "";
    function setLabel(v) { label = v || ""; e.textContent = label || "\u00a0"; }
    setLabel(o.label);
    e.classList.toggle("is-off", !!o.off);
    return {
      el: e,
      get label() { return label; },
      set label(v) { setLabel(v); },
      get off() { return e.classList.contains("is-off"); },
      set off(v) { e.classList.toggle("is-off", !!v); },
      destroy: function () { e.remove(); }
    };
  }

  // ---------- Bracket ----------
  // A bracket under the tags of neighbouring knobs, opening down onto the one tag for their
  // values together, which the caller puts in `el`. Off, it keeps its room too.
  function Bracket(host, o) {
    o = o || {};
    var e = el("div", "pr-bracket", host);
    e.classList.toggle("is-off", !!o.off);
    return {
      el: e,
      get off() { return e.classList.contains("is-off"); },
      set off(v) { e.classList.toggle("is-off", !!v); },
      destroy: function () { e.remove(); }
    };
  }

  // ---------- Device ----------
  // One device of a rack: the power switch, the name, and at the right the key that opens
  // it large (or, opened large, closes it). It names no track: which track it works on is
  // the one selected in the timeline.
  function Device(host, o) {
    o = Object.assign({ on: true, big: false }, o);
    var root = el("section", "pr-device", host);
    root.setAttribute("aria-label", o.name);
    var head = el("div", "pr-device__head", root);
    var sw = Switch(head, { label: o.name + " Bypass", on: o.on, onChange: o.onPower });
    el("span", "pr-device__name", head).textContent = o.name;
    if (o.onExpand || o.onClose) {
      Key(head, o.big ? { icon: "close", name: "閉じる", onClick: o.onClose } : { icon: "expand", name: "大きく開く", onClick: o.onExpand });
    }
    var body = el("div", "pr-device__body", root);
    return {
      el: root,
      head: head,
      body: body,
      get on() { return sw.on; },
      set on(v) { sw.on = v; },
      destroy: function () { root.remove(); }
    };
  }

  // ---------- Scrim ----------
  // The veil (ink at 35%) under a device opened large. Esc, or a press on the scrim outside the
  // device, calls onClose; put the large device in `el`.
  function Scrim(host, o) {
    o = o || {};
    var s = el("div", "pr-scrim", host || document.body);
    s.addEventListener("pointerdown", function (e) { if (e.target === s && o.onClose) o.onClose(); });
    function key(e) { if (e.key === "Escape" && o.onClose) o.onClose(); }
    document.addEventListener("keydown", key);
    return { el: s, destroy: function () { document.removeEventListener("keydown", key); s.remove(); } };
  }

  // ---------- Lamp ----------
  // A square key lit from inside, for a two-state setting in a row of knobs. It draws no
  // name; the name sits above it as the row's label.
  function Lamp(host, o) {
    o = Object.assign({ pressed: false, size: "md" }, o);
    var b = el("button", "pr-lamp pr-focusable" + (o.size === "lg" ? " pr-lamp--lg" : ""), host);
    b.type = "button";
    b.setAttribute("aria-label", o.label || "");
    el("span", "pr-lamp__cap", b).setAttribute("aria-hidden", "true");
    var pressed = !!o.pressed;
    function render() { b.setAttribute("aria-pressed", pressed ? "true" : "false"); }
    b.addEventListener("click", function () { pressed = !pressed; render(); if (o.onChange) o.onChange(pressed); });
    if (o.disabled) b.disabled = true;
    render();
    return {
      el: b,
      get pressed() { return pressed; },
      set pressed(v) { pressed = !!v; render(); },
      destroy: function () { b.remove(); }
    };
  }

  // ---------- Switch ----------
  // A slide switch for a whole device's power: a ridged block in a slot, at the right
  // and over a 朱 slot when on. It draws no name; the name sits beside it as text.
  function Switch(host, o) {
    o = Object.assign({ on: true }, o);
    var b = el("button", "pr-switch pr-focusable", host);
    b.type = "button";
    b.setAttribute("role", "switch");
    if (o.label) b.setAttribute("aria-label", o.label);
    el("span", "pr-switch__slot", b).setAttribute("aria-hidden", "true");
    el("span", "pr-switch__block", b).setAttribute("aria-hidden", "true");
    var on = !!o.on;
    function render() { b.setAttribute("aria-checked", on ? "true" : "false"); }
    b.addEventListener("click", function () { on = !on; render(); if (o.onChange) o.onChange(on); });
    if (o.disabled) b.disabled = true;
    render();
    return {
      el: b,
      get on() { return on; },
      set on(v) { on = !!v; render(); },
      destroy: function () { b.remove(); }
    };
  }

  // ---------- Hint ----------
  // What the control under the pointer is: its name and a few lines on it, on a dark,
  // blurred pane floating beside the device (not following the pointer), level with the
  // control. Inside the device's left edge when there is no room to its left.
  var hintCount = 0;
  function Hint(host, o) {
    o = o || {};
    var root = el("div", "pr-hint", host);
    root.id = "pr-hint-" + (++hintCount);
    root.setAttribute("role", "tooltip");
    var title = el("div", "pr-hint__title", root);
    var aside = el("div", "pr-hint__aside", root);
    var text = el("div", "pr-hint__text", root);
    var shown = false;
    var last = null; // where it was shown, to place it again
    function place(at) {
      var r = at.beside.getBoundingClientRect ? at.beside.getBoundingClientRect() : at.beside;
      var gap = parseFloat(cssVar(root, "--space-2")) || 8;
      var w = root.offsetWidth, h = root.offsetHeight;
      var x = !at.inside && r.left - gap - w >= gap ? r.left - gap - w : r.left + gap;
      var y = at.y == null ? r.top + r.height / 2 : at.y;
      root.style.left = x + "px";
      root.style.top = clamp(y - h / 2, gap, Math.max(gap, innerHeight - h - gap)) + "px";
    }
    function show(c, at) {
      title.textContent = c.title || "";
      aside.textContent = c.aside || "";
      aside.hidden = !c.aside;
      text.textContent = c.text || "";
      last = at;
      place(at);
      root.classList.add("is-shown");
      shown = true;
    }
    // The face of characters the page has not drawn yet loads after the pane is placed (about
    // 20 ms later), and the text grows, past the bottom of the window: place it again.
    if (window.ResizeObserver) new ResizeObserver(function () { if (shown && last) place(last); }).observe(root);
    function hide() {
      root.classList.remove("is-shown");
      shown = false;
    }
    return {
      el: root,
      show: show,
      hide: hide,
      get shown() { return shown; },
      destroy: function () { root.remove(); }
    };
  }

  // ---------- Segmented (5 or fewer options) / Select (6 or more) ----------
  function Segmented(host, o) {
    var opts = (o.options || []).map(function (x) { return typeof x === "object" ? x : { value: x, label: String(x) }; });
    var value = o.value != null ? o.value : (opts[0] && opts[0].value);
    var col = o.label ? controlColumn(host, o) : host;
    var api = { el: null };

    if ((opts.length > 5 && o.mode !== "segments") || o.mode === "select") {
      var s = el("select", "pr-select pr-focusable", col);
      opts.forEach(function (x) { var op = el("option", null, s); op.value = String(x.value); op.textContent = x.label; });
      s.value = String(value);
      if (o.label) s.setAttribute("aria-label", o.label);
      s.addEventListener("change", function () {
        var hit = opts.filter(function (x) { return String(x.value) === s.value; })[0];
        value = hit ? hit.value : s.value;
        if (o.onChange) o.onChange(value);
      });
      api.el = o.label ? col : s;
      api.setLed = function () {}; // a select draws no LEDs
      Object.defineProperty(api, "value", { get: function () { return value; }, set: function (v) { value = v; s.value = String(v); } });
      return api;
    }

    var g = el("div", "pr-seg" + (o.fill ? " pr-seg--fill" : ""), col);
    g.setAttribute("role", "radiogroup");
    if (o.label) g.setAttribute("aria-label", o.label);
    var leds = [];
    var btns = opts.map(function (x, i) {
      var b = el("button", "pr-seg__opt pr-focusable", g);
      b.type = "button";
      b.setAttribute("role", "radio");
      if (x.name) b.setAttribute("aria-label", x.name);
      if (x.led) { leds[i] = el("i", "pr-seg__led", b); leds[i].setAttribute("aria-hidden", "true"); setLed(i, x.led, x.lit); }
      if (x.glyph) glyph(x.glyph, b);
      if (x.label) el("span", null, b).textContent = x.label;
      b.addEventListener("click", function () { pick(x.value, true); });
      // the arrow keys move the choice and the focus, Home and End go to either end
      b.addEventListener("keydown", function (e) {
        var n = opts.length;
        var to = { ArrowRight: i + 1, ArrowDown: i + 1, ArrowLeft: i - 1, ArrowUp: i - 1, Home: 0, End: n - 1 }[e.key];
        if (to === undefined) return;
        e.preventDefault();
        var j = (to + n) % n;
        pick(opts[j].value, true);
        btns[j].focus();
      });
      return b;
    });
    function setLed(i, color, lit) {
      var d = leds[i];
      if (!d) return;
      if (color) tint(d, color);
      d.classList.toggle("is-lit", !!lit);
    }
    function pick(v, notify) {
      value = v;
      btns.forEach(function (b, i) {
        var on = opts[i].value === v;
        b.setAttribute("aria-checked", on ? "true" : "false");
        b.tabIndex = on ? 0 : -1;
      });
      if (notify && o.onChange) o.onChange(v);
    }
    pick(value, false);
    api.el = o.label ? col : g;
    api.setLed = setLed;
    Object.defineProperty(api, "value", { get: function () { return value; }, set: function (v) { pick(v, false); } });
    return api;
  }

  // the index of the option whose value is v, or -1
  function optionIndex(opts, v) {
    for (var i = 0; i < opts.length; i++) if (opts[i].value === v) return i;
    return -1;
  }

  // ---------- VSlide ----------
  // A vertical slide switch for 2 to 4 steps in a row of knobs: a ridged block in a slot, the
  // step names beside it (the chosen one bold), the largest value on top. The names stand in
  // for the value line, so the body runs from the knob's top edge to the bottom of that line.
  // The steps are under hit-min tall, so the whole body is the drag surface: a press anywhere
  // moves to that step and a drag snaps from step to step.
  function VSlide(host, o) {
    var col = controlColumn(host, o);
    col.classList.add("pr-vslide");
    var body = el("div", "pr-vslide__body pr-focusable", col);
    body.tabIndex = 0;
    body.setAttribute("role", "radiogroup");
    if (o.label) body.setAttribute("aria-label", o.label);
    el("span", "pr-vslide__slot", body).setAttribute("aria-hidden", "true");
    var block = el("span", "pr-vslide__block", body);
    block.setAttribute("aria-hidden", "true");
    var opts = o.options.slice().sort(function (a, b) { return b.value - a.value; }), n = opts.length;
    var value = o.value;
    var buttons = opts.map(function (op, i) {
      var b = el("button", "pr-vslide__opt", body);
      b.type = "button";
      b.textContent = op.label;
      b.setAttribute("role", "radio");
      b.tabIndex = -1;
      b.style.top = (i * 100 / n) + "%"; b.style.height = (100 / n) + "%";
      b.addEventListener("click", function () { set(op.value, false); });
      return b;
    });
    function render() {
      var i = optionIndex(opts, value);
      buttons.forEach(function (b, k) { b.setAttribute("aria-checked", k === i ? "true" : "false"); });
      block.style.top = ((i + 0.5) * 100 / n) + "%";
    }
    function set(v, silent) {
      v = clamp(v, opts[n - 1].value, opts[0].value);
      if (v === value) return;
      value = v; render();
      if (!silent && o.onChange) o.onChange(value);
    }
    function stepOf(y) { return opts[clamp(Math.floor(y / body.getBoundingClientRect().height * n), 0, n - 1)].value; }
    var dragging = false;
    // the step names cover the body, so a press anywhere starts a drag (the name's click then repeats the same step)
    body.addEventListener("pointerdown", function (e) { dragging = true; body.setPointerCapture(e.pointerId); set(stepOf(e.clientY - body.getBoundingClientRect().top), false); });
    body.addEventListener("pointermove", function (e) { if (dragging) set(stepOf(e.clientY - body.getBoundingClientRect().top), false); });
    body.addEventListener("pointerup", function () { dragging = false; });
    body.addEventListener("wheel", function (e) { e.preventDefault(); set(value + (e.deltaY < 0 ? 1 : -1), false); }, { passive: false });
    body.addEventListener("keydown", function (e) {
      if (e.key === "ArrowUp" || e.key === "ArrowRight") { e.preventDefault(); set(value + 1, false); }
      else if (e.key === "ArrowDown" || e.key === "ArrowLeft") { e.preventDefault(); set(value - 1, false); }
      else if (e.key === "Home") { e.preventDefault(); set(opts[0].value, false); }
      else if (e.key === "End") { e.preventDefault(); set(opts[n - 1].value, false); }
    });
    render();
    return {
      el: col,
      get value() { return value; },
      set value(v) { set(v, true); },
      setValue: function (v, silent) { set(v, silent !== false); },
      destroy: function () { col.remove(); }
    };
  }

  // ---------- Stepper ----------
  // A chooser one knob column wide, for any number of options: the current name sits on the
  // knob's centre line, with the one before faint above it and the one after faint below, as
  // in the list. A click opens the list; a drag (16 px a step), the wheel and the arrow keys
  // step through without it. The list sits on the body, so no panel clips it, under the window
  // or over it when it would leave the viewport; one list is open at a time.
  var openList = null;
  function Stepper(host, o) {
    var col = controlColumn(host, o);
    col.classList.add("pr-step");
    var box = el("div", "pr-step__box pr-focusable", col);
    box.tabIndex = 0;
    box.setAttribute("role", "combobox");
    if (o.label) box.setAttribute("aria-label", o.label);
    box.setAttribute("aria-haspopup", "listbox");
    box.setAttribute("aria-expanded", "false");
    var prev = el("div", "pr-step__opt pr-step__opt--prev", box), cur = el("div", "pr-step__opt pr-step__opt--cur", box), next = el("div", "pr-step__opt pr-step__opt--next", box);
    el("span", "pr-step__index", box).setAttribute("aria-hidden", "true");
    var opts = o.options, n = opts.length;
    var i = Math.max(0, optionIndex(opts, o.value)), list = null, items = [];
    function render() {
      prev.textContent = i > 0 ? opts[i - 1].label : "";
      cur.textContent = opts[i].label;
      next.textContent = i + 1 < n ? opts[i + 1].label : "";
      box.setAttribute("aria-valuetext", opts[i].label);
      items.forEach(function (b, k) { b.setAttribute("aria-selected", k === i ? "true" : "false"); });
    }
    function go(k, silent) {
      k = clamp(k, 0, n - 1);
      if (k === i) return;
      i = k; render();
      if (!silent && o.onChange) o.onChange(opts[i].value);
    }
    function open() {
      if (list) return;
      if (openList) openList.close();
      list = el("div", "pr-step__list", document.body);
      list.setAttribute("role", "listbox");
      items = opts.map(function (op, k) {
        var b = el("button", "pr-step__item", list);
        b.type = "button";
        b.textContent = op.full || op.label; // the list shows the full name, where the window shows a short one
        b.setAttribute("role", "option");
        b.addEventListener("click", function () { go(k); close(); box.focus(); });
        return b;
      });
      var r = box.getBoundingClientRect(), gap = parseFloat(cssVar(box, "--space-1")) || 4;
      list.style.left = r.left + "px"; list.style.minWidth = r.width + "px";
      var h = list.getBoundingClientRect().height, vh = window.innerHeight;
      if (r.bottom + gap + h <= vh - gap) list.style.top = (r.bottom + gap) + "px";
      else list.style.top = Math.max(gap, r.top - gap - h) + "px";
      box.setAttribute("aria-expanded", "true");
      openList = api; render();
      setTimeout(function () { document.addEventListener("pointerdown", outside); }, 0);
    }
    function close() {
      if (!list) return;
      list.remove(); list = null; items = [];
      box.setAttribute("aria-expanded", "false");
      document.removeEventListener("pointerdown", outside);
      if (openList === api) openList = null;
    }
    function outside(e) { if (!box.contains(e.target) && !(list && list.contains(e.target))) close(); }
    var y0 = null, i0 = 0, moved = false;
    box.addEventListener("pointerdown", function (e) { y0 = e.clientY; i0 = i; moved = false; box.setPointerCapture(e.pointerId); });
    box.addEventListener("pointermove", function (e) { if (y0 == null) return; var d = Math.round((e.clientY - y0) / 16); if (d) { moved = true; go(i0 + d); } });
    box.addEventListener("pointerup", function () { y0 = null; });
    // the click at the end of a drag is not a request for the list
    box.addEventListener("click", function () { if (moved) { moved = false; return; } if (list) close(); else open(); });
    box.addEventListener("wheel", function (e) { e.preventDefault(); go(i + (e.deltaY < 0 ? -1 : 1)); }, { passive: false });
    box.addEventListener("keydown", function (e) {
      if (e.key === "ArrowUp" || e.key === "ArrowLeft") { e.preventDefault(); go(i - 1); }
      else if (e.key === "ArrowDown" || e.key === "ArrowRight") { e.preventDefault(); go(i + 1); }
      else if (e.key === "Home") { e.preventDefault(); go(0); }
      else if (e.key === "End") { e.preventDefault(); go(n - 1); }
      else if (e.key === "Enter" || e.key === " ") { e.preventDefault(); if (list) close(); else open(); }
      else if (e.key === "Escape") { close(); }
    });
    box.addEventListener("blur", function () { setTimeout(function () { if (list && !list.contains(document.activeElement)) close(); }, 0); });
    render();
    var api = {
      el: col,
      get value() { return opts[i].value; },
      set value(v) { go(optionIndex(opts, v), true); },
      setValue: function (v, silent) { go(optionIndex(opts, v), silent !== false); },
      open: open,
      close: close,
      get isOpen() { return !!list; },
      destroy: function () { close(); col.remove(); }
    };
    return api;
  }

  // ---------- OverviewTabs: the sections' names over their main controls; a name shows its section ----------
  // o: { label, tabs: [{ value, label, width? }], value, onChange }. Each tab is a column: its name, then a body the
  // caller fills with the section's main controls (body(value)); a line stands between two tabs.
  function OverviewTabs(host, o) {
    var root = el("div", "pr-ovtabs", host);
    root.setAttribute("role", "tablist");
    if (o.label) root.setAttribute("aria-label", o.label);
    var tabs = o.tabs, i = Math.max(0, optionIndex(tabs, o.value)), names = [], bodies = [];
    tabs.forEach(function (t, k) {
      var sec = el("div", "pr-ovtabs__sec", root);
      if (t.width) sec.style.minWidth = t.width + "px";
      // the whole section is the tab's target: a press anywhere in it (its knobs too) shows the section
      sec.addEventListener("pointerdown", function () { go(k, false); }, true);
      var b = el("button", "pr-ovtabs__name pr-focusable", sec);
      b.type = "button"; b.textContent = t.label; b.setAttribute("role", "tab");
      b.addEventListener("click", function () { go(k, false); });
      b.addEventListener("keydown", function (e) {
        var to = e.key === "ArrowRight" ? k + 1 : e.key === "ArrowLeft" ? k - 1 : e.key === "Home" ? 0 : e.key === "End" ? tabs.length - 1 : null;
        if (to === null) return;
        e.preventDefault(); go(clamp(to, 0, tabs.length - 1), false); names[i].focus();
      });
      names.push(b);
      bodies.push(el("div", "pr-ovtabs__body", sec));
    });
    function render() {
      names.forEach(function (b, k) { b.setAttribute("aria-selected", k === i ? "true" : "false"); b.tabIndex = k === i ? 0 : -1; });
    }
    function go(k, silent) {
      if (k < 0 || k === i) return;
      i = k; render();
      if (!silent && o.onChange) o.onChange(tabs[i].value);
    }
    render();
    return {
      el: root,
      get value() { return tabs[i].value; },
      setValue: function (v, silent) { go(optionIndex(tabs, v), silent !== false); },
      body: function (v) { return bodies[optionIndex(tabs, v)]; },
      destroy: function () { root.remove(); }
    };
  }

  // ---------- Meter ----------
  // mode "level": dBFS from min..0, peak hold, sticky clip lamp (click to reset).
  // mode "gr": gain reduction in dB (positive numbers), bar grows downward from the top.
  function Meter(host, o) {
    o = Object.assign({ channels: 2, height: 160, mode: "level", min: -48, hot: -6, grRange: 24 }, o);
    var gr = o.mode === "gr";
    var wrap = el("div", "pr-meter" + (gr ? " pr-meter--gr" : ""), host);
    wrap.style.height = o.height + "px";
    wrap.setAttribute("role", "meter");
    if (o.label) wrap.setAttribute("aria-label", o.label);

    var scale = el("div", "pr-meter__scale", wrap);
    var scaleIn = el("div", null, scale);
    scaleIn.style.cssText = "position:absolute;left:0;right:0;bottom:0;top:" + (gr ? 0 : 10) + "px";
    var pos = gr
      ? function (db) { return clamp(db / o.grRange, 0, 1); }                 // 0 at top
      : function (db) { return clamp((db - o.min) / (0 - o.min), 0, 1); };    // 0 at bottom
    var ticks = o.ticks || (gr ? [0, 3, 6, 12, 24] : [0, -6, -12, -24, -48]);
    ticks.forEach(function (t) {
      var s = el("span", null, scaleIn);
      s.textContent = gr ? (t === 0 ? "0" : "-" + t) : String(t);
      s.style.top = (gr ? pos(t) : 1 - pos(t)) * 100 + "%";
    });

    var chans = el("div", "pr-meter__chans", wrap);
    var ch = [];
    for (var i = 0; i < o.channels; i++) {
      var c = el("div", "pr-meter__ch", chans);
      var lamp = null;
      if (!gr) {
        lamp = el("div", "pr-meter__lamp", c);
        lamp.title = "Clip — click to reset";
        lamp.addEventListener("click", function () { ch.forEach(function (x) { x.clip = false; x.lamp.classList.remove("is-lit"); }); });
      }
      var well = el("div", "pr-meter__well", c);
      var bar = el("div", "pr-meter__bar", well);
      if (!gr) {
        var p = (1 - pos(o.hot)) * 100;
        bar.style.background = "linear-gradient(to bottom, var(--meter-hot) 0%, var(--meter-hot) " + p + "%, var(--meter-ok) " + p + "%, var(--meter-ok) 100%)";
      }
      var peak = gr ? null : el("div", "pr-meter__peak", well);
      ch.push({ lamp: lamp, bar: bar, peak: peak, target: gr ? 0 : -Infinity, disp: gr ? 0 : o.min, hold: o.min, holdT: 0, clip: false });
    }

    var last = 0, raf = 0, alive = true;
    function frame(t) {
      if (!alive) return;
      var dt = last ? Math.min((t - last) / 1000, 0.1) : 0;
      last = t;
      ch.forEach(function (c) {
        if (gr) {
          // attack instantly, release ~300 ms
          c.disp = c.target > c.disp ? c.target : c.target + (c.disp - c.target) * Math.exp(-dt / 0.1);
          c.bar.style.clipPath = "inset(0 0 " + (1 - pos(c.disp)) * 100 + "% 0)";
          return;
        }
        var tg = Math.max(c.target, o.min);
        c.disp = tg > c.disp ? tg : tg + (c.disp - tg) * Math.exp(-dt / 0.1);
        if (tg >= c.hold) { c.hold = tg; c.holdT = 1.5; }
        else if (c.holdT > 0) c.holdT -= dt;
        else c.hold = Math.max(c.hold - 20 * dt, c.disp);
        if (c.target >= -0.05 && !c.clip) { c.clip = true; c.lamp.classList.add("is-lit"); }
        c.bar.style.clipPath = "inset(" + (1 - pos(c.disp)) * 100 + "% 0 0 0)";
        c.peak.style.top = (1 - pos(c.hold)) * 100 + "%";
        c.peak.style.opacity = c.hold <= o.min ? "0" : "1";
      });
      raf = requestAnimationFrame(frame);
    }
    raf = requestAnimationFrame(frame);

    return {
      el: wrap,
      set: function (values) {
        if (typeof values === "number") values = [values];
        ch.forEach(function (c, i) { var v = values[Math.min(i, values.length - 1)]; c.target = gr ? Math.abs(v) : v; });
      },
      resetClip: function () { ch.forEach(function (c) { c.clip = false; if (c.lamp) c.lamp.classList.remove("is-lit"); }); },
      destroy: function () { alive = false; cancelAnimationFrame(raf); wrap.remove(); }
    };
  }

  // ---------- Graph (EQ response) ----------
  function biquadDb(b, f, fs) {
    var A = Math.pow(10, b.gain / 40), w0 = 2 * Math.PI * b.freq / fs, cs = Math.cos(w0), sn = Math.sin(w0);
    var q = b.q || 0.707, al = sn / (2 * q), sA = Math.sqrt(A);
    var b0, b1, b2, a0, a1, a2;
    if (b.type === "lowshelf") {
      b0 = A * ((A + 1) - (A - 1) * cs + 2 * sA * al); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - 2 * sA * al);
      a0 = (A + 1) + (A - 1) * cs + 2 * sA * al; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - 2 * sA * al;
    } else if (b.type === "highshelf") {
      b0 = A * ((A + 1) + (A - 1) * cs + 2 * sA * al); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - 2 * sA * al);
      a0 = (A + 1) - (A - 1) * cs + 2 * sA * al; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - 2 * sA * al;
    } else {
      b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A;
    }
    var w = 2 * Math.PI * f / fs, c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
    var nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
    var dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
    return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
  }

  // ---------- Frequency colours ----------
  // A colour for each frequency, from pink at the lowest word of an EQ's word rows to green
  // at the highest. The hue takes equal steps from one word to the next (both rows
  // together), not equal steps per octave, so neighbouring words never come out alike;
  // each use has one lightness and chroma, so only the hue tells them apart.
  function freqColors(words, fMin) {
    fMin = fMin || 20;
    var xs = words.map(function (w) { return Math.log(w[0] / fMin); }).sort(function (a, b) { return a - b; });
    var n = xs.length - 1;
    function hue(f) {
      var x = Math.log(f / fMin), k = 0;
      if (n > 0 && x >= xs[n]) k = n;
      else if (n > 0 && x > xs[0]) { var i = 0; while (xs[i + 1] <= x) i++; k = i + (x - xs[i]) / (xs[i + 1] - xs[i]); }
      return ((10 - 260 * k / Math.max(n, 1)) % 360 + 360) % 360;
    }
    return {
      hue: hue,
      // text and outlines, dark enough to read on the tints and the light surfaces
      ink: function (f) { return "oklch(0.5 0.085 " + hue(f) + ")"; },
      // filled marks: points and lit LEDs (a paper-coloured number on it reads at 4.8:1)
      mark: function (f) { return "oklch(0.53 0.085 " + hue(f) + ")"; },
      // the word rows' bands, opaque
      tint: function (f) { return "oklch(0.9 0.045 " + hue(f) + ")"; },
      // washes over the graph, at the given opacity
      wash: function (f, a) { return "oklch(0.6 0.095 " + hue(f) + " / " + a + ")"; }
    };
  }

  function Graph(host, o) {
    o = Object.assign({ width: 360, height: 180, range: 18, fMin: 20, fMax: 20000, sampleRate: 48000, bands: [] }, o);
    var bands = o.bands.map(function (b) { return Object.assign({}, b); });
    // the word rows above and below the plot: what a range sounds like, and what too much of it does
    var ROW = 18, R = o.words ? ROW : 0;
    var fc = o.words ? freqColors(o.words.top.concat(o.words.bottom), o.fMin) : null;
    var hover = null; // the word under the pointer: { row, k }
    var box = el("div", "pr-graph", host);
    box.style.width = typeof o.width === "number" ? o.width + "px" : o.width;
    box.style.height = o.height + "px";
    if (o.label) { box.setAttribute("role", "img"); box.setAttribute("aria-label", o.label); }
    var cv = el("canvas", null, box), ctx = cv.getContext("2d");
    var W = 0, H = 0, active = -1, dragging = -1;

    var xOf = function (f) { return Math.log(f / o.fMin) / Math.log(o.fMax / o.fMin) * W; };
    var fOf = function (x) { return o.fMin * Math.pow(o.fMax / o.fMin, clamp(x / W, 0, 1)); };
    var yOf = function (db) { var h = (H - 2 * R) / 2; return R + h - db / o.range * (h - 10); };
    var dbOf = function (y) { var h = (H - 2 * R) / 2; return clamp((R + h - y) / (h - 10) * o.range, -o.range, o.range); };
    // the columns of a word's band on its row: the bands tile the row edge to edge, meeting
    // midway (on the log scale) between neighbouring ranges
    var tile = function (row, k) {
      return [k ? Math.round(xOf(Math.sqrt(row[k - 1][3] * row[k][2]))) : 0, k < row.length - 1 ? Math.round(xOf(Math.sqrt(row[k][3] * row[k + 1][2]))) : W];
    };

    function draw() {
      var r = box.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
      W = r.width; H = r.height;
      if (!W || !H) return;
      cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, W, H);
      var C = {
        line: cssVar(box, "--line"), strong: cssVar(box, "--line-strong"), muted: cssVar(box, "--ink-muted"),
        viz: cssVar(box, "--viz-primary"), vizFill: cssVar(box, "--viz-primary-fill"), ground: cssVar(box, "--surface-000"),
        mono: cssVar(box, "--font-mono") || "monospace", sans: cssVar(box, "--font-sans") || "sans-serif",
        paper: cssVar(box, "--surface-100")
      };
      // the range of the word under the pointer, as wide as the word's band on its row
      if (hover) {
        var hx = tile(hover.row, hover.k);
        ctx.fillStyle = fc.wash(hover.row[hover.k][0], 0.16);
        ctx.fillRect(hx[0], R, hx[1] - hx[0], H - 2 * R);
      }
      // grid
      ctx.lineWidth = 1;
      ctx.strokeStyle = C.line;
      [50, 100, 200, 500, 1000, 2000, 5000, 10000].forEach(function (f) {
        var x = Math.round(xOf(f)) + 0.5; ctx.beginPath(); ctx.moveTo(x, R); ctx.lineTo(x, H - R); ctx.stroke();
      });
      [-12, -6, 6, 12].forEach(function (d) {
        if (Math.abs(d) > o.range) return;
        var y = Math.round(yOf(d)) + 0.5; ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
      });
      ctx.strokeStyle = C.strong;
      var y0 = Math.round(yOf(0)) + 0.5; ctx.beginPath(); ctx.moveTo(0, y0); ctx.lineTo(W, y0); ctx.stroke();
      // labels
      ctx.fillStyle = C.muted;
      ctx.font = "10px " + C.mono;
      if (fc) {
        // every grid frequency in its own colour under the top row; the dB scale at the
        // left, for the right edge holds 10k, the highest word and the top band's handle
        ctx.textBaseline = "middle";
        [[50, "50"], [100, "100"], [200, "200"], [500, "500"], [1000, "1k"], [2000, "2k"], [5000, "5k"], [10000, "10k"]].forEach(function (p) {
          ctx.fillStyle = fc.ink(p[0]); ctx.fillText(p[1], xOf(p[0]) + 3, R + 9);
        });
        ctx.fillStyle = C.muted;
        [12, 6, -6, -12].forEach(function (d) { if (Math.abs(d) <= o.range) ctx.fillText((d > 0 ? "+" : "") + d, 4, yOf(d) - 6); });
      } else {
        ctx.textBaseline = "bottom";
        [[100, "100"], [1000, "1k"], [10000, "10k"]].forEach(function (p) { ctx.fillText(p[1], xOf(p[0]) + 4, H - 4); });
        ctx.textBaseline = "middle"; ctx.textAlign = "right";
        [12, 6, -6, -12].forEach(function (d) { if (Math.abs(d) <= o.range) ctx.fillText((d > 0 ? "+" : "") + d, W - 6, yOf(d) - 7); });
        ctx.textAlign = "left";
      }
      // response
      var pts = [];
      for (var x = 0; x <= W; x += 1) {
        var f = fOf(x), sum = 0;
        for (var i = 0; i < bands.length; i++) if (!bands[i].bypass) sum += biquadDb(bands[i], f, o.sampleRate);
        pts.push([x, yOf(clamp(sum, -o.range * 1.2, o.range * 1.2))]);
      }
      ctx.beginPath(); ctx.moveTo(0, yOf(0));
      pts.forEach(function (p) { ctx.lineTo(p[0], p[1]); });
      ctx.lineTo(W, yOf(0)); ctx.closePath();
      ctx.fillStyle = C.vizFill; ctx.fill();
      ctx.beginPath();
      pts.forEach(function (p, i) { if (i) ctx.lineTo(p[0], p[1]); else ctx.moveTo(p[0], p[1]); });
      ctx.lineWidth = 1.5; ctx.strokeStyle = C.viz; ctx.stroke();
      // handles: with word rows, each in the colour of the frequency it sits at
      bands.forEach(function (b, i) {
        var c = fc ? fc.mark(b.freq) : C.viz;
        ctx.beginPath(); ctx.arc(xOf(b.freq), yOf(b.gain), 6, 0, Math.PI * 2);
        ctx.fillStyle = (i === active || i === dragging) ? c : C.ground; ctx.fill();
        ctx.lineWidth = 1.5; ctx.strokeStyle = c; ctx.stroke();
      });
      // the word rows: each word on a tint of its own frequency's colour across its whole band,
      // in the middle of it; the top row (what the range sounds like) bold
      if (fc) {
        ctx.fillStyle = C.paper;
        ctx.fillRect(0, 0, W, R); ctx.fillRect(0, H - R, W, R);
        ctx.textAlign = "center"; ctx.textBaseline = "middle";
        [[o.words.top, 700, 0], [o.words.bottom, 500, H - R]].forEach(function (rw) {
          ctx.font = rw[1] + " 11px " + C.sans;
          rw[0].forEach(function (w, k) {
            var t = tile(rw[0], k);
            ctx.fillStyle = fc.tint(w[0]); ctx.fillRect(t[0], rw[2], t[1] - t[0], R);
            ctx.fillStyle = fc.ink(w[0]); ctx.fillText(w[1], (t[0] + t[1]) / 2, rw[2] + R / 2 + 1);
          });
        });
        ctx.textAlign = "left";
      }
    }

    // the word whose band on either row holds the pointer, or null
    function wordAt(e) {
      if (!fc) return null;
      var r = box.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top;
      var row = y < R ? o.words.top : y > H - R ? o.words.bottom : null;
      if (!row) return null;
      for (var k = 0; k < row.length; k++) { var t = tile(row, k); if (x >= t[0] && x < t[1]) return { row: row, k: k }; }
      return null;
    }
    if (fc) {
      box.addEventListener("pointermove", function (e) {
        var w = wordAt(e);
        if ((w && hover && w.row === hover.row && w.k === hover.k) || (!w && !hover)) return;
        hover = w; draw();
      });
      box.addEventListener("pointerleave", function () { if (hover) { hover = null; draw(); } });
    }

    function hitTest(e) {
      var r = box.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top, best = -1, bd = 144;
      bands.forEach(function (b, i) { var dx = xOf(b.freq) - x, dy = yOf(b.gain) - y, d = dx * dx + dy * dy; if (d < bd) { bd = d; best = i; } });
      return best;
    }
    function emit() { if (o.onChange) o.onChange(bands.map(function (b) { return Object.assign({}, b); })); }

    if (o.editable !== false) {
      box.addEventListener("pointerdown", function (e) {
        var i = hitTest(e); if (i < 0) return;
        dragging = i; box.setPointerCapture(e.pointerId); draw(); e.preventDefault();
      });
      box.addEventListener("pointermove", function (e) {
        var r = box.getBoundingClientRect();
        if (dragging >= 0) {
          bands[dragging].freq = Math.round(fOf(e.clientX - r.left));
          bands[dragging].gain = Math.round(dbOf(e.clientY - r.top) * 10) / 10;
          draw(); emit(); return;
        }
        var h = hitTest(e);
        if (h !== active) { active = h; box.style.cursor = h >= 0 ? "grab" : ""; draw(); }
      });
      var stop = function () { if (dragging >= 0) { dragging = -1; draw(); } };
      box.addEventListener("pointerup", stop);
      box.addEventListener("pointercancel", stop);
      box.addEventListener("pointerleave", function () { if (dragging < 0 && active >= 0) { active = -1; draw(); } });
      box.addEventListener("wheel", function (e) {
        var i = hitTest(e); if (i < 0) return;
        e.preventDefault();
        var b = bands[i]; b.q = clamp((b.q || 0.707) * (e.deltaY < 0 ? 1.1 : 1 / 1.1), 0.2, 12);
        draw(); emit();
      }, { passive: false });
    }

    var ro = typeof ResizeObserver !== "undefined" ? new ResizeObserver(draw) : null;
    if (ro) ro.observe(box);
    var mo = new MutationObserver(function () { requestAnimationFrame(draw); });
    mo.observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme", "class", "style"] });
    var mq = window.matchMedia ? window.matchMedia("(prefers-color-scheme: dark)") : null;
    if (mq && mq.addEventListener) mq.addEventListener("change", draw);
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(draw);
    draw();

    return {
      el: box,
      get bands() { return bands.map(function (b) { return Object.assign({}, b); }); },
      setBands: function (next) { bands = next.map(function (b) { return Object.assign({}, b); }); draw(); },
      redraw: draw,
      destroy: function () { if (ro) ro.disconnect(); mo.disconnect(); box.remove(); }
    };
  }

  // ---------- Envelope and PitchPath: a display over the knobs it shows ----------
  // Shared by both: a control column (label over a box with the look of Graph) that is as
  // wide as the place it is given, and runs from the knob's top edge to the bottom of the
  // value line. `draw(ctx, W, H, C, active)` paints and returns the handles as [x, y, key];
  // a press within 10 px of one starts a drag that `move(key, x, y, W, H)` turns into values.
  function displayBox(host, o, draw, move) {
    var col = controlColumn(host, o);
    col.classList.add("pr-disp");
    var box = el("div", "pr-disp__box", col);
    box.setAttribute("role", "img");
    if (o.label) box.setAttribute("aria-label", o.label);
    var cv = el("canvas", null, box), ctx = cv.getContext("2d");
    var handles = [], W = 0, H = 0, drag = null, over = null;
    function paint() {
      var r = box.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
      W = r.width; H = r.height;
      if (!W || !H) return;
      cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, W, H);
      var C = {
        line: cssVar(box, "--line"), strong: cssVar(box, "--line-strong"), viz: cssVar(box, "--viz-primary"),
        vizFill: cssVar(box, "--viz-primary-fill"), ground: cssVar(box, "--surface-000"), faint: cssVar(box, "--ink-faint")
      };
      handles = draw(ctx, W, H, C, drag ? drag.key : null) || [];
    }
    function pt(e) { var r = box.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; }
    // the nearest handle within 10 px (two can sit close: the glide's end and the pitch envelope's end)
    function hit(p) {
      var best = null, bd = 100;
      handles.forEach(function (h) { var dx = h[0] - p.x, dy = h[1] - p.y, d = dx * dx + dy * dy; if (d <= bd) { bd = d; best = h; } });
      return best;
    }
    box.addEventListener("pointerdown", function (e) {
      var h = hit(pt(e)); if (!h) return;
      drag = { key: h[2] }; box.setPointerCapture(e.pointerId); e.preventDefault();
      if (o.onGestureStart) o.onGestureStart(h[2]);
      paint();
    });
    box.addEventListener("pointermove", function (e) {
      if (!drag) {
        var h = hit(pt(e)), k = h ? h[2] : null;
        box.style.cursor = h ? "grab" : "";
        if (k !== over) { over = k; if (k && o.onHover) o.onHover(k); }
        return;
      }
      var p = pt(e); move(drag.key, p.x, p.y, W, H); paint();
    });
    function end() { if (!drag) return; var k = drag.key; drag = null; paint(); if (o.onGestureEnd) o.onGestureEnd(k); }
    box.addEventListener("pointerup", end);
    box.addEventListener("pointercancel", end);
    var ro = typeof ResizeObserver !== "undefined" ? new ResizeObserver(paint) : null;
    if (ro) ro.observe(box);
    requestAnimationFrame(paint);
    return { el: col, box: box, paint: paint, destroy: function () { if (ro) ro.disconnect(); col.remove(); } };
  }
  function handleDot(ctx, x, y, C, active) {
    ctx.beginPath(); ctx.arc(x, y, 6, 0, Math.PI * 2);
    ctx.fillStyle = active ? C.viz : C.ground; ctx.fill();
    ctx.lineWidth = 1.5; ctx.strokeStyle = C.viz; ctx.stroke();
  }
  function levelLine(ctx, y, W, color) {
    ctx.beginPath(); ctx.moveTo(0, Math.round(y) + 0.5); ctx.lineTo(W, Math.round(y) + 0.5);
    ctx.strokeStyle = color; ctx.lineWidth = 1; ctx.stroke();
  }
  var DISP_PAD = 8, DISP_MIN = 0.08; // the box's inner margin; the share of a segment's width at the knob's minimum

  // Envelope: an ADSR shape. a, d, r and vel are the knobs' positions on their scales (0..1), s the
  // sustain level on its scale (0..1). Each of A, D and R takes up to a quarter of the width, in
  // proportion to its knob's position; the sustain hold is a fifth. Not real time: a 1 ms attack
  // stays visible. Handles: the attack's peak (sideways: a), the decay's end (sideways: d, up and
  // down: s), the sustain's end (up and down: s), the release's end (sideways: r).
  // onChange(key, value) reports each value that moved.
  function Envelope(host, o) {
    var v = { a: o.a, d: o.d, s: o.s, r: o.r, vel: o.vel };
    var PAD = DISP_PAD, MIN = DISP_MIN;
    function seg(W) { return (W - 2 * PAD) / 4; }
    function share(n) { return MIN + (1 - MIN) * n; }
    function nOf(w, W) { return clamp((w / seg(W) - MIN) / (1 - MIN), 0, 1); }
    function geom(W, H) {
      var top = PAD + 2, base = H - PAD, x0 = PAD, x1 = x0 + seg(W) * share(v.a), x2 = x1 + seg(W) * share(v.d), x3 = x2 + (W - 2 * PAD) / 5, x4 = x3 + seg(W) * share(v.r);
      return { top: top, base: base, x0: x0, x1: x1, x2: x2, x3: x3, x4: x4, y: function (lv) { return base - (base - top) * lv; } };
    }
    var part = displayBox(host, o, function (ctx, W, H, C, active) {
      var g = geom(W, H);
      [0.25, 0.5, 0.75].forEach(function (f) { levelLine(ctx, g.base - (g.base - g.top) * f, W, C.line); });
      levelLine(ctx, g.base, W, C.strong);
      function curve(peak) {
        function y(lv) { return g.y(lv * peak); }
        ctx.beginPath(); ctx.moveTo(g.x0, g.base);
        ctx.quadraticCurveTo(g.x0 + (g.x1 - g.x0) * 0.35, y(1), g.x1, y(1));
        ctx.quadraticCurveTo(g.x1 + (g.x2 - g.x1) * 0.3, y(v.s), g.x2, y(v.s));
        ctx.lineTo(g.x3, y(v.s));
        ctx.quadraticCurveTo(g.x3 + (g.x4 - g.x3) * 0.3, g.base, g.x4, g.base);
        ctx.lineTo(W - PAD, g.base);
      }
      // the shape at the softest velocity, as a faint line under the full one
      if (v.vel > 0) { curve(1 - v.vel); ctx.lineWidth = 1; ctx.strokeStyle = C.faint; ctx.stroke(); }
      curve(1); ctx.closePath(); ctx.fillStyle = C.vizFill; ctx.fill();
      curve(1); ctx.lineWidth = 1.5; ctx.strokeStyle = C.viz; ctx.stroke();
      var hs = [[g.x1, g.y(1), "a"], [g.x2, g.y(v.s), "d"], [g.x3, g.y(v.s), "s"], [g.x4, g.base, "r"]];
      hs.forEach(function (h) { handleDot(ctx, h[0], h[1], C, h[2] === active); });
      return hs;
    }, function (key, x, y, W, H) {
      var g = geom(W, H), lv = clamp((g.base - y) / (g.base - g.top), 0, 1), was = Object.assign({}, v);
      if (key === "a") v.a = nOf(x - g.x0, W);
      else if (key === "d") { v.d = nOf(x - g.x1, W); v.s = lv; }
      else if (key === "s") v.s = lv;
      else if (key === "r") v.r = nOf(x - g.x3, W);
      // the keys that moved, with their new values, taken before any onChange (a listener may set() this display back)
      var ch = [];
      ["a", "d", "s", "r"].forEach(function (k) { if (v[k] !== was[k]) ch.push([k, v[k]]); });
      if (o.onChange) ch.forEach(function (c) { o.onChange(c[0], c[1]); });
    });
    return {
      el: part.el,
      set: function (p) { Object.assign(v, p); part.paint(); },
      values: function () { return Object.assign({}, v); },
      destroy: part.destroy
    };
  }

  // PitchPath: the pitch of one note over time, in semitones around the played pitch. The glide
  // starts an octave below (the previous note is drawn an octave down) and reaches the pitch over
  // `glide` (0..1 of the knob's scale, 0 when glide is off); the pitch envelope adds `amt` semitones
  // (up to `amtMax`) that fall to 0 over `time` (0..1). `tune` is in cents. Neither axis is real
  // time; widths follow the knobs. Handles: the start of the note (up and down: amt), the glide's
  // end (sideways: glide), the envelope's end (sideways: time; drawn while amt > 0).
  function PitchPath(host, o) {
    var v = { glide: o.glide, amt: o.amt, time: o.time, tune: o.tune }, amtMax = o.amtMax || 48;
    var PAD = DISP_PAD, MIN = DISP_MIN;
    function half(W) { return (W - 2 * PAD) / 2; }
    function nOf(w, W) { return clamp((w / half(W) - MIN) / (1 - MIN), 0, 1); }
    function geom(W, H) {
      var R = Math.max(12, v.amt) + 3, mid = H / 2, sc = (H / 2 - PAD) / R;
      var wg = v.glide > 0 ? half(W) * (MIN + (1 - MIN) * v.glide) : 0, we = v.amt > 0 ? half(W) * (MIN + (1 - MIN) * v.time) : 0;
      return {
        R: R, mid: mid, sc: sc, wg: wg, we: we,
        y: function (st) { return mid - st * sc; },
        f: function (x) {
          var t = x - PAD, st = v.tune / 100;
          if (wg && t < wg) st += -12 * (1 - t / wg);
          if (we && t < we) st += v.amt * (1 - t / we);
          return st;
        }
      };
    }
    var part = displayBox(host, o, function (ctx, W, H, C, active) {
      var g = geom(W, H), x;
      for (var st = -24; st <= 24; st += 12) if (st && Math.abs(st) < g.R) levelLine(ctx, g.y(st), W, C.line);
      levelLine(ctx, g.y(0), W, C.strong);
      ctx.beginPath(); ctx.moveTo(PAD, g.y(0));
      for (x = PAD; x <= W - PAD; x++) ctx.lineTo(x, g.y(g.f(x)));
      ctx.lineTo(W - PAD, g.y(0)); ctx.closePath(); ctx.fillStyle = C.vizFill; ctx.fill();
      ctx.beginPath();
      for (x = PAD; x <= W - PAD; x++) { var py = g.y(g.f(x)); if (x === PAD) ctx.moveTo(x, py); else ctx.lineTo(x, py); }
      ctx.lineWidth = 1.5; ctx.strokeStyle = C.viz; ctx.stroke();
      var hs = [[PAD, g.y(g.f(PAD)), "amt"]];
      if (g.wg) hs.push([PAD + g.wg, g.y(g.f(PAD + g.wg)), "glide"]);
      if (g.we) hs.push([PAD + g.we, g.y(g.f(PAD + g.we)), "time"]);
      hs.forEach(function (h) { handleDot(ctx, h[0], h[1], C, h[2] === active); });
      return hs;
    }, function (key, x, y, W, H) {
      var g = geom(W, H), was = v[key];
      if (key === "glide") v.glide = nOf(x - PAD, W);
      else if (key === "time") v.time = nOf(x - PAD, W);
      else if (key === "amt") { var st = (g.mid - y) / g.sc - v.tune / 100 - (g.wg ? -12 : 0); v.amt = clamp(Math.round(st), 0, amtMax); }
      if (o.onChange && v[key] !== was) o.onChange(key, v[key]);
    });
    return {
      el: part.el,
      set: function (p) { Object.assign(v, p); part.paint(); },
      values: function () { return Object.assign({}, v); },
      destroy: part.destroy
    };
  }

  // ---------- Panel (section) ----------
  function Panel(host, o) {
    o = o || {};
    var p = el("section", "pr-panel" + (o.tone === "warm" || o.tone === "cool" ? " pr-panel--" + o.tone : ""), host);
    if (o.title) { var h = el("h3", "pr-panel__title", p); h.textContent = o.title; p.setAttribute("aria-label", o.title); }
    var body = el("div", "pr-panel__body", p);
    if (o.grow) p.style.flex = String(o.grow);
    return { el: p, body: body };
  }

  // ---------- PluginWindow ----------
  function PluginWindow(host, o) {
    o = Object.assign({ width: 720, height: 420, presets: ["Default"], version: "" }, o);
    var w = el("div", "pr-window pr-root", host);
    w.style.width = o.width + "px";
    w.style.height = o.height + "px";
    var head = el("header", "pr-window__header", w);
    el("span", "pr-window__brand", head).textContent = "PRAYROOM";
    el("h2", "pr-window__title", head).textContent = o.title || "";
    el("div", "pr-window__spacer", head);
    var presetHost = el("div", "pr-window__preset", head);
    var preset = Segmented(presetHost, { options: o.presets, mode: "select", onChange: o.onPreset });
    var ab = Segmented(head, { options: ["A", "B"], value: "A", onChange: o.onAB });
    // the switch shows the plugin on; the host's parameter is the bypass, its opposite
    var power = Switch(head, { label: "Bypass", on: true, onChange: function (on) { if (o.onBypass) o.onBypass(!on); } });
    var bypass = {
      el: power.el,
      get pressed() { return !power.on; },
      set pressed(v) { power.on = !v; }
    };
    var body = el("main", "pr-window__body", w);
    var foot = el("footer", "pr-window__footer", w);
    var readout = el("span", "pr-window__readout", foot);
    readout.textContent = o.hint || "";
    el("span", "pr-window__version", foot).textContent = o.version;
    return {
      el: w, body: body, preset: preset, ab: ab, bypass: bypass,
      setReadout: function (text) { readout.textContent = text; },
      destroy: function () { w.remove(); }
    };
  }


  // ---------- Viewer (video preview on a neutral surround) ----------
  function Viewer(host, o) {
    o = Object.assign({ aspect: 16 / 9, info: [] }, o);
    var v = el("div", "pr-viewer", host);
    if (o.width) v.style.width = typeof o.width === "number" ? o.width + "px" : o.width;
    if (o.height) v.style.height = typeof o.height === "number" ? o.height + "px" : o.height;
    v.style.paddingBottom = "16px";
    var frame = el("div", "pr-viewer__frame", v);
    var info = el("div", "pr-viewer__info", v);
    function fit() {
      // 16px around the frame, plus a 32px band at the bottom for the info row
      var r = v.getBoundingClientRect();
      var w = Math.max(0, r.width - 32), h = Math.max(0, r.height - 16 - 32);
      if (w / h > o.aspect) w = h * o.aspect; else h = w / o.aspect;
      frame.style.width = Math.floor(w) + "px"; frame.style.height = Math.floor(h) + "px";
    }
    function setInfo(items) {
      info.textContent = "";
      (items || []).forEach(function (s) { el("span", null, info).textContent = s; });
    }
    setInfo(o.info);
    var ro = typeof ResizeObserver !== "undefined" ? new ResizeObserver(fit) : null;
    if (ro) ro.observe(v);
    fit();
    return { el: v, frame: frame, setInfo: setInfo, setAspect: function (a) { o.aspect = a; fit(); }, destroy: function () { if (ro) ro.disconnect(); v.remove(); } };
  }

  // ---------- Timeline (bars/beats ruler, video tracks, audio lane, playhead) ----------
  // ---------- Timeline, mix: a mixing app's tracks ----------
  // Tall rows, each a mixer head (the track's colour, its name, mute and solo, its volume on
  // a Fader laid on its side) beside one region of its waveform, under a ruler that holds
  // the loop. It fits its container's width and scrolls up and down; the ruler stays put.
  // Positions are bars from 1 (1.5 = the middle of the first bar), as in the video variant.
  function mixTimeline(host, o) {
    o = Object.assign({ bars: 16, firstBar: 1, beatsPerBar: 4, tracks: [], selected: 0, position: 1, loop: null }, o);
    var vol = Object.assign({ min: -24, max: 12, step: 0.5 }, o.volume);
    var root = el("div", "pr-tl pr-tl--mix", host);
    var grid = el("div", "pr-tl__grid", root);
    el("div", "pr-tl__corner", grid);
    var ruler = el("div", "pr-tl__ruler", grid);
    var pct = function (p) { return (p - 1) / o.bars * 100 + "%"; };
    var at = function (e) { var r = ruler.getBoundingClientRect(); return 1 + clamp((e.clientX - r.left) / r.width, 0, 1) * o.bars; };
    var onBeat = function (p) { return 1 + Math.round((p - 1) * o.beatsPerBar) / o.beatsPerBar; };
    for (var b = 0; b < o.bars; b++) {
      var no = el("span", "pr-tl__barno", ruler);
      no.style.left = pct(b + 1);
      no.textContent = String(o.firstBar + b);
    }

    // the playhead and the loop's lines over the lanes
    var layer = el("div", "pr-tl__layer", grid);
    var shade = el("div", "pr-tl__loopshade", layer);
    var head = el("div", "pr-tl__playhead", layer);
    var pos = o.position;
    function setPosition(p) { pos = clamp(p, 1, o.bars + 1); head.style.left = pct(pos); }
    function seek(p) { if (p === pos) return; setPosition(p); if (o.onSeek) o.onSeek(pos); }

    // the loop: drag its body to move it, an end to resize it, by whole bars; a press that
    // does not move seeks, on the loop as off it
    var loop = o.loop ? { start: o.loop.start, end: o.loop.end } : null;
    var loopEl = el("div", "pr-tl__loop", ruler);
    var drag = null;
    function renderLoop() {
      var on = !!loop && loop.end > loop.start;
      loopEl.hidden = shade.hidden = !on;
      if (!on) return;
      loopEl.style.left = shade.style.left = pct(loop.start);
      loopEl.style.width = shade.style.width = (loop.end - loop.start) / o.bars * 100 + "%";
    }
    function grabLoop(mode) {
      return function (e) {
        e.stopPropagation();
        e.currentTarget.setPointerCapture(e.pointerId);
        drag = { mode: mode, at: at(e), x: e.clientX, moved: false, loop: { start: loop.start, end: loop.end } };
      };
    }
    function moveLoop(e) {
      if (!drag) return;
      if (Math.abs(e.clientX - drag.x) >= 4) drag.moved = true;
      if (!drag.moved) return;
      var d = Math.round(at(e) - drag.at), s = drag.loop.start, en = drag.loop.end;
      if (drag.mode === "move") { var len = en - s; s = clamp(s + d, 1, o.bars + 1 - len); en = s + len; }
      else if (drag.mode === "start") s = clamp(s + d, 1, en - 1);
      else en = clamp(en + d, s + 1, o.bars + 1);
      if (s === loop.start && en === loop.end) return;
      loop = { start: s, end: en }; renderLoop();
      if (o.onLoop) o.onLoop({ start: s, end: en });
    }
    function dropLoop(e) { if (drag && !drag.moved) seek(onBeat(at(e))); drag = null; }
    // the ends' moves and releases come up to the body
    [[loopEl, "move"], [el("i", "pr-tl__loop-edge pr-tl__loop-edge--start", loopEl), "start"], [el("i", "pr-tl__loop-edge pr-tl__loop-edge--end", loopEl), "end"]].forEach(function (p) {
      p[0].addEventListener("pointerdown", grabLoop(p[1]));
    });
    loopEl.addEventListener("pointermove", moveLoop);
    loopEl.addEventListener("pointerup", dropLoop);
    // a press on the ruler off the loop seeks to the nearest beat, and the playhead follows a drag
    var scrub = false;
    ruler.addEventListener("pointerdown", function (e) { ruler.setPointerCapture(e.pointerId); scrub = true; seek(onBeat(at(e))); });
    ruler.addEventListener("pointermove", function (e) { if (scrub) seek(onBeat(at(e))); });
    ruler.addEventListener("pointerup", function () { scrub = false; });

    // the tracks
    var selected = o.selected, rows = [];
    function select(i) {
      if (i === selected) return;
      selected = i;
      rows.forEach(function (r, k) { r.head.classList.toggle("is-selected", k === i); r.region.classList.toggle("is-selected", k === i); });
      if (o.onSelectTrack) o.onSelectTrack(i);
    }
    // silent: while any track is soloed, every track without solo; otherwise the muted ones
    function renderSilent() {
      var solo = o.tracks.some(function (t) { return t.solo; });
      rows.forEach(function (r, k) { var t = o.tracks[k]; r.lane.classList.toggle("is-silent", solo ? !t.solo : !!t.mute); });
    }
    var keep = function (e) { e.stopPropagation(); }; // pressing M, S or the fader does not select the track
    o.tracks.forEach(function (t, i) {
      var color = t.color || trackColor(i, o.tracks.length);
      var hd = el("div", "pr-tl__thead" + (i === selected ? " is-selected" : ""), grid);
      hd.style.setProperty("--tc", color);
      hd.addEventListener("pointerdown", function () { select(i); });
      var tr = el("div", "pr-tl__trow", hd);
      el("span", "pr-tl__tname", tr).textContent = t.name;
      var m = Key(tr, { label: "M", name: t.name + " Mute", pressed: !!t.mute, onClick: function () { t.mute = !t.mute; m.pressed = t.mute; renderSilent(); if (o.onMute) o.onMute(i, t.mute); } });
      var s = Key(tr, { label: "S", name: t.name + " Solo", pressed: !!t.solo, onClick: function () { t.solo = !t.solo; s.pressed = t.solo; renderSilent(); if (o.onSolo) o.onSolo(i, t.solo); } });
      [m, s].forEach(function (k) { k.el.classList.add("pr-tl__ms"); k.el.addEventListener("pointerdown", keep); });
      var f = Fader(hd, { orientation: "horizontal", label: t.name + " Volume", min: vol.min, max: vol.max, step: vol.step, value: t.volume || 0, default: 0, mark: 0, unit: "dB", signed: true, onChange: function (v) { t.volume = v; if (o.onVolume) o.onVolume(i, v); } });
      f.el.addEventListener("pointerdown", keep);
      var lane = el("div", "pr-tl__lane", grid);
      lane.addEventListener("pointerdown", function () { select(i); });
      for (var b = 0; b < o.bars; b++) el("i", "pr-tl__gridline", lane).style.left = pct(b + 1);
      var region = el("div", "pr-tl__region" + (i === selected ? " is-selected" : ""), lane);
      region.style.setProperty("--tc", color);
      var cv = el("canvas", null, region);
      rows.push({ head: hd, lane: lane, region: region, draw: function () {
        var w = region.clientWidth, h = region.clientHeight, dpr = window.devicePixelRatio || 1;
        if (!w || !h) return;
        cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
        var g = cv.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
        g.fillStyle = color;
        var pk = t.peaks || [];
        for (var x = 0; x < w; x++) {
          var a = (pk.length ? pk[Math.floor(x / w * pk.length)] : 0) * (h / 2 - 2);
          g.fillRect(x, h / 2 - a, 1, Math.max(1, a * 2));
        }
      } });
    });
    // the layer last, so the lines are drawn over the lanes
    grid.appendChild(layer);
    renderSilent(); renderLoop(); setPosition(pos);
    var drawAll = function () { rows.forEach(function (r) { r.draw(); }); };
    var ro = typeof ResizeObserver !== "undefined" ? new ResizeObserver(drawAll) : null;
    if (ro) ro.observe(grid);
    drawAll();
    return {
      el: root,
      get position() { return pos; },
      setPosition: setPosition,
      get selected() { return selected; },
      select: select,
      setLoop: function (l) { loop = l ? { start: l.start, end: l.end } : null; renderLoop(); },
      destroy: function () { if (ro) ro.disconnect(); root.remove(); }
    };
  }

  function Timeline(host, o) {
    if (o && o.variant === "mix") return mixTimeline(host, o);
    o = Object.assign({ bars: 32, beatsPerBar: 4, pxPerBar: 64, tracks: [], audio: null, position: 1, label: "BPM" }, o);
    var root = el("div", "pr-tl", host);
    var heads = el("div", "pr-tl__heads", root);
    el("div", "pr-tl__corner", heads).textContent = o.label;
    var scroll = el("div", "pr-tl__scroll", root);
    var cv = el("div", "pr-tl__canvas", scroll);
    var width = o.bars * o.pxPerBar;
    cv.style.width = width + "px";
    var ruler = el("div", "pr-tl__ruler", cv);
    var step = o.pxPerBar >= 48 ? 1 : o.pxPerBar >= 24 ? 2 : 4;
    for (var b = 0; b < o.bars; b++) {
      if (b % step === 0) {
        var bar = el("div", "pr-tl__bar", ruler);
        bar.style.left = b * o.pxPerBar + "px";
        bar.textContent = String(b + 1);
      }
      for (var k = 1; k < o.beatsPerBar && o.pxPerBar / o.beatsPerBar >= 8; k++) {
        el("div", "pr-tl__beat", ruler).style.left = (b + k / o.beatsPerBar) * o.pxPerBar + "px";
      }
    }
    var x = function (barPos) { return (barPos - 1) * o.pxPerBar; };
    var selected = null;
    o.tracks.forEach(function (t, ti) {
      var hd = el("div", "pr-tl__head", heads);
      el("span", null, hd).textContent = t.name;
      if (t.tag) el("span", "pr-tl__tag", hd).textContent = t.tag;
      var tr = el("div", "pr-tl__track", cv);
      (t.clips || []).forEach(function (c) {
        var lane = (c.lane || t.tag || "A").toLowerCase() === "b" ? "b" : "a";
        var cl = el("div", "pr-tl__clip pr-tl__clip--" + lane, tr);
        cl.style.left = x(c.start) + "px";
        cl.style.width = Math.max(4, (c.end - c.start) * o.pxPerBar - 1) + "px";
        cl.textContent = c.label + (c.opacity != null && c.opacity < 100 ? "  " + c.opacity + "%" : "");
        cl.title = cl.textContent;
        cl.addEventListener("pointerdown", function (e) {
          e.stopPropagation();
          if (selected) selected.classList.remove("is-selected");
          selected = cl; cl.classList.add("is-selected");
          if (o.onSelect) o.onSelect(ti, c);
        });
      });
    });
    if (o.audio) {
      var ah = el("div", "pr-tl__head", heads);
      el("span", null, ah).textContent = o.audio.name || "Audio";
      var at = el("div", "pr-tl__track", cv);
      var wc = el("canvas", "pr-tl__wave", at);
      var drawWave = function () {
        var dpr = window.devicePixelRatio || 1, h = at.clientHeight || 24;
        wc.width = width * dpr; wc.height = h * dpr; wc.style.width = width + "px"; wc.style.height = h + "px";
        var g = wc.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, width, h);
        g.fillStyle = cssVar(root, "--meter-ok");
        var peaks = o.audio.peaks || [];
        for (var i = 0; i < width; i++) {
          var p = peaks.length ? peaks[Math.floor(i / width * peaks.length)] : 0;
          var a = Math.max(1, p * (h / 2 - 3));
          g.fillRect(i, h / 2 - a, 1, a * 2);
        }
      };
      drawWave();
      var mo = new MutationObserver(function () { requestAnimationFrame(drawWave); });
      mo.observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
    }
    var head = el("div", "pr-tl__playhead", cv);
    var pos = o.position;
    function setPosition(p) { pos = Math.max(1, Math.min(o.bars + 1, p)); head.style.left = x(pos) + "px"; }
    function seekFrom(e) {
      var r = cv.getBoundingClientRect();
      setPosition(1 + (e.clientX - r.left) / o.pxPerBar);
      if (o.onSeek) o.onSeek(pos);
    }
    var dragging = false;
    scroll.addEventListener("pointerdown", function (e) { dragging = true; scroll.setPointerCapture(e.pointerId); seekFrom(e); });
    scroll.addEventListener("pointermove", function (e) { if (dragging) seekFrom(e); });
    scroll.addEventListener("pointerup", function () { dragging = false; });
    setPosition(pos);
    return {
      el: root,
      get position() { return pos; },
      setPosition: setPosition,
      destroy: function () { root.remove(); }
    };
  }

  var api = { Knob: Knob, Dial: Dial, Drum: Drum, Tape: Tape, Encoder: Encoder, Fader: Fader, trackColor: trackColor, Toggle: Toggle, Key: Key, Badge: Badge, Tag: Tag, Bracket: Bracket, Device: Device, Scrim: Scrim, Lamp: Lamp, Switch: Switch, Hint: Hint, Segmented: Segmented, VSlide: VSlide, Stepper: Stepper, OverviewTabs: OverviewTabs, Meter: Meter, Graph: Graph, freqColors: freqColors, Envelope: Envelope, PitchPath: PitchPath, Panel: Panel, PluginWindow: PluginWindow, Viewer: Viewer, Timeline: Timeline };
  window.PRAYROOM = Object.assign(window.PRAYROOM || {}, api);
})();
