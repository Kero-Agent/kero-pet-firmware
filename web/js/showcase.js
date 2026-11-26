// Showcase page: the pet model, preset moves, driving, faces and sounds.

import { JOINTS, JOINT_KEYS } from './robot.js';
import { Pet, FACES, faceIcon } from './pet.js';

const $ = (id) => document.getElementById(id);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const POSE_KEY = 'kero.poses.v1';

export function initShowcase(robot) {
  const state = robot.state;
  const currentPose = () => Object.fromEntries(JOINT_KEYS.map((k) => [k, robot.jointDeg(k)]));
  const homePose = () => Object.fromEntries(JOINT_KEYS.map((k) => [k, robot.config.cal[Math.max(robot.servoOfJoint(k), 0)].home]));

  // ---------------- model ----------------
  const pet = new Pet($('pet-stage'), {
    onJoint: (key, deg) => { cancelAction(); robot.setJoint(key, deg); },
  });
  function renderPet() {
    pet.setPose(currentPose());
    pet.setWheels(state.motor.a, state.motor.b);
  }

  // ---------------- joints ----------------
  const jointRows = {};
  $('joint-list').innerHTML = JOINT_KEYS.map((k) =>
    '<div class="joint"><div class="joint-head"><span>' + JOINTS[k].name + ' ' + JOINTS[k].sub + '</span><span class="mono muted" data-v="' + k + '"></span></div>'
    + '<input type="range" data-j="' + k + '" min="0" max="180" step="1" aria-label="' + JOINTS[k].name + '"></div>'
  ).join('');
  for (const input of $('joint-list').querySelectorAll('input')) {
    const key = input.dataset.j;
    jointRows[key] = { input, value: $('joint-list').querySelector('[data-v="' + key + '"]') };
    input.addEventListener('input', () => { cancelAction(); robot.setJoint(key, Number(input.value)); });
  }
  function renderJoints() {
    for (const key of JOINT_KEYS) {
      const id = robot.servoOfJoint(key);
      const c = robot.config.cal[Math.max(id, 0)];
      const row = jointRows[key];
      row.input.min = c.lo; row.input.max = c.hi;
      const d = robot.jointDeg(key);
      if (document.activeElement !== row.input) row.input.value = d;
      row.value.textContent = state.servoDeg[id] === null ? '已释放' : d + '°';
    }
  }

  // ---------------- moves ----------------
  let token = 0;
  function cancelAction() {
    token++;
    for (const b of $('action-grid').querySelectorAll('.running')) b.classList.remove('running');
  }

  /** Glide from the current pose to `target` (only the listed joints) in `ms`. False when cancelled. */
  async function moveTo(target, ms, mine) {
    const from = currentPose();
    const steps = Math.max(1, Math.round(ms / 40));
    for (let i = 1; i <= steps; i++) {
      if (mine !== token) return false;
      const t = i / steps;
      const ease = t * t * (3 - 2 * t);
      for (const [key, to] of Object.entries(target)) robot.setJoint(key, from[key] + (to - from[key]) * ease);
      await sleep(40);
    }
    return true;
  }
  async function pause(ms, mine) {
    await sleep(ms);
    return mine === token;
  }

  const ACTIONS = [
    { name: '点头', sub: '脖子 β', run: async (m) => {
      for (const b of [112, 72, 112, 72]) if (!await moveTo({ neckB: b }, 260, m)) return;
      await moveTo({ neckB: 90 }, 260, m);
    } },
    { name: '摇头', sub: '脖子 α', run: async (m) => {
      for (const a of [60, 120, 60, 120]) if (!await moveTo({ neckA: a }, 300, m)) return;
      await moveTo({ neckA: 90 }, 300, m);
    } },
    { name: '挥手', sub: '左臂', run: async (m) => {
      if (!await moveTo({ lShoulder: 150, lElbow: 110 }, 500, m)) return;
      for (const e of [60, 130, 60, 130, 60]) if (!await moveTo({ lElbow: e }, 220, m)) return;
      await moveTo({ lShoulder: homePose().lShoulder, lElbow: 90 }, 500, m);
    } },
    { name: '歪头看', sub: '脖子 + 表情', run: async (m) => {
      setFace('surprised');
      if (!await moveTo({ neckA: 118, neckB: 102 }, 400, m)) return;
      if (!await pause(900, m)) return;
      await moveTo({ neckA: 90, neckB: 90 }, 400, m);
      setFace('happy');
    } },
    { name: '伸懒腰', sub: '两臂', run: async (m) => {
      if (!await moveTo({ lShoulder: 170, rShoulder: 170, lElbow: 90, rElbow: 90, neckB: 110 }, 800, m)) return;
      if (!await pause(700, m)) return;
      await moveTo({ ...homePose() }, 800, m);
    } },
    { name: '鼓掌', sub: '两臂', run: async (m) => {
      if (!await moveTo({ lShoulder: 75, rShoulder: 75, lElbow: 120, rElbow: 120 }, 450, m)) return;
      for (let i = 0; i < 4; i++) {
        if (!await moveTo({ lElbow: 160, rElbow: 160 }, 150, m)) return;
        if (!await moveTo({ lElbow: 120, rElbow: 120 }, 150, m)) return;
      }
      await moveTo({ ...homePose() }, 500, m);
    } },
    { name: '转一圈', sub: '底盘', run: async (m) => {
      robot.setMotors(45, -45);
      const ok = await pause(1400, m);
      if (ok) robot.stopMotors();
    } },
    { name: '睡觉', sub: '全部释放', run: async (m) => {
      setFace('sleepy');
      if (!await moveTo({ ...homePose(), neckB: 72 }, 900, m)) return;
      await robot.releaseAll();
    } },
  ];

  function loadPoses() {
    try { const p = JSON.parse(localStorage.getItem(POSE_KEY)); return Array.isArray(p) ? p : []; } catch (e) { return []; }
  }
  let poses = loadPoses();
  function savePoses() {
    try { localStorage.setItem(POSE_KEY, JSON.stringify(poses)); } catch (e) { /* private window */ }
  }

  function renderActions() {
    const grid = $('action-grid');
    grid.innerHTML = '';
    ACTIONS.forEach((a) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'btn';
      b.innerHTML = '<strong>' + a.name + '</strong><span>' + a.sub + '</span>';
      b.onclick = async () => {
        cancelAction();
        const mine = token;
        b.classList.add('running');
        await a.run(mine);
        b.classList.remove('running');
      };
      grid.appendChild(b);
    });
    poses.forEach((p, i) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'btn';
      b.innerHTML = '<strong></strong><span>存的姿势 · 按住 Shift 点击删除</span>';
      b.querySelector('strong').textContent = p.name;
      b.onclick = (ev) => {
        if (ev.shiftKey) { poses.splice(i, 1); savePoses(); renderActions(); return; }
        cancelAction();
        moveTo(p.pose, 600, token);
      };
      grid.appendChild(b);
    });
  }
  $('pose-save').onclick = () => {
    const name = window.prompt('给这个姿势起个名字', '姿势 ' + (poses.length + 1));
    if (!name) return;
    poses.push({ name: name.slice(0, 12), pose: currentPose() });
    savePoses();
    renderActions();
  };

  // ---------------- faces ----------------
  function setFace(key) {
    pet.setFace(key);
    for (const b of $('face-grid').querySelectorAll('button')) b.setAttribute('aria-pressed', String(b.dataset.face === key));
  }
  $('face-grid').innerHTML = Object.keys(FACES).map((k) =>
    '<button type="button" data-face="' + k + '" aria-pressed="false">' + faceIcon(k) + FACES[k].name + '</button>'
  ).join('');
  for (const b of $('face-grid').querySelectorAll('button')) b.onclick = () => setFace(b.dataset.face);
  setFace('happy');
  const calm = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (!calm) {
    setInterval(() => {
      if (!$('view-showcase').hidden && (pet.face === 'happy' || pet.face === 'left')) pet.blink();
    }, 4200);
  }

  // ---------------- driving ----------------
  const pad = $('joystick'), knob = $('joy-knob');
  const REACH = 50;
  let driving = false;
  function drive(x, y) {                     // x: right, y: forward, both -1..1
    const max = Number($('drv-max').value);
    const clamp = (v) => Math.max(-max, Math.min(max, v));
    const left = Math.round(clamp((y + x) * max)), right = Math.round(clamp((y - x) * max));
    knob.style.transform = 'translate(' + (x * REACH).toFixed(0) + 'px,' + (-y * REACH).toFixed(0) + 'px)';
    if (x === 0 && y === 0) robot.stopMotors(); else robot.setMotors(left, right);
  }
  function fromPointer(ev) {
    const r = pad.getBoundingClientRect();
    let x = (ev.clientX - (r.left + r.width / 2)) / REACH, y = -(ev.clientY - (r.top + r.height / 2)) / REACH;
    const len = Math.hypot(x, y);
    if (len > 1) { x /= len; y /= len; }
    if (len < 0.12) { x = 0; y = 0; }         // dead zone in the middle
    drive(x, y);
  }
  pad.addEventListener('pointerdown', (ev) => { ev.preventDefault(); pad.focus(); cancelAction(); driving = true; pad.setPointerCapture(ev.pointerId); fromPointer(ev); });
  pad.addEventListener('pointermove', (ev) => { if (driving) fromPointer(ev); });
  const letGo = () => { if (driving) { driving = false; drive(0, 0); } };
  pad.addEventListener('pointerup', letGo);
  pad.addEventListener('pointercancel', letGo);
  const KEYS = { ArrowUp: [0, 1], ArrowDown: [0, -1], ArrowLeft: [-1, 0], ArrowRight: [1, 0] };
  const held = new Set();
  const fromKeys = () => {
    let x = 0, y = 0;
    for (const k of held) { x += KEYS[k][0]; y += KEYS[k][1]; }
    drive(x * 0.7, y);
  };
  pad.addEventListener('keydown', (ev) => { if (KEYS[ev.key]) { ev.preventDefault(); if (!held.has(ev.key)) { held.add(ev.key); fromKeys(); } } });
  pad.addEventListener('keyup', (ev) => { if (held.delete(ev.key)) fromKeys(); });
  pad.addEventListener('blur', () => { if (held.size) { held.clear(); drive(0, 0); } });
  $('drv-max').addEventListener('input', (ev) => { $('drv-max-label').textContent = '速度上限 ' + ev.target.value + ' %'; });
  function renderDrive() {
    const fmt = (v) => (v > 0 ? '+' : '') + v + ' %';
    $('drv-l').textContent = '左轮 ' + fmt(state.motor.a);
    $('drv-r').textContent = '右轮 ' + fmt(state.motor.b);
    if (!driving && !held.size && state.motor.a === 0 && state.motor.b === 0) knob.style.transform = '';
  }

  // ---------------- camera / sound / status ----------------
  $('show-shot').onclick = async () => {
    const c = await robot.camShot(1);
    if (c) $('show-cam').style.background = 'rgb(' + c.rgb.join(',') + ')';
  };
  $('snd-chirp').onclick = async () => { await robot.tone(1320, 80); await robot.tone(1760, 130); };
  $('snd-scale').onclick = () => robot.scale();
  $('snd-echo').onclick = () => robot.micEcho(2000);
  $('show-vol').addEventListener('input', (ev) => {
    $('show-vol-val').textContent = ev.target.value + ' %';
    robot.setVolume(ev.target.value);
  });
  function renderStatus() {
    const b = state.battery;
    $('show-bat').textContent = !b ? '—' : b.absent ? '没接电池' : (b.mv / 1000).toFixed(2) + ' V';
    const s = state.imu;
    if (s) {
      // Tilt from gravity. Which axis is "forward" depends on how the board sits in the body.
      const [ax, ay, az] = s.accel;
      $('show-pitch').textContent = Math.round(Math.atan2(ax, Math.hypot(ay, az)) * 180 / Math.PI) + '°';
      $('show-roll').textContent = Math.round(Math.atan2(ay, az) * 180 / Math.PI) + '°';
    }
    const m = state.mic;
    if (m) {
      $('show-db').textContent = m.rms.toFixed(0) + ' dBFS';
      $('show-mic-bar').style.width = Math.max(0, Math.min(100, (m.rms + 90) / 0.9)) + '%';
    }
  }

  // ---------------- wiring ----------------
  robot.subscribe((topic) => {
    if (topic === 'servo' || topic === 'config' || topic === 'conn') { renderPet(); renderJoints(); }
    if (topic === 'motor') { renderPet(); renderDrive(); }
    if (topic === 'battery' || topic === 'imu' || topic === 'mic') renderStatus();
  });
  renderActions(); renderPet(); renderJoints(); renderDrive();

  return {
    wants: () => ({ imu: true, battery: true, mic: true }),
    shown: () => { renderPet(); },
    cancel: cancelAction,
  };
}
