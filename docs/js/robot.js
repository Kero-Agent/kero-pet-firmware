// Robot model for the pages: turns shell commands into functions, keeps the
// latest readings, and remembers which servo channel drives which joint.

import { Link, SerialTransport } from './link.js';
import { uploadPicture } from './picture.js';
import { SimTransport } from './sim.js';

export const SERVO_COUNT = 6;

/** The pet's joints. `home` is the resting angle, `lo`/`hi` the default soft limits. */
export const JOINTS = {
  neckA:     { name: '脖子 α', sub: '左右转头', home: 90, lo: 30, hi: 150 },
  neckB:     { name: '脖子 β', sub: '抬头低头', home: 90, lo: 60, hi: 120 },
  lShoulder: { name: '左肩',   sub: '抬手臂',   home: 25, lo: 0,  hi: 180 },
  lElbow:    { name: '左肘',   sub: '弯曲',     home: 90, lo: 20, hi: 160 },
  rShoulder: { name: '右肩',   sub: '抬手臂',   home: 25, lo: 0,  hi: 180 },
  rElbow:    { name: '右肘',   sub: '弯曲',     home: 90, lo: 20, hi: 160 },
};
export const JOINT_KEYS = Object.keys(JOINTS);

const MODULE_NAMES = {
  i2c: 'I2C 总线', battery: '电池', pca9685: 'PCA9685', servo: '舵机', motor: '电机',
  imu: 'IMU', audio: '声音', display: '屏幕', camera: '摄像头',
};
const STORE_KEY = 'kero.servo-config.v1';

// ---------- parsers for the firmware's text output (exported for the tests) ----------

const NUM = /-?\d+(?:\.\d+)?/g;

export function parseStatus(lines) {
  const out = [];
  for (const l of lines) {
    const m = /^(\w+)\s+(ok|FAILED.*|skipped.*|disabled.*|not initialised)$/.exec(l.trim());
    if (m && MODULE_NAMES[m[1]]) {
      out.push({ id: m[1], name: MODULE_NAMES[m[1]], ok: m[2] === 'ok', state: m[2] });
    }
  }
  return out;
}

export function parseSys(lines) {
  const text = lines.join('\n');
  const pick = (re) => { const m = re.exec(text); return m ? m[1] : null; };
  return {
    idf: pick(/ESP-IDF (\S+?),/),
    uptimeS: Number(pick(/uptime ([\d.]+) s/)),
    reset: pick(/last reset: (.+)/),
    heapFree: Number(pick(/heap internal: (\d+) free/)),
    psramFree: Number(pick(/heap PSRAM:\s+(\d+) free/)),
  };
}

export function parseBattery(lines) {
  for (const l of lines) {
    const m = /^(-?\d+) mV/.exec(l.trim());
    if (m) {
      const pct = /about (\d+) %/.exec(l);
      return { mv: Number(m[1]), percent: pct ? Number(pct[1]) : null, absent: /no battery/.test(l) };
    }
  }
  return null;
}

export function parseImu(lines) {
  for (const l of lines) {
    if (!l.includes('|') || /ax/.test(l)) continue;
    const n = (l.match(NUM) || []).map(Number);
    if (n.length >= 7) return { accel: n.slice(0, 3), gyro: n.slice(3, 6), temp: n[6] };
  }
  return null;
}

export function parseMicLevel(lines) {
  let last = null;
  for (const l of lines) {
    const m = /rms\s+(-?[\d.]+) dBFS\s+peak\s+(-?[\d.]+) dBFS/.exec(l);
    if (m) last = { rms: Number(m[1]), peak: Number(m[2]) };
  }
  return last;
}

export function parseCamShot(lines) {
  let frame = null, fps = null;
  for (const l of lines) {
    const m = /frame \d+: (\d+)x(\d+), (\d+) bytes, mean R (\d+) G (\d+) B (\d+)/.exec(l);
    if (m) frame = { width: +m[1], height: +m[2], rgb: [+m[4], +m[5], +m[6]] };
    const f = /([\d.]+) frames per second/.exec(l);
    if (f) fps = Number(f[1]);
  }
  return frame ? { ...frame, fps } : null;
}

export function parseI2cScan(lines) {
  const out = [];
  for (const l of lines) {
    const m = /^\s*(0x[0-9A-Fa-f]{2})\s*(.*)$/.exec(l);
    if (m) out.push({ addr: m[1], name: m[2].trim() });
  }
  return out;
}

/** Lines of "servo" without arguments: pulse width per channel, 0 when released. */
export function parseServoState(lines) {
  const pulses = new Array(SERVO_COUNT).fill(0);
  for (const l of lines) {
    const m = /servo (\d):\s+(?:(\d+) us|released)/.exec(l);
    if (m && +m[1] < SERVO_COUNT) pulses[+m[1]] = m[2] ? Number(m[2]) : 0;
  }
  return pulses;
}

// ---------- configuration kept in the browser ----------

function defaultConfig() {
  return {
    assign: [...JOINT_KEYS],            // servo channel -> joint key
    cal: JOINT_KEYS.map((k) => ({ min: 500, max: 2500, trim: 0, lo: JOINTS[k].lo, hi: JOINTS[k].hi, home: JOINTS[k].home, invert: false })),
    custom: false,                      // true once the user changed something: then it is pushed to the robot on connect
  };
}

function loadConfig() {
  try {
    const raw = JSON.parse(localStorage.getItem(STORE_KEY));
    if (raw && Array.isArray(raw.assign) && raw.assign.length === SERVO_COUNT && Array.isArray(raw.cal) && raw.cal.length === SERVO_COUNT) {
      return raw;
    }
  } catch (e) { /* no storage or bad data: use defaults */ }
  return defaultConfig();
}

// ---------- the robot ----------

export class Robot {
  constructor() {
    this.link = new Link();
    this.mode = 'none';                 // 'sim' | 'real' | 'none'
    this.config = loadConfig();
    this.subs = [];
    this.state = {
      modules: [], sys: null, battery: null, imu: null, mic: null, cam: null,
      servoDeg: new Array(SERVO_COUNT).fill(null),      // null = released
      motor: { a: 0, b: 0 },
      batteryHistory: [], imuHistory: [],
    };
    this.link.on('state', () => {
      if (!this.link.connected) this.mode = 'none';
      this._emit('conn');
    });
    this._lastPoll = {};
  }

  // --- events ---
  subscribe(fn) { this.subs.push(fn); }
  _emit(topic) { for (const fn of this.subs) fn(topic); }

  saveConfig() {
    this.config.custom = true;
    try { localStorage.setItem(STORE_KEY, JSON.stringify(this.config)); } catch (e) { /* private window */ }
    this._emit('config');
  }

  // --- connection ---
  get connected() { return this.link.connected; }
  static get serialSupported() { return SerialTransport.supported; }

  async connectSim() {
    await this.link.attach(new SimTransport());
    this.mode = 'sim';
    this._emit('conn');
    await this._afterConnect();
  }

  /** Call from a click: the browser asks which port to use (unless an already picked port is given). */
  async connectSerial(port) {
    const t = new SerialTransport();
    await t.open(port);
    const ok = await this.link.attach(t);
    this.mode = 'real';
    this._emit('conn');
    if (ok) await this._afterConnect();
    return ok;
  }

  async disconnect() {
    await this.link.detach();
  }

  async _afterConnect() {
    await this.refreshStatus();
    await this.refreshSys();
    if (this.config.custom) {
      for (let id = 0; id < SERVO_COUNT; id++) await this.applyCalibration(id, { save: false });
    }
    await this.syncServos();
    await this.readBattery();
  }

  // --- generic ---
  cmd(text, opts) {
    return this.link.send(text, opts).catch((e) => ({ lines: [], ok: false, error: e.message }));
  }

  // --- modules / system ---
  async refreshStatus() {
    const r = await this.cmd('status');
    if (r.lines.length) { this.state.modules = parseStatus(r.lines); this._emit('modules'); }
  }

  moduleOk(id) {
    const m = this.state.modules.find((x) => x.id === id);
    return !m || m.ok;      // unknown counts as usable: the command itself will report an error
  }

  async retryModules() {
    await this.cmd('init all', { timeout: 15000 });
    await this.refreshStatus();
    await this.syncServos();
  }

  async initModule(id) {
    const r = await this.cmd('init ' + id, { timeout: 15000 });
    await this.refreshStatus();
    return r.ok;
  }

  async refreshSys(opts) {
    const r = await this.cmd('sys', opts);
    if (r.lines.length) { this.state.sys = parseSys(r.lines); this._emit('sys'); }
  }

  async reboot() {
    await this.cmd('reboot', { timeout: 8000 });
    this.state.servoDeg.fill(null);
    this.state.motor = { a: 0, b: 0 };
    this._emit('servo'); this._emit('motor');
    await new Promise((r) => setTimeout(r, 1500));
    if (this.connected) await this._afterConnect();
  }

  // --- battery / imu / mic ---
  async readBattery(opts) {
    const r = await this.cmd('bat', opts);
    const b = parseBattery(r.lines);
    if (b) {
      this.state.battery = b;
      const h = this.state.batteryHistory;
      h.push(b.mv / 1000);
      if (h.length > 120) h.shift();
      this._emit('battery');
    }
    return b;
  }

  async readImu(opts) {
    const r = await this.cmd('imu', opts);
    const s = parseImu(r.lines);
    if (s) {
      this.state.imu = s;
      const h = this.state.imuHistory;
      h.push(s.accel);
      if (h.length > 150) h.shift();
      this._emit('imu');
    }
    return s;
  }

  calibrateGyro() { return this.cmd('imu cal', { timeout: 6000 }); }

  async readMic(opts) {
    const r = await this.cmd('mic level 1 150', { timeout: 3000, ...opts });
    const m = parseMicLevel(r.lines);
    if (m) { this.state.mic = m; this._emit('mic'); }
    return m;
  }

  // --- servos ---
  jointOfServo(id) { return this.config.assign[id]; }
  servoOfJoint(key) { return this.config.assign.indexOf(key); }

  /** Angle that the channel was last told to hold, from the robot's own pulse width. */
  async syncServos() {
    const r = await this.cmd('servo');
    if (!r.lines.length) return;
    const pulses = parseServoState(r.lines);
    pulses.forEach((us, id) => { this.state.servoDeg[id] = us ? this.pulseToDeg(id, us) : null; });
    this._emit('servo');
  }

  pulseToDeg(id, us) {
    const c = this.config.cal[id];
    let deg = ((us - c.trim - c.min) / (c.max - c.min)) * 180;
    if (c.invert) deg = 180 - deg;
    return Math.round(Math.max(0, Math.min(180, deg)));
  }

  degToPulse(id, deg) {
    const c = this.config.cal[id];
    const d = c.invert ? 180 - deg : deg;
    return Math.round(c.min + (c.max - c.min) * d / 180 + c.trim);
  }

  clampDeg(id, deg) {
    const c = this.config.cal[id];
    return Math.round(Math.max(c.lo, Math.min(c.hi, deg)));
  }

  setServo(id, deg) {
    const d = this.clampDeg(id, deg);
    if (this.state.servoDeg[id] === d) return Promise.resolve();
    this.state.servoDeg[id] = d;
    this._emit('servo');
    // Slider drags produce many values: the key lets a newer one replace an unsent older one.
    return this.cmd('servo ' + id + ' ' + d, { key: 'servo' + id });
  }

  setJoint(key, deg) {
    const id = this.servoOfJoint(key);
    return id >= 0 ? this.setServo(id, deg) : Promise.resolve();
  }

  jointDeg(key) {
    const id = this.servoOfJoint(key);
    const d = id >= 0 ? this.state.servoDeg[id] : null;
    return d === null || d === undefined ? this.config.cal[Math.max(id, 0)].home : d;
  }

  async releaseServo(id) {
    this.state.servoDeg[id] = null;
    this._emit('servo');
    await this.cmd('servo off ' + id);
  }

  async releaseAll() {
    this.state.servoDeg.fill(null);
    this._emit('servo');
    await this.cmd('servo off');
  }

  async homeAll() {
    for (let id = 0; id < SERVO_COUNT; id++) await this.setServo(id, this.config.cal[id].home);
  }

  async sweepServo(id) {
    const c = this.config.cal[id];
    await this.cmd('servo sweep ' + id + ' ' + c.lo + ' ' + c.hi, { timeout: 15000 });
    this.state.servoDeg[id] = c.lo;
    this._emit('servo');
  }

  /** Send one channel's calibration to the robot (it is not stored there across a reboot). */
  async applyCalibration(id, { save = true } = {}) {
    const c = this.config.cal[id];
    await this.cmd('servo cal ' + id + ' ' + c.min + ' ' + c.max + ' ' + c.trim);
    await this.cmd('servo limit ' + id + ' ' + c.lo + ' ' + c.hi);
    await this.cmd('servo invert ' + id + ' ' + (c.invert ? 1 : 0));
    if (save) this.saveConfig();
  }

  // --- motors ---
  setMotors(a, b, { quiet = false } = {}) {
    const clamp = (v) => Math.round(Math.max(-100, Math.min(100, v)));
    a = clamp(a); b = clamp(b);
    this.state.motor = { a, b };
    this._emit('motor');
    if (a === b) return this.cmd('motor both ' + a, { key: 'motor', quiet });
    this.cmd('motor a ' + a, { key: 'motorA', quiet });
    return this.cmd('motor b ' + b, { key: 'motorB', quiet });
  }

  async stopMotors(brake = false) {
    this.state.motor = { a: 0, b: 0 };
    this._emit('motor');
    await this.cmd(brake ? 'motor brake' : 'motor stop', { urgent: true });
  }

  setMotorWatchdog(ms) { return this.cmd('motor wdt ' + ms); }

  /** Everything off, ahead of anything still queued. */
  async emergencyStop() {
    this.link.clearQueue();
    this.state.motor = { a: 0, b: 0 };
    this.state.servoDeg.fill(null);
    this._emit('motor'); this._emit('servo');
    const stop = this.cmd('motor stop', { urgent: true });
    await this.cmd('servo off');
    await stop;
  }

  // --- sound / display / camera ---
  tone(hz, ms) { return this.cmd('spk tone ' + hz + ' ' + ms, { timeout: ms + 3000 }); }
  scale() { return this.cmd('spk scale', { timeout: 8000 }); }
  setVolume(pct) { return this.cmd('spk vol ' + pct, { key: 'vol' }); }
  micEcho(ms) { return this.cmd('mic echo ' + ms, { timeout: ms * 2 + 4000 }); }
  setBacklight(pct) { return this.cmd('lcd bl ' + pct, { key: 'bl' }); }
  lcd(args) { return this.cmd('lcd ' + args); }
  /** Bring back the animated face, the screen's default picture. */
  face() { return this.cmd('face'); }
  showPictureAgain() { return this.cmd('img show'); }

  /** Show a picture (base64 from rgbaToBase64) on the screen. Rejects with a readable message. */
  async sendPicture(base64, onProgress) {
    this.link.log('tx', 'kero> img recv   （上传图片，' + Math.round(base64.length / 1024) + ' KB）');
    try {
      await this.link.exclusive((io) => uploadPicture(io, base64, onProgress));
      this.link.log('rx', 'DONE');
    } catch (e) {
      this.link.log('err', e.message);
      throw e;
    }
  }

  async camShot(count = 1) {
    const r = await this.cmd('cam shot ' + count, { timeout: 4000 + count * 1500 });
    const c = parseCamShot(r.lines);
    this.state.cam = c;
    this._emit('cam');
    return c;
  }

  // --- background readings ---
  /**
   * Call a few times per second. `want` says which readings the visible page needs.
   * Polls only while the link is idle, so it never delays a user's command.
   */
  async poll(want) {
    if (!this.link.idle) return;
    const now = Date.now();
    const due = (name, every) => {
      if (!want[name] || now - (this._lastPoll[name] || 0) < every) return false;
      this._lastPoll[name] = now;
      return true;
    };
    const quiet = { quiet: !want.showInConsole };
    if (due('imu', 200) && this.moduleOk('imu')) await this.readImu(quiet);
    if (due('battery', 3000) && this.moduleOk('battery')) await this.readBattery(quiet);
    if (due('mic', 900) && this.moduleOk('audio')) await this.readMic(quiet);
    if (due('sys', 5000)) await this.refreshSys(quiet);
  }
}
