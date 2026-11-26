// The pet as a front-view SVG puppet. Angles are servo degrees (0..180):
//   neck α 90 = looking straight, neck β 90 = level
//   shoulder 0 = arm hanging down, 90 = out to the side, 180 = straight up
//   elbow 90 = straight, more = forearm bends further up
// Dragging a blue handle or the head reports new angles through onJoint(key, degrees).

const NS = 'http://www.w3.org/2000/svg';
const L1 = 80, L2 = 76;                        // upper arm and forearm length in drawing units
const ARMS = {
  l: { shoulder: [276, 330], side: -1, shoulderKey: 'lShoulder', elbowKey: 'lElbow' },
  r: { shoulder: [444, 330], side: 1, shoulderKey: 'rShoulder', elbowKey: 'rElbow' },
};
const LABELS = {
  neckB:     { text: '脖子 β', at: [562, 249], anchor: 'start' },
  neckA:     { text: '脖子 α', at: [562, 295], anchor: 'start' },
  rShoulder: { text: '右肩', at: [600, 345], anchor: 'start' },
  rElbow:    { text: '右肘', at: [600, 405], anchor: 'start' },
  lShoulder: { text: '左肩', at: [120, 345], anchor: 'end' },
  lElbow:    { text: '左肘', at: [120, 405], anchor: 'end' },
};

export const FACES = {
  happy:     { name: '开心', eyes: '<rect x="-32" y="-20" width="18" height="38" rx="9"/><rect x="14" y="-20" width="18" height="38" rx="9"/>' },
  wink:      { name: '眨眼', eyes: '<rect x="-32" y="-20" width="18" height="38" rx="9"/><path d="M12 0h24" fill="none" stroke-width="9" stroke-linecap="round"/>' },
  sleepy:    { name: '困', eyes: '<path d="M-36 4h24M12 4h24" fill="none" stroke-width="9" stroke-linecap="round"/>' },
  surprised: { name: '惊讶', eyes: '<circle cx="-23" cy="-2" r="14" fill="none" stroke-width="8"/><circle cx="23" cy="-2" r="14" fill="none" stroke-width="8"/>' },
  angry:     { name: '生气', eyes: '<path d="M-40 -22l26 16M40 -22l-26 16" fill="none" stroke-width="9" stroke-linecap="round"/><rect x="-30" y="2" width="14" height="20" rx="7"/><rect x="16" y="2" width="14" height="20" rx="7"/>' },
  left:      { name: '看左边', eyes: '<rect x="-46" y="-20" width="18" height="38" rx="9"/><rect x="0" y="-20" width="18" height="38" rx="9"/>' },
};

/** Eyes of one face as a small standalone SVG (for the face picker buttons). */
export function faceIcon(key) {
  return '<svg aria-hidden="true" viewBox="-70 -70 140 140"><circle r="66" fill="var(--ink)"/>'
    + '<g fill="var(--eye)" stroke="var(--eye)" stroke-width="0">' + FACES[key].eyes + '</g></svg>';
}

const rad = (deg) => (deg * Math.PI) / 180;
const deg = (r) => (r * 180) / Math.PI;
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

function armPoints(arm, shoulderDeg, elbowDeg) {
  const dir = (a) => [arm.side * Math.sin(rad(a)), Math.cos(rad(a))];
  const [sx, sy] = arm.shoulder;
  const d1 = dir(shoulderDeg);
  const elbow = [sx + L1 * d1[0], sy + L1 * d1[1]];
  const d2 = dir(shoulderDeg + (elbowDeg - 90));
  const hand = [elbow[0] + L2 * d2[0], elbow[1] + L2 * d2[1]];
  return { elbow, hand };
}

export class Pet {
  constructor(container, { onJoint, onGrab } = {}) {
    this.onJoint = onJoint || (() => {});
    this.onGrab = onGrab || (() => {});
    this.pose = { neckA: 90, neckB: 90, lShoulder: 25, lElbow: 90, rShoulder: 25, rElbow: 90 };
    this.face = 'happy';
    container.innerHTML = this._markup();
    this.svg = container.querySelector('svg');
    this.el = {};
    for (const node of this.svg.querySelectorAll('[data-id]')) this.el[node.dataset.id] = node;
    this._bindDrag();
    this.render();
  }

  _markup() {
    const label = (key) => {
      const l = LABELS[key];
      return '<line data-id="lead-' + key + '" stroke="#8A988F" stroke-width="1.2"/>'
        + '<text data-id="label-' + key + '" x="' + l.at[0] + '" y="' + l.at[1] + '" text-anchor="' + l.anchor + '" font-size="14" fill="var(--ink)"></text>';
    };
    const handle = (id) => '<g class="handle" data-id="' + id + '" data-handle="' + id + '" tabindex="-1">'
      + '<circle r="20" fill="transparent"/><circle class="ring" r="16" fill="none" stroke="var(--accent)" stroke-width="3" visibility="hidden"/>'
      + '<circle class="knob" r="9" fill="var(--blue)" stroke="#FFFFFF" stroke-width="3"/></g>';
    return '<svg xmlns="' + NS + '" viewBox="0 0 720 560" role="img" aria-label="Kero 机器人模型：头上是圆形屏幕和摄像头，脖子两个关节，左右手臂各两个关节，底盘两个轮子">'
      + '<ellipse cx="360" cy="532" rx="190" ry="14" fill="#E3E9E4"/>'
      + '<rect x="190" y="450" width="46" height="84" rx="18" fill="var(--ink)"/>'
      + '<rect x="484" y="450" width="46" height="84" rx="18" fill="var(--ink)"/>'
      + '<rect x="228" y="442" width="264" height="76" rx="22" fill="#FFFFFF" stroke="var(--ink)" stroke-width="3"/>'
      + '<rect x="276" y="300" width="168" height="150" rx="28" fill="#FFFFFF" stroke="var(--ink)" stroke-width="3"/>'
      + '<path d="M336 396H384M336 408H384M336 420H384" stroke="var(--ink)" stroke-width="3" stroke-linecap="round" fill="none"/>'
      + '<rect x="342" y="256" width="36" height="50" fill="#FFFFFF" stroke="var(--ink)" stroke-width="3"/>'
      + '<g class="head" data-id="head" data-handle="head">'
      +   '<rect x="232" y="96" width="256" height="172" rx="44" fill="#FFFFFF" stroke="var(--ink)" stroke-width="3"/>'
      +   '<circle cx="360" cy="108" r="6" fill="var(--ink)"/>'
      +   '<circle cx="454" cy="240" r="3.5" fill="var(--ink)"/>'
      +   '<circle cx="360" cy="184" r="66" fill="var(--ink)"/>'
      +   '<g data-id="eyes" fill="var(--eye)" stroke="var(--eye)" stroke-width="0"></g>'
      +   '<path d="M366 100L404 70M300 150L226 96" stroke="#8A988F" stroke-width="1.2" fill="none"/>'
      +   '<text x="410" y="70" font-size="14" fill="var(--muted)">摄像头</text>'
      +   '<text x="220" y="94" text-anchor="end" font-size="14" fill="var(--muted)">屏幕（脸）</text>'
      + '</g>'
      + '<path data-id="arm-l-out" fill="none" stroke="var(--ink)" stroke-width="22" stroke-linecap="round" stroke-linejoin="round"/>'
      + '<path data-id="arm-l-in" fill="none" stroke="#FFFFFF" stroke-width="16" stroke-linecap="round" stroke-linejoin="round"/>'
      + '<path data-id="arm-r-out" fill="none" stroke="var(--ink)" stroke-width="22" stroke-linecap="round" stroke-linejoin="round"/>'
      + '<path data-id="arm-r-in" fill="none" stroke="#FFFFFF" stroke-width="16" stroke-linecap="round" stroke-linejoin="round"/>'
      + Object.keys(LABELS).map(label).join('')
      + '<text x="176" y="498" text-anchor="end" font-size="14" fill="var(--muted)">左轮</text>'
      + '<text data-id="wheel-l" x="176" y="516" text-anchor="end" font-size="13" fill="var(--muted)" font-family="var(--font-mono)"></text>'
      + '<text x="544" y="498" font-size="14" fill="var(--muted)">右轮</text>'
      + '<text data-id="wheel-r" x="544" y="516" font-size="13" fill="var(--muted)" font-family="var(--font-mono)"></text>'
      + handle('lElbow') + handle('lHand') + handle('rElbow') + handle('rHand')
      + '</svg>';
  }

  setPose(pose) {
    Object.assign(this.pose, pose);
    this.render();
  }

  setFace(key) {
    if (FACES[key]) { this.face = key; this.render(); }
  }

  setWheels(a, b) {
    const fmt = (v) => (v > 0 ? '+' : '') + v + ' %';
    this.el['wheel-l'].textContent = a ? fmt(a) : '';
    this.el['wheel-r'].textContent = b ? fmt(b) : '';
  }

  /** Close the eyes for a moment. */
  blink() {
    const eyes = this.el.eyes;
    const t = eyes.getAttribute('transform');
    eyes.setAttribute('transform', t + ' scale(1 0.12)');
    setTimeout(() => this.render(), 130);
  }

  render() {
    const p = this.pose;
    // Head: turning left/right slides it sideways, with the face sliding a bit further (parallax).
    const hx = (p.neckA - 90) * 0.45, hy = -(p.neckB - 90) * 0.5;
    this.el.head.setAttribute('transform', 'translate(' + hx.toFixed(1) + ' ' + hy.toFixed(1) + ')');
    const fx = 360 + (p.neckA - 90) * 0.4, fy = 184 - (p.neckB - 90) * 0.35;
    this.el.eyes.setAttribute('transform', 'translate(' + fx.toFixed(1) + ' ' + fy.toFixed(1) + ')');
    this.el.eyes.innerHTML = FACES[this.face].eyes;

    this.points = {};
    for (const [name, arm] of Object.entries(ARMS)) {
      const { elbow, hand } = armPoints(arm, p[arm.shoulderKey], p[arm.elbowKey]);
      const d = 'M' + arm.shoulder.join(' ') + 'L' + elbow.map((v) => v.toFixed(1)).join(' ') + 'L' + hand.map((v) => v.toFixed(1)).join(' ');
      this.el['arm-' + name + '-out'].setAttribute('d', d);
      this.el['arm-' + name + '-in'].setAttribute('d', d);
      this.el[name + 'Elbow'].setAttribute('transform', 'translate(' + elbow[0].toFixed(1) + ' ' + elbow[1].toFixed(1) + ')');
      this.el[name + 'Hand'].setAttribute('transform', 'translate(' + hand[0].toFixed(1) + ' ' + hand[1].toFixed(1) + ')');
      this.points[arm.shoulderKey] = arm.shoulder;
      this.points[arm.elbowKey] = elbow;
    }
    this.points.neckA = [360 + hx, 300];
    this.points.neckB = [360 + hx, 262 + hy];

    for (const [key, l] of Object.entries(LABELS)) {
      this.el['label-' + key].textContent = l.text + ' ' + Math.round(p[key]) + '°';
      const lead = this.el['lead-' + key];
      const [jx, jy] = this.points[key];
      const lx = l.at[0] + (l.anchor === 'start' ? -6 : 6);
      lead.setAttribute('x1', lx); lead.setAttribute('y1', l.at[1] - 5);
      lead.setAttribute('x2', jx.toFixed(1)); lead.setAttribute('y2', jy.toFixed(1));
    }
  }

  _toSvg(ev) {
    const pt = this.svg.createSVGPoint();
    pt.x = ev.clientX; pt.y = ev.clientY;
    const m = this.svg.getScreenCTM();
    return m ? pt.matrixTransform(m.inverse()) : pt;
  }

  _bindDrag() {
    let grab = null;
    this.svg.addEventListener('pointerdown', (ev) => {
      const node = ev.target.closest('[data-handle]');
      if (!node) return;
      ev.preventDefault();
      const id = node.dataset.handle;
      grab = { id, start: this._toSvg(ev), pose: { ...this.pose } };
      this.svg.setPointerCapture(ev.pointerId);
      this._mark(id, true);
      this.onGrab(true);
    });
    this.svg.addEventListener('pointermove', (ev) => {
      if (!grab) return;
      const pt = this._toSvg(ev);
      if (grab.id === 'head') {
        this._emit('neckA', grab.pose.neckA + (pt.x - grab.start.x) * 0.5);
        this._emit('neckB', grab.pose.neckB - (pt.y - grab.start.y) * 0.5);
        return;
      }
      const arm = ARMS[grab.id[0]];
      // Angle of the pointer seen from a pivot, in the same convention as armPoints().
      const angleFrom = (pivot) => deg(Math.atan2(arm.side * (pt.x - pivot[0]), pt.y - pivot[1]));
      if (grab.id.endsWith('Elbow')) {
        this._emit(arm.shoulderKey, clamp(angleFrom(arm.shoulder), 0, 180));
      } else {
        const elbow = this.points[arm.elbowKey];
        let rel = angleFrom(elbow) - this.pose[arm.shoulderKey];
        rel = ((rel + 540) % 360) - 180;
        this._emit(arm.elbowKey, clamp(90 + rel, 0, 180));
      }
    });
    const release = () => {
      if (!grab) return;
      this._mark(grab.id, false);
      grab = null;
      this.onGrab(false);
    };
    this.svg.addEventListener('pointerup', release);
    this.svg.addEventListener('pointercancel', release);
  }

  _emit(key, value) {
    this.onJoint(key, Math.round(value));
  }

  _mark(id, on) {
    const node = this.el[id];
    if (!node || id === 'head') return;
    node.querySelector('.ring').setAttribute('visibility', on ? 'visible' : 'hidden');
    node.querySelector('.knob').setAttribute('fill', on ? 'var(--accent)' : 'var(--blue)');
  }
}
