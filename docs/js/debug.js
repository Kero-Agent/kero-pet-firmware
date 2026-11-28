// Debug page: one panel per hardware module plus the serial console.

import { JOINTS, JOINT_KEYS, SERVO_COUNT, parseI2cScan } from './robot.js';
import { drawChart } from './chart.js';
import { PICTURE_SIZE, rgbaToBase64 } from './picture.js';
import { fetchManifest, fetchImages, imagesFromFile, pickPort, flashFirmware } from './flash.js';

const $ = (id) => document.getElementById(id);
const KNOWN_I2C = { '0x40': 'PCA9685（电源板）', '0x6A': 'LSM6DS3TR-C', '0x70': 'PCA9685 广播地址' };

export function initDebug(robot) {
  const state = robot.state;

  // ---------------- modules / system ----------------
  function renderModules() {
    const box = $('module-list');
    if (!state.modules.length) {
      box.innerHTML = '<p class="hint">连接后显示各模块是否启动成功。</p>';
      return;
    }
    box.innerHTML = state.modules.map((m) =>
      '<div class="module ' + (m.ok ? 'ok' : 'bad') + '"><span class="name">' + m.name + '</span><span class="state">' + (m.ok ? 'ok' : '失败') + '</span></div>'
    ).join('');
    const cam = state.modules.find((m) => m.id === 'camera');
    const pill = $('cam-state');
    pill.textContent = !cam ? '—' : cam.ok ? '正常' : '初始化失败';
    pill.className = 'pill' + (!cam ? ' idle' : cam.ok ? '' : ' bad');
    $('cam-msg').className = 'note' + (cam && !cam.ok ? ' bad' : '');
    $('cam-msg').textContent = cam && !cam.ok
      ? '没读到传感器。检查排线方向，以及 2.8 V 和 1.8 V 两路电压。'
      : '抓帧可以确认传感器在出图，并给出帧率。';
  }
  $('retry-modules').onclick = () => robot.retryModules();
  $('reboot').onclick = () => robot.reboot();

  // ---------------- firmware update ----------------
  let manifest = null;
  let flashing = false;
  const serialOk = typeof navigator !== 'undefined' && 'serial' in navigator;
  function fwSay(text, bad) {
    $('fw-msg').hidden = !text;
    $('fw-msg').textContent = text;
    $('fw-msg').className = 'note' + (bad ? ' bad' : '');
  }
  function fwButtons() {
    $('fw-flash').disabled = flashing || !manifest || !serialOk;
    $('fw-file-btn').disabled = flashing || !serialOk;
  }
  fetchManifest().then((m) => {
    manifest = m;
    $('fw-version').textContent = '网页上的固件：' + m.version + '（' + m.built + ' 编译）';
    fwButtons();
  }).catch((e) => {
    $('fw-version').textContent = '网页上没有可用的固件：' + e.message;
  });
  if (!serialOk) fwSay('烧录需要电脑版 Chrome 或 Edge。');
  fwButtons();

  /** getImages() returns what to write; it runs after the port is chosen. */
  async function runFlash(getImages, what) {
    if (flashing) return;
    let port;
    try {
      port = await pickPort();          // first thing: the port picker needs the click
    } catch (e) {
      fwSay(e && e.name === 'NotFoundError' ? '没有选择串口。' : '打不开串口选择：' + e.message, true);
      return;
    }
    if (!window.confirm('把' + what + '写进机器人？\n\n大约需要一分钟。过程中电机和舵机会停下，不要拔 USB 线。')) return;
    flashing = true;
    fwButtons();
    $('fw-meter').hidden = false;
    $('fw-bar').style.width = '0%';
    const log = (text) => robot.link.log('sys', '[烧录] ' + text);
    try {
      if (robot.mode === 'real') {
        await robot.emergencyStop();
        await robot.disconnect();       // the port can only have one user
      }
      fwSay('正在准备固件…');
      const images = await getImages();
      log(what + '：' + images.map((im) => im.name + ' → 0x' + im.address.toString(16)).join('，'));
      await flashFirmware({
        port, images, onLog: log,
        onProgress(fraction, text) {
          $('fw-bar').style.width = Math.round(fraction * 100) + '%';
          fwSay(text + ' ' + Math.round(fraction * 100) + '%');
        },
      });
      log('完成');
      fwSay('烧录完成，正在重新连接…');
      await new Promise((r) => setTimeout(r, 2000));    // let the new firmware boot
      try {
        const ok = await robot.connectSerial(port);
        fwSay(ok ? '烧录完成，已经连上新固件。' : '烧录完成，但没等到 kero> 提示符。按一下 RST，再点右上角的"真机"。', !ok);
      } catch (e) {
        fwSay('烧录完成。点右上角的"真机"重新连接。');
      }
    } catch (e) {
      log('失败：' + e.message);
      fwSay('烧录失败：' + e.message + '。如果一直连不上：按住 BOOT，按一下 RST，松开 BOOT，再点一次烧录。', true);
    } finally {
      flashing = false;
      fwButtons();
    }
  }
  $('fw-flash').onclick = () => runFlash(() => fetchImages(manifest), '最新固件 ' + manifest.version);
  $('fw-file-btn').onclick = () => $('fw-file').click();
  $('fw-file').addEventListener('change', (ev) => {
    const file = ev.target.files[0];
    ev.target.value = '';
    if (file) runFlash(() => imagesFromFile(file), '本地文件 ' + file.name);
  });

  const RESET_NAMES = { 'power on': '上电', 'software reset': '软件重启', panic: '程序崩溃', brownout: '欠压', USB: 'USB', 'task watchdog': '任务看门狗', 'interrupt watchdog': '中断看门狗' };
  function renderSys() {
    const s = state.sys;
    if (!s) return;
    const t = Math.floor(s.uptimeS || 0);
    const two = (n) => String(n).padStart(2, '0');
    $('sys-uptime').textContent = two(Math.floor(t / 3600)) + ':' + two(Math.floor(t / 60) % 60) + ':' + two(t % 60);
    $('sys-reset').textContent = RESET_NAMES[s.reset] || s.reset || '—';
    $('sys-heap').textContent = s.heapFree ? Math.round(s.heapFree / 1024) + ' KB' : '—';
    $('sys-psram').textContent = s.psramFree ? (s.psramFree / 1048576).toFixed(1) + ' MB' : '—';
    $('sys-idf').textContent = s.idf || '—';
  }

  // ---------------- servos ----------------
  const table = $('servo-table');
  const rows = [];
  for (let id = 0; id < SERVO_COUNT; id++) {
    const row = document.createElement('div');
    row.className = 'servo-row';
    row.innerHTML =
      '<select aria-label="通道 ' + id + ' 接的关节">' + JOINT_KEYS.map((k) => '<option value="' + k + '">' + JOINTS[k].name + '</option>').join('') + '</select>'
      + '<span class="mono muted">CH' + id + '</span>'
      + '<input type="range" min="0" max="180" step="1" aria-label="通道 ' + id + ' 角度">'
      + '<span class="mono right" data-r="deg"></span>'
      + '<span class="mono right muted small" data-r="us"></span>'
      + '<span class="mono muted small" data-r="limit"></span>'
      + '<div class="servo-actions">'
      + '<button type="button" class="btn small" data-a="release">释放</button>'
      + '<button type="button" class="btn small" data-a="sweep">扫动</button>'
      + '<button type="button" class="btn small" data-a="cal">校准</button></div>';
    table.appendChild(row);
    const r = {
      select: row.querySelector('select'), slider: row.querySelector('input'),
      deg: row.querySelector('[data-r="deg"]'), us: row.querySelector('[data-r="us"]'), limit: row.querySelector('[data-r="limit"]'),
    };
    rows.push(r);
    r.slider.addEventListener('input', () => robot.setServo(id, Number(r.slider.value)));
    r.select.addEventListener('change', () => {
      // Swap, so every joint stays assigned to exactly one channel.
      const other = robot.config.assign.indexOf(r.select.value);
      const mine = robot.config.assign[id];
      if (other >= 0 && other !== id) robot.config.assign[other] = mine;
      robot.config.assign[id] = r.select.value;
      robot.saveConfig();
    });
    row.querySelector('[data-a="release"]').onclick = () => robot.releaseServo(id);
    row.querySelector('[data-a="sweep"]').onclick = () => robot.sweepServo(id);
    row.querySelector('[data-a="cal"]').onclick = () => openCalibration(id);
  }

  function renderServos() {
    rows.forEach((r, id) => {
      const c = robot.config.cal[id];
      const d = state.servoDeg[id];
      r.select.value = robot.config.assign[id];
      r.slider.min = c.lo; r.slider.max = c.hi;
      if (document.activeElement !== r.slider) r.slider.value = d === null ? c.home : d;
      r.deg.textContent = d === null ? '—' : d + '°';
      r.deg.classList.toggle('released', d === null);
      r.us.textContent = d === null ? '已释放' : robot.degToPulse(id, d) + ' µs';
      r.limit.textContent = c.lo + '°–' + c.hi + '°';
    });
  }
  $('servo-home').onclick = () => robot.homeAll();
  $('servo-release').onclick = () => robot.releaseAll();
  $('servo-export').onclick = () => download('kero-servo-config.json', JSON.stringify(robot.config, null, 2));

  // calibration dialog
  const dialog = $('cal-dialog');
  let calId = 0;
  function openCalibration(id) {
    calId = id;
    const c = robot.config.cal[id];
    $('cal-title').textContent = '校准 CH' + id + ' · ' + JOINTS[robot.config.assign[id]].name;
    $('cal-min').value = c.min; $('cal-max').value = c.max; $('cal-trim').value = c.trim;
    $('cal-home').value = c.home; $('cal-lo').value = c.lo; $('cal-hi').value = c.hi;
    $('cal-invert').checked = c.invert;
    dialog.showModal();
  }
  $('cal-cancel').onclick = () => dialog.close();
  $('cal-form').addEventListener('submit', (ev) => {
    ev.preventDefault();
    const num = (id, lo, hi, fallback) => {
      const v = Number($(id).value);
      return Number.isFinite(v) ? Math.max(lo, Math.min(hi, Math.round(v))) : fallback;
    };
    const c = robot.config.cal[calId];
    const min = num('cal-min', 400, 2600, c.min), max = num('cal-max', 400, 2600, c.max);
    const lo = num('cal-lo', 0, 180, c.lo), hi = num('cal-hi', 0, 180, c.hi);
    if (max <= min || hi <= lo) return;          // the form stays open: the values make no sense
    Object.assign(c, { min, max, lo, hi, trim: num('cal-trim', -300, 300, c.trim), invert: $('cal-invert').checked });
    c.home = Math.max(lo, Math.min(hi, num('cal-home', 0, 180, c.home)));
    dialog.close();
    robot.applyCalibration(calId);
    renderServos();
  });

  // ---------------- motors ----------------
  const motA = $('mot-a'), motB = $('mot-b');
  function motorInput(changed) {
    if ($('mot-link').checked) {
      (changed === motA ? motB : motA).value = changed.value;
    }
    robot.setMotors(Number(motA.value), Number(motB.value));
  }
  motA.addEventListener('input', () => motorInput(motA));
  motB.addEventListener('input', () => motorInput(motB));
  $('mot-stop').onclick = () => robot.stopMotors(false);
  $('mot-brake').onclick = () => robot.stopMotors(true);
  $('mot-wdt').addEventListener('change', (ev) => robot.setMotorWatchdog(ev.target.checked ? 500 : 0));
  function renderMotors() {
    const fmt = (v) => (v > 0 ? '+' : '') + v + ' %';
    $('mot-a-val').textContent = fmt(state.motor.a);
    $('mot-b-val').textContent = fmt(state.motor.b);
    if (document.activeElement !== motA) motA.value = state.motor.a;
    if (document.activeElement !== motB) motB.value = state.motor.b;
  }

  // ---------------- IMU / battery ----------------
  $('imu-cal').onclick = () => robot.calibrateGyro();
  function renderImu() {
    const s = state.imu;
    if (!s) return;
    ['ax', 'ay', 'az'].forEach((k, i) => { $('imu-' + k).textContent = s.accel[i].toFixed(3) + ' g'; });
    ['gx', 'gy', 'gz'].forEach((k, i) => { $('imu-' + k).textContent = s.gyro[i].toFixed(1) + ' °/s'; });
    $('imu-temp').textContent = '温度 ' + s.temp.toFixed(1) + ' °C';
    const h = state.imuHistory;
    drawChart($('imu-chart'), [
      { values: h.map((a) => a[0]), color: '--accent' },
      { values: h.map((a) => a[1]), color: '--blue' },
      { values: h.map((a) => a[2]), color: '--orange' },
    ], { min: -1.5, max: 1.5, unit: ' g', capacity: 150 });
  }
  function renderBattery() {
    const b = state.battery;
    if (!b) return;
    $('bat-v').textContent = (b.mv / 1000).toFixed(2);
    const pill = $('bat-pct');
    if (b.absent) {
      pill.textContent = '没有电池电压';
      pill.className = 'pill warn';
      $('bat-note').textContent = '读数接近 0：电源开关断开，或者没接电源板。USB 供电时主控照常工作，舵机和电机没电。';
    } else {
      pill.textContent = b.percent === null ? '—' : '约 ' + b.percent + ' %';
      pill.className = 'pill';
      $('bat-note').textContent = '舵机动作时电压会下沉，这里能直接看到。';
    }
    drawChart($('bat-chart'), [{ values: state.batteryHistory, color: '--accent' }], { unit: ' V', minSpan: 0.3, capacity: 120 });
  }

  // ---------------- sound ----------------
  const intOf = (id, lo, hi, fallback) => {
    const v = Math.round(Number($(id).value));
    return Number.isFinite(v) ? Math.max(lo, Math.min(hi, v)) : fallback;
  };
  $('spk-play').onclick = () => robot.tone(intOf('spk-hz', 50, 7000, 1000), intOf('spk-ms', 20, 5000, 300));
  $('spk-vol').addEventListener('input', (ev) => {
    $('spk-vol-val').textContent = ev.target.value + ' %';
    robot.setVolume(ev.target.value);
  });
  $('mic-echo').onclick = () => robot.micEcho(2000);
  function renderMic() {
    const m = state.mic;
    if (!m) return;
    $('mic-db').textContent = m.rms.toFixed(0) + ' dBFS';
    $('mic-bar').style.width = Math.max(0, Math.min(100, (m.rms + 90) / 0.9)) + '%';
  }

  // ---------------- display ----------------
  $('lcd-bl').addEventListener('input', (ev) => {
    $('lcd-bl-label').textContent = '背光 ' + ev.target.value + ' %';
    robot.setBacklight(ev.target.value);
  });
  $('lcd-inv').addEventListener('change', (ev) => robot.lcd('invert ' + (ev.target.checked ? 1 : 0)));
  $('lcd-test').onclick = () => robot.lcd('test');
  $('lcd-color').addEventListener('change', (ev) => {
    if (ev.target.value) robot.lcd('fill ' + ev.target.value);
    ev.target.value = '';
  });
  $('lcd-lines').onclick = () => robot.lcd('lines');
  $('lcd-clk').addEventListener('change', async (ev) => {
    if (!ev.target.value) return;
    const r = await robot.cmd('lcd clk ' + ev.target.value, { timeout: 8000 });
    ev.target.value = '';
    if (r.ok) await robot.face();
  });
  $('lcd-seq').addEventListener('change', async (ev) => {
    const r = await robot.cmd('lcd seq ' + ev.target.value, { timeout: 8000 });
    if (r.ok) await robot.face();
  });
  const screenIcon = document.querySelector('.round-screen');
  $('lcd-off').onclick = () => { robot.lcd('off'); screenIcon.classList.add('off'); };
  $('lcd-on').onclick = () => { robot.lcd('on'); screenIcon.classList.remove('off'); };

  // picture for the screen: choose, zoom, drag, send
  const picCv = $('pic-cv'), picCtx = picCv.getContext('2d', { willReadFrequently: true });
  const picView = $('pic-view');
  let picture = null, picZoom = 1, picX = 0, picY = 0, picBusy = false;
  function picSay(text, bad) {
    $('pic-msg').hidden = !text;
    $('pic-msg').textContent = text;
    $('pic-msg').className = 'note' + (bad ? ' bad' : '');
  }
  function picButtons() {
    $('pic-send').disabled = !picture || picBusy || !robot.connected;
  }
  function drawPicture() {
    picCtx.fillStyle = '#000';
    picCtx.fillRect(0, 0, PICTURE_SIZE, PICTURE_SIZE);
    if (!picture) return;
    const k = Math.max(PICTURE_SIZE / picture.width, PICTURE_SIZE / picture.height) * picZoom;   // cover the screen
    const w = picture.width * k, h = picture.height * k;
    const maxX = (w - PICTURE_SIZE) / 2, maxY = (h - PICTURE_SIZE) / 2;
    picX = Math.min(maxX, Math.max(-maxX, picX));
    picY = Math.min(maxY, Math.max(-maxY, picY));
    picCtx.imageSmoothingQuality = 'high';
    picCtx.drawImage(picture, (PICTURE_SIZE - w) / 2 + picX, (PICTURE_SIZE - h) / 2 + picY, w, h);
  }
  async function loadPicture(file) {
    if (!file || !file.type.startsWith('image/')) return;
    try {
      picture = await createImageBitmap(file);
    } catch (e) {
      picSay('这个文件打不开：' + e.message, true);
      return;
    }
    picZoom = 1; picX = picY = 0;
    $('pic-zoom').value = 1;
    $('pic-zoom').disabled = false;
    $('pic-hint').hidden = true;
    picSay('');
    drawPicture();
    picButtons();
  }
  $('pic-pick').onclick = () => $('pic-file').click();
  $('pic-file').addEventListener('change', (ev) => { loadPicture(ev.target.files[0]); ev.target.value = ''; });
  $('pic-zoom').addEventListener('input', (ev) => { picZoom = Number(ev.target.value); drawPicture(); });
  picView.addEventListener('dragover', (ev) => { ev.preventDefault(); picView.classList.add('drop'); });
  picView.addEventListener('dragleave', () => picView.classList.remove('drop'));
  picView.addEventListener('drop', (ev) => {
    ev.preventDefault();
    picView.classList.remove('drop');
    loadPicture(ev.dataTransfer.files[0]);
  });
  let picDrag = null;
  picView.addEventListener('pointerdown', (ev) => {
    if (!picture) return;
    picDrag = { x: ev.clientX, y: ev.clientY, picX, picY };
    picView.setPointerCapture(ev.pointerId);
  });
  picView.addEventListener('pointermove', (ev) => {
    if (!picDrag) return;
    const k = PICTURE_SIZE / picView.clientWidth;
    picX = picDrag.picX + (ev.clientX - picDrag.x) * k;
    picY = picDrag.picY + (ev.clientY - picDrag.y) * k;
    drawPicture();
  });
  picView.addEventListener('pointerup', () => { picDrag = null; });
  picView.addEventListener('pointercancel', () => { picDrag = null; });

  $('pic-send').onclick = async () => {
    if (!picture || picBusy) return;
    picBusy = true;
    picButtons();
    $('pic-meter').hidden = false;
    $('pic-bar').style.width = '0%';
    const t0 = Date.now();
    try {
      const data = rgbaToBase64(picCtx.getImageData(0, 0, PICTURE_SIZE, PICTURE_SIZE).data);
      await robot.sendPicture(data, (f) => {
        $('pic-bar').style.width = Math.round(f * 100) + '%';
        picSay('正在发送… ' + Math.round(f * 100) + '%');
      });
      picSay('发送完成，用时 ' + ((Date.now() - t0) / 1000).toFixed(1) + ' 秒。'
        + (robot.mode === 'sim' ? '现在是模拟模式，真机屏幕上不会有变化。' : ''));
    } catch (e) {
      picSay(e.message, true);
    } finally {
      picBusy = false;
      picButtons();
    }
  };
  $('lcd-face').onclick = () => robot.face();
  $('pic-again').onclick = () => robot.showPictureAgain();
  drawPicture();

  // ---------------- camera ----------------
  $('cam-init').onclick = () => robot.initModule('camera');
  $('cam-shot').onclick = async () => {
    const c = await robot.camShot(10);
    if (!c) return;
    $('cam-view').style.background = 'rgb(' + c.rgb.join(',') + ')';
    $('cam-view-text').textContent = c.width + '×' + c.height + (c.fps ? '，' + c.fps.toFixed(1) + ' 帧/秒' : '') + '。背景是这一帧的平均颜色，遮住镜头应该变暗。';
  };
  $('cam-preview').onclick = () => robot.cmd('preview 10', { timeout: 16000 });
  const flip = () => robot.cmd('cam flip ' + ($('cam-h').checked ? 1 : 0) + ' ' + ($('cam-v').checked ? 1 : 0));
  $('cam-h').addEventListener('change', flip);
  $('cam-v').addEventListener('change', flip);

  // ---------------- I2C ----------------
  $('i2c-scan').onclick = async () => {
    const r = await robot.cmd('i2c scan', { timeout: 8000 });
    const list = parseI2cScan(r.lines);
    $('i2c-list').innerHTML = list.length
      ? list.map((d) => {
        const name = KNOWN_I2C[d.addr.toUpperCase().replace('0X', '0x')] || d.name;
        return '<div class="i2c-item' + (name ? '' : ' unknown') + '"><span>' + d.addr + '</span><span>' + (name || '未知设备，可能是触摸芯片') + '</span></div>';
      }).join('')
      : '<p class="hint">总线上没有设备应答。</p>';
  };
  const i2cArg = (id) => $(id).value.trim().replace(/[^0-9a-fA-Fx]/g, '');
  $('i2c-read').onclick = async () => {
    const r = await robot.cmd('i2c r ' + i2cArg('i2c-addr') + ' ' + i2cArg('i2c-reg'));
    const m = /:\s*([0-9A-Fa-f]{2})/.exec(r.lines.join(' '));
    $('i2c-val').value = m ? '0x' + m[1] : '';
  };
  $('i2c-write').onclick = () => {
    if (i2cArg('i2c-val')) robot.cmd('i2c w ' + i2cArg('i2c-addr') + ' ' + i2cArg('i2c-reg') + ' ' + i2cArg('i2c-val'));
  };

  // ---------------- console ----------------
  const log = $('con-log');
  const history = [];
  let historyAt = 0;
  robot.link.on('log', (kind, text) => {
    const span = document.createElement('span');
    span.className = kind;
    span.textContent = text + '\n';
    const stick = log.scrollTop + log.clientHeight >= log.scrollHeight - 30;
    log.appendChild(span);
    while (log.childNodes.length > 1500) log.removeChild(log.firstChild);
    if (stick) log.scrollTop = log.scrollHeight;
  });
  $('con-form').addEventListener('submit', (ev) => {
    ev.preventDefault();
    const input = $('con-in');
    const text = input.value.trim();
    if (!text) return;
    history.push(text);
    historyAt = history.length;
    input.value = '';
    robot.cmd(text, { timeout: 20000 });
  });
  $('con-in').addEventListener('keydown', (ev) => {
    if (ev.key !== 'ArrowUp' && ev.key !== 'ArrowDown') return;
    ev.preventDefault();
    historyAt = Math.max(0, Math.min(history.length, historyAt + (ev.key === 'ArrowUp' ? -1 : 1)));
    ev.target.value = history[historyAt] || '';
  });
  $('con-clear').onclick = () => { log.textContent = ''; };
  $('con-save').onclick = () => download('kero-console.log', log.textContent);
  $('con-toggle').onclick = (ev) => {
    const collapsed = $('console').classList.toggle('collapsed');
    ev.target.textContent = collapsed ? '展开' : '收起';
  };

  // ---------------- wiring ----------------
  robot.subscribe((topic) => {
    if (topic === 'modules' || topic === 'conn') renderModules();
    if (topic === 'conn') picButtons();
    if (topic === 'sys') renderSys();
    if (topic === 'servo' || topic === 'config' || topic === 'conn') renderServos();
    if (topic === 'motor') renderMotors();
    if (topic === 'imu') renderImu();
    if (topic === 'battery') renderBattery();
    if (topic === 'mic') renderMic();
  });
  renderModules(); renderServos(); renderMotors();

  return {
    /** Which background readings this page needs right now. */
    wants: () => ({ imu: true, battery: true, sys: true, mic: $('mic-watch').checked, showInConsole: $('con-poll').checked }),
    /** Called when the page becomes visible: canvases have a size only then. */
    shown: () => { renderImu(); renderBattery(); },
  };
}

function download(name, text) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([text], { type: 'text/plain' }));
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
