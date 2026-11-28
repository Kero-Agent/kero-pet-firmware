// A pretend robot that answers the same shell commands as the firmware, in the
// same text format. It lets the site run without hardware (GitHub Pages
// visitors) and keeps one code path: everything above the transport is identical.

const MODULES = ['i2c', 'battery', 'pca9685', 'servo', 'motor', 'imu', 'audio', 'display', 'camera'];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const noise = (amp) => (Math.random() - 0.5) * 2 * amp;
const pad = (s, n) => String(s).padEnd(n);
const fix = (v, w, d) => v.toFixed(d).padStart(w);

export class SimTransport {
  constructor() {
    this.onData = null;
    this.onClose = null;
    this.buffer = '';
    this.busy = Promise.resolve();
    this.closed = false;
    this.recv = null;           // characters received so far while a picture is coming in
    this.t0 = Date.now();
    this.s = {
      pulse: [0, 0, 0, 0, 0, 0],
      cal: Array.from({ length: 6 }, () => ({ min: 500, max: 2500, trim: 0, lo: 0, hi: 180, invert: false })),
      motor: { a: 0, b: 0 },
      wdt: 0,
      volume: 30,
      backlight: 60,
      lastServoMove: 0,
      gyroBias: [0.42, -0.31, 0.18],
    };
  }

  async open() { /* nothing to open */ }

  async close() {
    this.closed = true;
  }

  write(text) {
    if (this.recv !== null) {
      // "img recv": base64 text, one dot back per 192 characters (see main/app_image.c)
      for (const ch of text) {
        if (ch === '\r' || ch === '\n') continue;
        this.recv++;
        if (this.recv % 192 === 0) this._out('.');
        if (this.recv === 360 * 360 * 2 / 3 * 4) {
          this.recv = null;
          this._out('\nDONE\nkero> ');
          break;
        }
      }
      return Promise.resolve();
    }
    this.buffer += text;
    let i;
    while ((i = this.buffer.indexOf('\n')) >= 0) {
      const line = this.buffer.slice(0, i).replace(/\r$/, '');
      this.buffer = this.buffer.slice(i + 1);
      this.busy = this.busy.then(() => this._run(line));
    }
    return Promise.resolve();
  }

  _out(text) {
    if (!this.closed && this.onData) this.onData(text);
  }

  async _run(line) {
    await sleep(12);
    if (line.trim()) this._out(line + '\n');          // the real shell echoes what was typed
    let reply;
    try {
      reply = await this._exec(line.trim().split(/\s+/).filter(Boolean));
    } catch (e) {
      reply = ['sim error: ' + e.message];
    }
    for (const l of reply) this._out(l + '\n');
    if (this.recv === null) this._out('kero> ');
  }

  _servoDeg(id) {
    const c = this.s.cal[id];
    const p = this.s.pulse[id];
    return p ? ((p - c.trim - c.min) / (c.max - c.min)) * 180 : null;
  }

  _battery() {
    const sag = Math.max(0, 1 - (Date.now() - this.s.lastServoMove) / 600) * 110
      + (Math.abs(this.s.motor.a) + Math.abs(this.s.motor.b)) * 1.3;
    return Math.round(3920 - sag + noise(6));
  }

  async _exec(a) {
    const s = this.s;
    const fail = (msg) => [msg, 'Command returned non-zero error code: 0x1 (ERROR)'];
    if (a.length === 0) return [];
    switch (a[0]) {
      case 'help':
        return ['help  status  init  sys  reboot  log  i2c  bat  pca  servo  motor  imu  spk  mic  lcd  cam  preview  face  img', '（模拟模式：没有连接真机）'];

      case 'status':
        return ['module    state', ...MODULES.map((m) => pad(m, 9) + ' ok')];

      case 'init':
        if (a[1] === 'all') return ['module    state', ...MODULES.map((m) => pad(m, 9) + ' ok')];
        return MODULES.includes(a[1]) ? [a[1] + ': ok'] : fail("unknown module '" + a[1] + "'");

      case 'sys': {
        const up = (Date.now() - this.t0) / 1000;
        return [
          'ESP-IDF sim, chip rev v0.2, 2 core(s), flash 16 MB',
          'uptime ' + up.toFixed(3) + ' s, last reset: power on',
          'heap internal: ' + Math.round(268000 + noise(900)) + ' free, 251340 minimum ever',
          'heap PSRAM:    7964112 free of 8388608',
        ];
      }

      case 'reboot':
        this.t0 = Date.now();
        s.pulse.fill(0);
        s.motor = { a: 0, b: 0 };
        await sleep(500);
        return ['restarting...', 'I (312) app: Kero firmware starting'];

      case 'log':
        return ["log level of '" + a[1] + "' set to " + a[2]];

      case 'bat': {
        const n = Math.min(+a[1] || 1, 20);
        const out = [];
        for (let i = 0; i < n; i++) {
          const mv = this._battery();
          out.push(mv + ' mV, about ' + Math.max(0, Math.min(100, Math.round((mv - 3300) / 9))) + ' %');
        }
        return out;
      }

      case 'i2c':
        if (a[1] === 'scan') {
          return ['  0x15  ', '  0x40  PCA9685 (power board)', '  0x6A  LSM6DS3TR-C', '3 device(s) on SDA=41 SCL=42'];
        }
        if (a[1] === 'r') {
          const addr = Number(a[2]), reg = Number(a[3]);
          const v = addr === 0x6a && reg === 0x0f ? 0x6a : Math.floor(Math.random() * 256);
          const hex = (x) => x.toString(16).toUpperCase().padStart(2, '0');
          return ['0x' + hex(addr) + '[0x' + hex(reg) + ']: ' + hex(v)];
        }
        if (a[1] === 'w') return ['ok'];
        return fail('usage: i2c scan | i2c r <addr> <reg> [count] | i2c w <addr> <reg> <value>');

      case 'pca':
        return ['frequency 50.05 Hz', ...s.pulse.map((p, i) => '  ch' + pad(i, 2) + (p ? ' duty ' + Math.round(p * 4096 / 19980) + ' / 4096' : ' off'))];

      case 'servo': {
        if (a.length === 1) {
          return s.pulse.map((p, i) => '  servo ' + i + ': ' + (p ? p + ' us' : 'released'));
        }
        const setPulse = (id, us) => {
          s.pulse[id] = Math.max(400, Math.min(2600, Math.round(us)));
          s.lastServoMove = Date.now();
        };
        const setAngle = (id, deg) => {
          const c = s.cal[id];
          let d = Math.max(c.lo, Math.min(c.hi, deg));
          if (c.invert) d = 180 - d;
          setPulse(id, c.min + (c.max - c.min) * d / 180 + c.trim);
        };
        const idOk = (v) => Number.isInteger(+v) && +v >= 0 && +v < 6;
        if (a[1] === 'off') {
          if (a[2] !== undefined && idOk(a[2])) s.pulse[+a[2]] = 0; else s.pulse.fill(0);
          return ['ok'];
        }
        if (a[1] === 'us' && idOk(a[2])) { setPulse(+a[2], +a[3]); return ['ok']; }
        if (a[1] === 'sweep' && idOk(a[2])) {
          const from = a[3] !== undefined ? +a[3] : 30, to = a[4] !== undefined ? +a[4] : 150;
          for (const target of [to, from]) { setAngle(+a[2], target); await sleep(1100); }
          return ['ok'];
        }
        if (a[1] === 'cal' && idOk(a[2])) {
          Object.assign(s.cal[+a[2]], { min: +a[3], max: +a[4], trim: a[5] !== undefined ? +a[5] : s.cal[+a[2]].trim });
          return ['ok'];
        }
        if (a[1] === 'limit' && idOk(a[2])) { Object.assign(s.cal[+a[2]], { lo: +a[3], hi: +a[4] }); return ['ok']; }
        if (a[1] === 'invert' && idOk(a[2])) { s.cal[+a[2]].invert = a[3] !== '0'; return ['ok']; }
        if (a[1] === 'all') { for (let i = 0; i < 6; i++) setAngle(i, +a[2]); return ['ok']; }
        if (idOk(a[1]) && a[2] !== undefined) { setAngle(+a[1], +a[2]); return ['ok']; }
        return fail('usage: servo <id|all> <deg> ...');
      }

      case 'motor': {
        if (a.length === 1) return ['motor A ' + s.motor.a + ' %, motor B ' + s.motor.b + ' %'];
        if (a[1] === 'stop' || a[1] === 'brake') { s.motor = { a: 0, b: 0 }; return ['ok']; }
        if (a[1] === 'wdt') { s.wdt = +a[2]; return ['ok']; }
        if (a[1] === 'invert') return ['ok'];
        const v = Math.max(-100, Math.min(100, Math.round(+a[2])));
        if (!['a', 'b', 'both'].includes(a[1]) || Number.isNaN(v)) return fail('usage: motor <a|b|both> <-100..100> [ms]');
        if (a[1] !== 'b') s.motor.a = v;
        if (a[1] !== 'a') s.motor.b = v;
        clearTimeout(this._wdtTimer);
        if (s.wdt) this._wdtTimer = setTimeout(() => { s.motor = { a: 0, b: 0 }; }, s.wdt);
        if (a[3] !== undefined) { await sleep(Math.min(+a[3], 3000)); s.motor = { a: 0, b: 0 }; }
        return ['ok'];
      }

      case 'imu': {
        if (a[1] === 'cal') { await sleep(1000); s.gyroBias = [0, 0, 0]; return ['keep the robot still...', 'ok']; }
        const turn = (s.motor.a - s.motor.b) * 0.9;
        const drive = (s.motor.a + s.motor.b) / 200;
        const row = fix(noise(0.006) + drive * 0.05, 7, 3) + ' ' + fix(noise(0.006), 7, 3) + ' ' + fix(1 + noise(0.004), 7, 3)
          + '     | ' + fix(s.gyroBias[0] + noise(0.15), 8, 2) + ' ' + fix(s.gyroBias[1] + noise(0.15), 8, 2) + ' ' + fix(s.gyroBias[2] + turn + noise(0.2), 8, 2)
          + '       | ' + fix(31.4 + noise(0.1), 5, 1);
        return ['   ax      ay      az   (g) |    gx       gy       gz  (dps) |  temp', row];
      }

      case 'spk':
        if (a[1] === 'tone') { await sleep(Math.min(+a[3] || 500, 3000)); return ['ok']; }
        if (a[1] === 'scale') { await sleep(1600); return ['ok']; }
        if (a[1] === 'vol') { s.volume = +a[2]; return ['volume ' + s.volume + ' %']; }
        return ['volume ' + s.volume + ' %, 16000 Hz'];

      case 'mic': {
        if (a[1] === 'echo') { await sleep(Math.min((+a[2] || 2000) * 2, 6000)); return ['recording...', 'playing back...', 'ok']; }
        if (a[1] === 'gain') return ['ok'];
        const n = Math.min(+a[2] || 5, 10);
        const out = [];
        for (let i = 0; i < n; i++) {
          await sleep(Math.min(+a[3] || 200, 400));
          const rms = -46 + noise(5) + (Math.random() < 0.15 ? 14 : 0);
          out.push('rms ' + fix(rms, 6, 1) + ' dBFS  peak ' + fix(rms + 11, 6, 1) + ' dBFS  dc ' + String(Math.round(noise(20))).padStart(5) + '  ' + '#'.repeat(Math.max(0, Math.round((rms + 90) / 2))));
        }
        return out;
      }

      case 'lcd':
        if (a.length === 1) return ['panel ready, 360x360, backlight ' + s.backlight + ' %'];
        if (a[1] === 'bl') s.backlight = Math.max(0, Math.min(100, +a[2]));
        return ['ok'];

      case 'face':
        return ['ok'];

      case 'img':
        if (a[1] === 'recv') {
          this.recv = 0;
          return ['READY 192'];
        }
        return ['ok'];

      case 'cam': {
        if (a.length === 1) return ['sensor PID 0x2145 (GC2145)'];
        if (a[1] === 'shot') {
          const n = Math.min(+a[2] || 1, 30);
          const out = [];
          const t = Date.now() / 4000;
          for (let i = 0; i < n; i++) {
            await sleep(110);
            const r = Math.round(120 + 60 * Math.sin(t + i * 0.05)), g = Math.round(130 + 50 * Math.sin(t * 1.3 + 1)), b = Math.round(110 + 60 * Math.sin(t * 0.7 + 2));
            out.push('frame ' + i + ': 320x240, 153600 bytes, mean R ' + r + ' G ' + g + ' B ' + b);
          }
          if (n > 1) out.push('8.7 frames per second');
          return out;
        }
        return ['ok'];
      }

      case 'preview':
        await sleep(1000);
        return ['42 frames, 8.4 per second'];

      default:
        return ['Unrecognized command'];
    }
  }
}
