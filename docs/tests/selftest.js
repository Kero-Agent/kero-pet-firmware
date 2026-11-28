// Self-test without a browser or a robot:  node docs/tests/selftest.js
// Runs the command link against the simulated robot and against canned firmware output.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { checkManifest, looksLikeEspImage, md5Hex } from '../js/flash.js';
import { Link, cleanText } from '../js/link.js';
import { Robot, parseStatus, parseSys, parseBattery, parseImu, parseMicLevel, parseCamShot, parseI2cScan, parseServoState } from '../js/robot.js';

let passed = 0;
async function test(name, fn) {
  try {
    await fn();
    passed++;
    console.log('ok   ' + name);
  } catch (e) {
    console.log('FAIL ' + name + '\n     ' + (e && e.stack || e));
    process.exitCode = 1;
  }
}

/** Transport that replies with whatever the test scripted. */
class ScriptedTransport {
  constructor(reply) { this.reply = reply; this.sent = []; }
  write(text) {
    this.sent.push(text);
    for (const chunk of this.reply(text)) setTimeout(() => this.onData && this.onData(chunk), 1);
    return Promise.resolve();
  }
  async close() {}
}

// Text exactly as the firmware prints it (see components/*/*_cmd.c).
const FW = {
  status: ['module    state', 'i2c       ok', 'battery   ok', 'pca9685   FAILED (ESP_ERR_NOT_FOUND)', "servo     skipped, needs 'pca9685'", 'motor     skipped, needs \'pca9685\'', 'imu       ok', 'audio     ok', 'display   ok', 'camera    FAILED (ESP_ERR_NOT_FOUND)'],
  sys: ['ESP-IDF v6.1.0, chip rev v0.2, 2 core(s), flash 16 MB', 'uptime 767.412 s, last reset: power on', 'heap internal: 274512 free, 251340 minimum ever', 'heap PSRAM:    7964112 free of 8388608'],
  bat: ['3924 mV, about 71 %'],
  batOff: ['12 mV - no battery voltage (power switch off, or power board not connected)'],
  imu: ['   ax      ay      az   (g) |    gx       gy       gz  (dps) |  temp', '  0.012  -0.031   0.998     |     0.42    -0.31     0.18       |  31.4'],
  mic: ['rms  -42.3 dBFS  peak  -30.1 dBFS  dc    12  ########################'],
  cam: ['frame 0: 320x240, 153600 bytes, mean R 121 G 133 B 97', 'frame 1: 320x240, 153600 bytes, mean R 122 G 131 B 99', '8.3 frames per second'],
  i2c: ['  0x40  PCA9685 (power board)', '  0x6A  LSM6DS3TR-C', '2 device(s) on SDA=41 SCL=42'],
  servo: ['  servo 0: 1633 us   cal 500..2500 us trim 0, limit 0..180 deg', '  servo 1: released   cal 500..2500 us trim 0, limit 0..180 deg'],
};

await test('parsers read the firmware formats', () => {
  const st = parseStatus(FW.status);
  assert.equal(st.length, 9);
  assert.equal(st.find((m) => m.id === 'imu').ok, true);
  assert.equal(st.find((m) => m.id === 'camera').ok, false);
  assert.equal(st.find((m) => m.id === 'servo').ok, false);
  const sys = parseSys(FW.sys);
  assert.equal(sys.idf, 'v6.1.0');
  assert.equal(sys.reset, 'power on');
  assert.equal(sys.heapFree, 274512);
  assert.equal(sys.psramFree, 7964112);
  assert.equal(Math.round(sys.uptimeS), 767);
  assert.deepEqual(parseBattery(FW.bat), { mv: 3924, percent: 71, absent: false });
  assert.equal(parseBattery(FW.batOff).absent, true);
  assert.deepEqual(parseImu(FW.imu), { accel: [0.012, -0.031, 0.998], gyro: [0.42, -0.31, 0.18], temp: 31.4 });
  assert.deepEqual(parseMicLevel(FW.mic), { rms: -42.3, peak: -30.1 });
  const cam = parseCamShot(FW.cam);
  assert.deepEqual(cam.rgb, [122, 131, 99]);
  assert.equal(cam.fps, 8.3);
  assert.deepEqual(parseI2cScan(FW.i2c).map((d) => d.addr), ['0x40', '0x6A']);
  assert.deepEqual(parseServoState(FW.servo), [1633, 0, 0, 0, 0, 0]);
});

await test('plain echo mode: echo and prompt are stripped', async () => {
  const link = new Link();
  const t = new ScriptedTransport((text) => {
    const cmd = text.trim();
    if (!cmd) return ['kero> '];
    return [cmd + '\n', FW.bat[0] + '\n', 'kero> '];
  });
  assert.equal(await link.attach(t), true);
  const r = await link.send('bat');
  assert.deepEqual(r.lines, FW.bat);
  assert.equal(r.ok, true);
});

await test('line-editing mode: escape codes and redraws do not confuse the parser', async () => {
  const link = new Link();
  const t = new ScriptedTransport((text) => {
    const cmd = text.trim();
    if (!cmd) return ['\x1b[0;32mkero> \x1b[0m'];
    const chunks = [];
    let typed = '';
    for (const ch of cmd) {                     // the shell redraws the prompt after every key
      typed += ch;
      chunks.push('\r\x1b[0;32mkero> \x1b[0m' + typed + '\x1b[0K');
    }
    chunks.push('\r\n', 'ok\r\n', '\x1b[0;32mkero> \x1b[0m');
    return chunks;
  });
  await link.attach(t);
  const r = await link.send('servo 0 90');
  assert.deepEqual(r.lines, ['ok']);
});

await test('errors are recognised', async () => {
  const link = new Link();
  const t = new ScriptedTransport((text) => {
    const cmd = text.trim();
    if (!cmd) return ['kero> '];
    return [cmd + '\n', 'servo module is not initialised (power board connected?)\n', 'Command returned non-zero error code: 0x1 (ERROR)\n', 'kero> '];
  });
  await link.attach(t);
  assert.equal((await link.send('servo 0 90')).ok, false);
});

await test('queued commands with the same key collapse to the newest', async () => {
  const link = new Link();
  const t = new ScriptedTransport((text) => (text.trim() ? [text.trim() + '\n', 'ok\n', 'kero> '] : ['kero> ']));
  await link.attach(t);
  const all = [];
  for (let d = 10; d <= 60; d += 10) all.push(link.send('servo 0 ' + d, { key: 'servo0' }));
  const results = await Promise.all(all);
  const sent = t.sent.map((s) => s.trim()).filter((s) => s.startsWith('servo'));
  assert.deepEqual(sent, ['servo 0 10', 'servo 0 60']);     // first went out at once, the rest collapsed
  assert.equal(results.filter((r) => r.superseded).length, 4);
});

await test('a silent device times out instead of hanging', async () => {
  const link = new Link();
  const t = new ScriptedTransport((text) => (text.trim() ? [] : ['kero> ']));
  await link.attach(t);
  await assert.rejects(link.send('bat', { timeout: 60 }), /timeout/);
  assert.equal(link.idle, true);
});

await test('cleanText handles carriage returns', () => {
  assert.equal(cleanText('abc\rkero> x\r\nok\n'), 'kero> x\nok\n');
});

await test('robot against the simulator: connect, move, read', async () => {
  const robot = new Robot();
  await robot.connectSim();
  assert.equal(robot.mode, 'sim');
  assert.equal(robot.state.modules.length, 9);
  assert.ok(robot.state.modules.every((m) => m.ok));
  assert.ok(robot.state.battery.mv > 3500);
  assert.deepEqual(robot.state.servoDeg, [null, null, null, null, null, null]);

  await robot.setJoint('neckA', 120);
  await robot.setJoint('lElbow', 400);              // beyond the limit: clamped to 160
  await robot.syncServos();
  assert.equal(robot.state.servoDeg[0], 120);
  assert.equal(robot.state.servoDeg[3], 160);

  const imu = await robot.readImu();
  assert.ok(Math.abs(imu.accel[2] - 1) < 0.05);
  const mic = await robot.readMic();
  assert.ok(mic.rms < 0);
  const cam = await robot.camShot(3);
  assert.equal(cam.width, 320);

  await robot.setMotors(40, -40);
  assert.deepEqual(robot.state.motor, { a: 40, b: -40 });
  await robot.emergencyStop();
  assert.deepEqual(robot.state.motor, { a: 0, b: 0 });
  await robot.syncServos();
  assert.deepEqual(robot.state.servoDeg, [null, null, null, null, null, null]);
  await robot.disconnect();
  assert.equal(robot.mode, 'none');
});

await test('calibration maps degrees to pulse width and back', async () => {
  const robot = new Robot();
  assert.equal(robot.degToPulse(0, 90), 1500);
  assert.equal(robot.pulseToDeg(0, 1500), 90);
  robot.config.cal[0].invert = true;
  assert.equal(robot.degToPulse(0, 0), 2500);
  assert.equal(robot.pulseToDeg(0, 2500), 0);
});

await test('firmware manifest matches the published binaries', async () => {
  const dir = new URL('../firmware/', import.meta.url);
  const manifest = JSON.parse(readFileSync(new URL('manifest.json', dir), 'utf8'));
  const parts = checkManifest(manifest);
  assert.deepEqual(parts.map((p) => p.offset), [0x0, 0x8000, 0x10000]);
  for (const p of parts) {
    const data = new Uint8Array(readFileSync(new URL(p.file, dir)));
    assert.equal(data.length, p.size, p.file + ' size');
    const digest = await crypto.subtle.digest('SHA-256', data);
    assert.equal(Buffer.from(digest).toString('hex'), p.sha256, p.file + ' sha256');
    if (p.file !== 'partitions.bin') assert.ok(looksLikeEspImage(data), p.file + ' is an ESP image');
  }
});

await test('firmware manifest checks reject bad layouts', async () => {
  const part = (file, offset, size) => ({ file, offset, size });
  assert.throws(() => checkManifest({ parts: [] }));
  assert.throws(() => checkManifest({ chip: 'ESP32', parts: [part('a.bin', 0, 10)] }));
  assert.throws(() => checkManifest({ parts: [part('a.bin', 0, 100), part('b.bin', 50, 10)] }));       // overlap
  assert.throws(() => checkManifest({ parts: [part('../a.bin', 0, 10)] }));                            // path
  assert.throws(() => checkManifest({ parts: [part('a.bin', 0x10000, 16 * 1024 * 1024)] }));           // too big
  assert.deepEqual(checkManifest({ parts: [part('b.bin', 0x8000, 10), part('a.bin', 0, 10)] }).map((p) => p.file), ['a.bin', 'b.bin']);
});

await test('md5 matches the reference values', async () => {
  const bytes = (s) => new TextEncoder().encode(s);
  assert.equal(md5Hex(bytes('')), 'd41d8cd98f00b204e9800998ecf8427e');
  assert.equal(md5Hex(bytes('abc')), '900150983cd24fb0d6963f7d28e17f72');
  assert.equal(md5Hex(bytes('The quick brown fox jumps over the lazy dog')), '9e107d9d372bb6826bd80ea726e37d9c');
  assert.equal(md5Hex(new Uint8Array(1000).fill(0x61)), 'cabe45dcc9ae5b66ba86600cca6b8ba8');
});

console.log(passed + ' passed' + (process.exitCode ? ', some FAILED' : ''));
