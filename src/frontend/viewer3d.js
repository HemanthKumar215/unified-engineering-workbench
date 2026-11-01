// Dependency-free 3D viewer on a 2D canvas: orthographic projection, painter's-algorithm
// triangle sorting with flat shading, coloured point cloud, orbit/pan/zoom, node picking.
"use strict";

const Viewer3D = (() => {
  // Blue -> cyan -> green -> yellow -> red colour map for temperature.
  const STOPS = [[0, [49, 54, 149]], [0.25, [69, 160, 220]], [0.5, [120, 198, 121]], [0.75, [253, 200, 60]], [1, [215, 48, 39]]];
  function colormap(t) {
    t = Math.min(1, Math.max(0, t));
    for (let i = 1; i < STOPS.length; i++) {
      if (t <= STOPS[i][0]) {
        const [t0, c0] = STOPS[i - 1], [t1, c1] = STOPS[i];
        const u = (t - t0) / (t1 - t0);
        return c0.map((c, k) => Math.round(c + u * (c1[k] - c)));
      }
    }
    return STOPS[STOPS.length - 1][1];
  }

  class Viewer {
    constructor(canvas, onPick) {
      this.canvas = canvas;
      this.onPick = onPick;
      this.mesh = null;     // {verts: Float64Array, scale}
      this.cloud = null;    // {xyz, temp, ids, tMin, tMax}
      this.show = { mesh: true, points: true, wire: false };
      this.yaw = -0.7; this.pitch = 0.45; this.zoom = 1; this.pan = [0, 0];
      this.center = [0, 0, 0]; this.radius = 1;
      this.projected = null;  // screen positions of points, for picking
      this._bind();
      new ResizeObserver(() => this.render()).observe(canvas);
    }

    setMesh(verts, unitScale = 1) {
      this.mesh = verts ? { verts: Float64Array.from(verts, v => v * unitScale) } : null;
      this.fit();
    }
    setCloud(cloud) { this.cloud = cloud; this.fit(); }

    bounds() {
      const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
      const add = arr => { for (let i = 0; i < arr.length; i += 3) for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], arr[i + k]); mx[k] = Math.max(mx[k], arr[i + k]); } };
      if (this.mesh && this.show.mesh) add(this.mesh.verts);
      if (this.cloud && this.show.points) add(this.cloud.xyz);
      if (!isFinite(mn[0])) return null;
      return { mn, mx };
    }

    fit() {
      const b = this.bounds();
      if (b) {
        this.center = [0, 1, 2].map(k => (b.mn[k] + b.mx[k]) / 2);
        this.radius = Math.max(1e-12, Math.hypot(b.mx[0] - b.mn[0], b.mx[1] - b.mn[1], b.mx[2] - b.mn[2]) / 2);
      }
      this.zoom = 1; this.pan = [0, 0];
      this.render();
    }

    _rot() {
      const cy = Math.cos(this.yaw), sy = Math.sin(this.yaw), cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
      // Rotate about the world Z axis (yaw), then tilt about screen X (pitch). Z is "up".
      return [[cy, -sy, 0], [sy * sp, cy * sp, cp], [-sy * cp, -cy * cp, sp]];
    }

    render() {
      const c = this.canvas, dpr = window.devicePixelRatio || 1;
      const W = c.clientWidth, H = c.clientHeight;
      if (!W || !H) return;
      c.width = W * dpr; c.height = H * dpr;
      const g = c.getContext("2d");
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, W, H);

      const R = this._rot();
      const s = this.zoom * 0.45 * Math.min(W, H) / this.radius;
      const cx = W / 2 + this.pan[0], cy = H / 2 + this.pan[1];
      const [ox, oy, oz] = this.center;
      const proj = (x, y, z) => {
        x -= ox; y -= oy; z -= oz;
        return [cx + s * (R[0][0] * x + R[0][1] * y + R[0][2] * z),
                cy - s * (R[1][0] * x + R[1][1] * y + R[1][2] * z),
                R[2][0] * x + R[2][1] * y + R[2][2] * z];  // depth: larger = farther
      };

      if (this.mesh && this.show.mesh) {
        const v = this.mesh.verts, n = v.length / 9;
        const tris = new Array(n);
        for (let t = 0; t < n; t++) {
          const o = 9 * t;
          const a = proj(v[o], v[o + 1], v[o + 2]), b = proj(v[o + 3], v[o + 4], v[o + 5]), d = proj(v[o + 6], v[o + 7], v[o + 8]);
          // Face normal in view space for two-sided Lambert shading.
          const ux = v[o + 3] - v[o], uy = v[o + 4] - v[o + 1], uz = v[o + 5] - v[o + 2];
          const wx = v[o + 6] - v[o], wy = v[o + 7] - v[o + 1], wz = v[o + 8] - v[o + 2];
          let nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
          const len = Math.hypot(nx, ny, nz) || 1;
          const vz = (R[2][0] * nx + R[2][1] * ny + R[2][2] * nz) / len;
          tris[t] = { a, b, d, depth: (a[2] + b[2] + d[2]) / 3, shade: 0.35 + 0.65 * Math.abs(vz) };
        }
        tris.sort((p, q) => q.depth - p.depth);
        g.lineJoin = "round";
        for (const t of tris) {
          const k = Math.round(120 + 110 * t.shade);
          g.fillStyle = `rgb(${k - 25},${k - 10},${k})`;
          g.beginPath(); g.moveTo(t.a[0], t.a[1]); g.lineTo(t.b[0], t.b[1]); g.lineTo(t.d[0], t.d[1]); g.closePath();
          g.fill();
          g.strokeStyle = this.show.wire ? "rgba(30,41,59,.55)" : g.fillStyle;  // stroke hides hairline seams
          g.lineWidth = this.show.wire ? 0.8 : 0.6;
          g.stroke();
        }
      }

      this.projected = null;
      if (this.cloud && this.show.points) {
        const { xyz, temp, tMin, tMax } = this.cloud, n = temp.length;
        const span = tMax - tMin || 1;
        const pts = new Array(n);
        for (let i = 0; i < n; i++) pts[i] = proj(xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]).concat(i);
        const order = pts.slice().sort((p, q) => q[2] - p[2]);
        const size = n > 50000 ? 2 : n > 5000 ? 3 : 5;
        for (const p of order) {
          const [r, gg, b] = colormap((temp[p[3]] - tMin) / span);
          g.fillStyle = `rgb(${r},${gg},${b})`;
          g.fillRect(p[0] - size / 2, p[1] - size / 2, size, size);
        }
        this.projected = pts;
      }

      this._axes(g, R, H);
    }

    _axes(g, R, H) {
      const o = [46, H - 46], L = 28;
      [["x", "#dc2626", 0], ["y", "#16a34a", 1], ["z", "#2563eb", 2]].forEach(([name, col, k]) => {
        const ex = R[0][k] * L, ey = -R[1][k] * L;
        g.strokeStyle = g.fillStyle = col; g.lineWidth = 2;
        g.beginPath(); g.moveTo(o[0], o[1]); g.lineTo(o[0] + ex, o[1] + ey); g.stroke();
        g.font = "12px Segoe UI"; g.fillText(name, o[0] + ex * 1.25 - 3, o[1] + ey * 1.25 + 4);
      });
    }

    pick(mx, my) {
      if (!this.projected) return null;
      let best = -1, bestD = 64;  // 8 px radius
      for (const p of this.projected) {
        const d = (p[0] - mx) ** 2 + (p[1] - my) ** 2;
        if (d < bestD || (d === bestD && best >= 0 && p[2] < this.projected[best][2])) { bestD = d; best = p[3]; }
      }
      return best >= 0 ? best : null;
    }

    _bind() {
      const c = this.canvas;
      let drag = null;
      c.addEventListener("contextmenu", e => e.preventDefault());
      c.addEventListener("pointerdown", e => {
        drag = { x: e.clientX, y: e.clientY, moved: false, pan: e.shiftKey || e.button === 2 };
        c.setPointerCapture(e.pointerId);
      });
      c.addEventListener("pointermove", e => {
        if (!drag) return;
        const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
        if (Math.abs(dx) + Math.abs(dy) > 2) drag.moved = true;
        drag.x = e.clientX; drag.y = e.clientY;
        if (drag.pan) { this.pan[0] += dx; this.pan[1] += dy; }
        else { this.yaw += dx * 0.01; this.pitch = Math.max(-1.55, Math.min(1.55, this.pitch + dy * 0.01)); }
        this._schedule();
      });
      c.addEventListener("pointerup", e => {
        if (drag && !drag.moved && this.onPick) {
          const r = c.getBoundingClientRect();
          this.onPick(this.pick(e.clientX - r.left, e.clientY - r.top), e.clientX - r.left, e.clientY - r.top);
        }
        drag = null;
      });
      c.addEventListener("wheel", e => {
        e.preventDefault();
        this.zoom = Math.max(0.05, Math.min(200, this.zoom * Math.exp(-e.deltaY * 0.0015)));
        this._schedule();
      }, { passive: false });
    }

    _schedule() {
      if (this._raf) return;
      this._raf = requestAnimationFrame(() => { this._raf = 0; this.render(); });
    }
  }

  return { Viewer, colormap };
})();
