// Small line chart on a canvas: a few series over the same samples, scaled to fit.

function cssVar(name, fallback) {
  const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  return v || fallback;
}

/**
 * series: [{ values: number[], color: '--css-var' }]
 * opts:   { min, max }  fixed range, or omitted to fit the data with a little headroom
 */
export function drawChart(canvas, series, opts = {}) {
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth, h = canvas.clientHeight || Number(canvas.getAttribute('height'));
  if (!w || !h) return;
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);

  const all = series.flatMap((s) => s.values).filter((v) => Number.isFinite(v));
  let lo = opts.min, hi = opts.max;
  if (lo === undefined || hi === undefined) {
    lo = all.length ? Math.min(...all) : 0;
    hi = all.length ? Math.max(...all) : 1;
    const padY = Math.max((hi - lo) * 0.15, opts.minSpan ? opts.minSpan / 2 : 0.01);
    lo -= padY; hi += padY;
  }

  // grid: three quiet horizontal lines
  ctx.strokeStyle = cssVar('--line-soft', '#E7ECE8');
  ctx.lineWidth = 1;
  ctx.beginPath();
  for (let i = 1; i <= 3; i++) {
    const y = Math.round((h * i) / 4) + 0.5;
    ctx.moveTo(0, y); ctx.lineTo(w, y);
  }
  ctx.stroke();

  // range labels
  ctx.fillStyle = cssVar('--muted', '#56655C');
  ctx.font = '11px ' + cssVar('--font-mono', 'monospace');
  ctx.textBaseline = 'top';
  ctx.fillText(hi.toFixed(2) + (opts.unit || ''), 2, 2);
  ctx.textBaseline = 'bottom';
  ctx.fillText(lo.toFixed(2) + (opts.unit || ''), 2, h - 2);

  const n = Math.max(opts.capacity || 0, ...series.map((s) => s.values.length), 2);
  for (const s of series) {
    if (s.values.length < 2) continue;
    ctx.strokeStyle = cssVar(s.color, '#1B7F47');
    ctx.lineWidth = 2;
    ctx.lineJoin = 'round';
    ctx.beginPath();
    s.values.forEach((v, i) => {
      const x = ((i + (n - s.values.length)) / (n - 1)) * w;
      const y = h - ((v - lo) / (hi - lo)) * h;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }
}
