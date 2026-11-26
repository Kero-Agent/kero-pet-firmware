// Entry point: top bar, switching between the two pages, connection handling.

import { Robot } from './robot.js';
import { initDebug } from './debug.js';
import { initShowcase } from './showcase.js';

const $ = (id) => document.getElementById(id);
const robot = new Robot();
const pages = { debug: initDebug(robot), showcase: initShowcase(robot) };

// ---------------- pages ----------------
// One document, two views: the USB connection survives switching between them.
function currentView() {
  return location.hash === '#debug' ? 'debug' : 'showcase';
}
function showView() {
  const view = currentView();
  for (const name of Object.keys(pages)) $('view-' + name).hidden = name !== view;
  for (const a of document.querySelectorAll('.tabs a')) {
    if (a.dataset.view === view) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
  }
  pages[view].shown();
}
window.addEventListener('hashchange', showView);

// ---------------- connection ----------------
function renderConnection() {
  const chip = $('conn-chip');
  const labels = { sim: ['sim', '模拟模式'], real: ['ok', 'USB 已连接'], none: ['bad', '未连接'] };
  const [cls, text] = labels[robot.mode];
  chip.className = 'chip ' + cls;
  $('conn-text').textContent = text;
  $('mode-sim').setAttribute('aria-pressed', String(robot.mode === 'sim'));
  $('mode-real').setAttribute('aria-pressed', String(robot.mode === 'real'));

  const banner = $('banner');
  if (robot.mode === 'sim') {
    banner.hidden = false;
    banner.textContent = '现在是模拟模式：页面里的机器人是假的，用来看界面和动作。接好 USB 后点右上角的"真机"连接真正的 Kero。'
      + (Robot.serialSupported ? '' : ' 这个浏览器不支持 USB 串口，连接真机请用电脑版 Chrome 或 Edge。');
  } else if (robot.mode === 'none') {
    banner.hidden = false;
    banner.textContent = '没有连接。点"真机"通过 USB 连接，或者点"模拟"先看看界面。';
  } else {
    banner.hidden = true;
  }
  renderInfo();
}

function renderInfo() {
  const parts = [];
  if (robot.connected && robot.state.sys && robot.state.sys.idf) parts.push('固件 ' + robot.state.sys.idf);
  const b = robot.state.battery;
  if (robot.connected && b) parts.push(b.absent ? '没接电池' : '电池 ' + (b.mv / 1000).toFixed(2) + ' V');
  $('conn-info').textContent = parts.join(' · ');
}

async function useSim() {
  pages.showcase.cancel();
  await robot.connectSim();
  if ($('mot-wdt').checked) await robot.setMotorWatchdog(500);
}

async function useReal() {
  if (!Robot.serialSupported) {
    $('banner').hidden = false;
    $('banner').textContent = '这个浏览器不支持 USB 串口。请用电脑版 Chrome 或 Edge 打开这个页面。';
    return;
  }
  pages.showcase.cancel();
  try {
    const ok = await robot.connectSerial();
    if (ok && $('mot-wdt').checked) await robot.setMotorWatchdog(500);
  } catch (e) {
    // The user closed the port picker, or the port is in use by another program.
    if (!robot.connected) {
      await robot.connectSim();
      $('banner').textContent = '没有连上真机（' + (e && e.name === 'NotFoundError' ? '没有选择串口' : '串口打不开，可能被别的程序占用') + '），先回到模拟模式。';
    }
  }
}

$('mode-sim').onclick = useSim;
$('mode-real').onclick = useReal;

// ---------------- emergency stop ----------------
function emergencyStop() {
  pages.showcase.cancel();
  if (robot.connected) robot.emergencyStop();
}
$('estop').onclick = emergencyStop;
window.addEventListener('keydown', (ev) => { if (ev.key === 'Escape' && !document.querySelector('dialog[open]')) emergencyStop(); });

// ---------------- background work ----------------
robot.subscribe((topic) => {
  if (topic === 'conn') renderConnection();
  if (topic === 'battery' || topic === 'sys') renderInfo();
});

setInterval(() => {
  if (!robot.connected || document.hidden) return;
  // Keep the motor watchdog fed while the motors are meant to run: if this page or the
  // cable goes away, the firmware stops them by itself.
  const m = robot.state.motor;
  if (m.a !== 0 || m.b !== 0) robot.setMotors(m.a, m.b, { quiet: true });
  robot.poll(pages[currentView()].wants());
}, 200);

// Motors must not keep running when the tab is hidden or closed.
document.addEventListener('visibilitychange', () => { if (document.hidden && robot.connected) robot.stopMotors(); });

showView();
renderConnection();
useSim();
