// Small canvas line chart: axes with units, nice ticks, optional log scales, legend.
"use strict";

const Plot = (() => {
  const COLORS = ["#dc2626", "#ea580c", "#16a34a", "#2563eb", "#7c3aed", "#0891b2", "#be185d", "#4b5563"];

  function niceTicks(lo, hi, count = 6) {
    const span = hi - lo || 1;
    const raw = span / count;
    const mag = Math.pow(10, Math.floor(Math.log10(raw)));
    const step = [1, 2, 5, 10].map(m => m * mag).find(s => s >= raw);
    const ticks = [];
    for (let v = Math.ceil(lo / step) * step; v <= hi + 1e-9 * span; v += step) ticks.push(Math.abs(v) < step * 1e-9 ? 0 : v);
    return ticks;
  }

  const fmt = v => {
    const a = Math.abs(v);
    return (a !== 0 && (a < 1e-3 || a >= 1e5)) ? v.toExponential(1) : +v.toPrecision(4) + "";
  };

  // series: [{label, x[], y[], color?, markers?, dashed?}]
  function draw(canvas, spec, series) {
    const dpr = window.devicePixelRatio || 1;
    const W = canvas.clientWidth, H = canvas.clientHeight;
    canvas.width = W * dpr;
    canvas.height = H * dpr;
    const g = canvas.getContext("2d");
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, W, H);
    g.fillStyle = "#fff";
    g.fillRect(0, 0, W, H);

    const ml = 70, mr = 170, mt = 34, mb = 48;
    const pw = W - ml - mr, ph = H - mt - mb;
    if (pw < 40 || ph < 40) return;
    const tx = v => spec.logX ? Math.log10(v) : v;
    const ty = v => spec.logY ? Math.log10(v) : v;

    let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
    for (const s of series) for (let i = 0; i < s.x.length; i++) {
      if ((spec.logX && s.x[i] <= 0) || (spec.logY && s.y[i] <= 0) || s.y[i] == null) continue;
      x0 = Math.min(x0, tx(s.x[i])); x1 = Math.max(x1, tx(s.x[i]));
      y0 = Math.min(y0, ty(s.y[i])); y1 = Math.max(y1, ty(s.y[i]));
    }
    if (!isFinite(x0)) { x0 = 0; x1 = 1; y0 = 0; y1 = 1; }
    if (x1 === x0) x1 = x0 + 1;
    if (y1 === y0) { y0 -= 0.5; y1 += 0.5; }
    if (spec.logX) { x0 = Math.floor(x0); x1 = Math.ceil(x1); }
    if (spec.logY) { y0 = Math.floor(y0); y1 = Math.ceil(y1); } else { const p = 0.05 * (y1 - y0); y0 -= p; y1 += p; }
    if (spec.yMin !== undefined) y0 = spec.yMin;

    const px = v => ml + (tx(v) - x0) / (x1 - x0) * pw;
    const py = v => mt + ph - (ty(v) - y0) / (y1 - y0) * ph;

    g.font = "12px Segoe UI, sans-serif";
    g.fillStyle = "#111827";
    g.textAlign = "center";
    g.font = "600 14px Segoe UI, sans-serif";
    g.fillText(spec.title || "", ml + pw / 2, 20);
    g.font = "12px Segoe UI, sans-serif";

    const ticks = (lo, hi, log) => log ? Array.from({ length: Math.round(hi - lo) + 1 }, (_, i) => lo + i) : niceTicks(lo, hi);
    g.strokeStyle = "#e5e7eb";
    g.lineWidth = 1;
    for (const t of ticks(x0, x1, spec.logX)) {
      const X = ml + (t - x0) / (x1 - x0) * pw;
      g.beginPath(); g.moveTo(X, mt); g.lineTo(X, mt + ph); g.stroke();
      g.fillStyle = "#374151";
      g.fillText(fmt(spec.logX ? Math.pow(10, t) : t), X, mt + ph + 16);
    }
    g.textAlign = "right";
    for (const t of ticks(y0, y1, spec.logY)) {
      const Y = mt + ph - (t - y0) / (y1 - y0) * ph;
      g.beginPath(); g.moveTo(ml, Y); g.lineTo(ml + pw, Y); g.stroke();
      g.fillText(fmt(spec.logY ? Math.pow(10, t) : t), ml - 6, Y + 4);
    }
    g.strokeStyle = "#374151";
    g.strokeRect(ml, mt, pw, ph);
    g.textAlign = "center";
    g.fillStyle = "#111827";
    g.fillText(spec.xLabel || "", ml + pw / 2, H - 10);
    g.save();
    g.translate(16, mt + ph / 2);
    g.rotate(-Math.PI / 2);
    g.fillText(spec.yLabel || "", 0, 0);
    g.restore();

    g.save();
    g.beginPath(); g.rect(ml, mt, pw, ph); g.clip();
    series.forEach((s, si) => {
      const color = s.color || COLORS[si % COLORS.length];
      g.strokeStyle = g.fillStyle = color;
      g.lineWidth = 1.8;
      g.setLineDash(s.dashed ? [6, 4] : []);
      if (s.markers) {
        for (let i = 0; i < s.x.length; i++) {
          if ((spec.logY && s.y[i] <= 0) || s.y[i] == null) continue;
          g.beginPath(); g.arc(px(s.x[i]), py(s.y[i]), 3.2, 0, 2 * Math.PI); g.fill();
        }
      }
      if (!s.markers || s.line) {
        g.beginPath();
        let started = false;
        for (let i = 0; i < s.x.length; i++) {
          if ((spec.logY && s.y[i] <= 0) || s.y[i] == null) continue;
          const X = px(s.x[i]), Y = py(s.y[i]);
          started ? g.lineTo(X, Y) : g.moveTo(X, Y);
          started = true;
        }
        g.stroke();
      }
    });
    g.restore();

    g.setLineDash([]);
    g.textAlign = "left";
    series.forEach((s, si) => {
      const color = s.color || COLORS[si % COLORS.length];
      const lx = ml + pw + 12, ly = mt + 10 + si * 18;
      g.strokeStyle = g.fillStyle = color;
      g.setLineDash(s.dashed ? [6, 4] : []);
      if (s.markers && !s.line) { g.beginPath(); g.arc(lx + 11, ly, 3.2, 0, 2 * Math.PI); g.fill(); }
      else { g.beginPath(); g.moveTo(lx, ly); g.lineTo(lx + 22, ly); g.stroke(); }
      g.setLineDash([]);
      g.fillStyle = "#111827";
      g.fillText(s.label, lx + 28, ly + 4);
    });
  }

  return { draw, COLORS };
})();
