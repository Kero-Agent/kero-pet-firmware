// Firmware update from the browser.
//
// The binaries live next to the page in firmware/ (put there by
// tools/publish_firmware.py) and are written with esptool-js, Espressif's
// flasher for Web Serial. The serial port can only have one user, so the
// command link has to be detached before flashFirmware() is called.

const ESPTOOL_URL = new URL('../vendor/esptool-js/bundle.js', import.meta.url).href;
const APP_OFFSET = 0x10000;         // where a single firmware.bin goes
const FLASH_BYTES = 16 * 1024 * 1024;

/** Check a manifest and return its parts sorted by offset. Throws with a readable message. */
export function checkManifest(manifest) {
  if (!manifest || !Array.isArray(manifest.parts) || manifest.parts.length === 0) {
    throw new Error('固件清单里没有文件');
  }
  if (manifest.chip && manifest.chip !== 'ESP32-S3') throw new Error('固件不是给 ESP32-S3 的：' + manifest.chip);
  const parts = manifest.parts.map((p) => ({ ...p })).sort((a, b) => a.offset - b.offset);
  let end = 0;
  for (const p of parts) {
    if (typeof p.file !== 'string' || !/^[\w.-]+\.bin$/.test(p.file)) throw new Error('清单里的文件名不对：' + p.file);
    if (!Number.isInteger(p.offset) || !Number.isInteger(p.size) || p.offset < 0 || p.size <= 0) throw new Error(p.file + ' 的地址或大小不对');
    if (p.offset < end) throw new Error(p.file + ' 和前一个文件的地址重叠');
    end = p.offset + p.size;
  }
  if (end > FLASH_BYTES) throw new Error('固件超出了 16 MB 闪存');
  return parts;
}

/** An ESP application or bootloader image starts with 0xE9. */
export function looksLikeEspImage(data) {
  return data.length > 24 && data[0] === 0xE9;
}

async function sha256Hex(data) {
  const digest = await crypto.subtle.digest('SHA-256', data);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

/** Read firmware/manifest.json: which firmware this page can install. */
export async function fetchManifest(fetchFn = fetch) {
  const res = await fetchFn(new URL('../firmware/manifest.json', import.meta.url), { cache: 'no-store' });
  if (!res.ok) throw new Error('读不到固件清单（HTTP ' + res.status + '）');
  const manifest = await res.json();
  checkManifest(manifest);
  return manifest;
}

/** Download every file of a manifest and verify size and checksum. */
export async function fetchImages(manifest, fetchFn = fetch) {
  const images = [];
  for (const p of checkManifest(manifest)) {
    const res = await fetchFn(new URL('../firmware/' + p.file, import.meta.url), { cache: 'no-store' });
    if (!res.ok) throw new Error('下载 ' + p.file + ' 失败（HTTP ' + res.status + '）');
    const data = new Uint8Array(await res.arrayBuffer());
    if (data.length !== p.size) throw new Error(p.file + ' 大小不对：' + data.length + '，应为 ' + p.size);
    if (p.sha256 && (await sha256Hex(data)) !== p.sha256) throw new Error(p.file + ' 校验和不对，文件可能没下载完整');
    images.push({ name: p.file, address: p.offset, data });
  }
  return images;
}

/** A firmware.bin picked from the disk: only the application is replaced. */
export async function imagesFromFile(file) {
  const data = new Uint8Array(await file.arrayBuffer());
  if (!looksLikeEspImage(data)) throw new Error('这不是 ESP32 的固件文件（要选编译出来的 firmware.bin）');
  if (APP_OFFSET + data.length > FLASH_BYTES) throw new Error('文件太大');
  return [{ name: file.name, address: APP_OFFSET, data }];
}

/** The robot's USB port: the one already allowed for this page, else ask (needs a click). */
export async function pickPort() {
  const filters = [{ usbVendorId: 0x303a }];
  const known = (await navigator.serial.getPorts()).filter((p) => p.getInfo().usbVendorId === 0x303a);
  if (known.length === 1) return known[0];
  return navigator.serial.requestPort({ filters });
}

// The flasher asks the chip for the MD5 of what it wrote; this is the other side of that check.
export function md5Hex(bytes) {
  const K = new Uint32Array(64), S = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
  for (let i = 0; i < 64; i++) K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 4294967296);
  const total = ((bytes.length + 8) >> 6) + 1;
  const msg = new Uint8Array(total * 64);
  msg.set(bytes);
  msg[bytes.length] = 0x80;
  const view = new DataView(msg.buffer);
  view.setUint32(msg.length - 8, (bytes.length * 8) >>> 0, true);
  view.setUint32(msg.length - 4, Math.floor(bytes.length / 536870912), true);
  let a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  const M = new Uint32Array(16);
  for (let off = 0; off < msg.length; off += 64) {
    for (let i = 0; i < 16; i++) M[i] = view.getUint32(off + i * 4, true);
    let a = a0, b = b0, c = c0, d = d0;
    for (let i = 0; i < 64; i++) {
      let f, g;
      if (i < 16) { f = (b & c) | (~b & d); g = i; }
      else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
      else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
      else { f = c ^ (b | ~d); g = (7 * i) % 16; }
      const x = (a + f + K[i] + M[g]) | 0;
      const s = S[(i >> 4) * 4 + (i % 4)];
      a = d; d = c; c = b;
      b = (b + ((x << s) | (x >>> (32 - s)))) | 0;
    }
    a0 = (a0 + a) | 0; b0 = (b0 + b) | 0; c0 = (c0 + c) | 0; d0 = (d0 + d) | 0;
  }
  const out = new DataView(new ArrayBuffer(16));
  [a0, b0, c0, d0].forEach((v, i) => out.setUint32(i * 4, v, true));
  return [...new Uint8Array(out.buffer)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

/**
 * Write the images to the chip and restart it.
 *   port      a closed SerialPort
 *   images    [{ name, address, data }]
 *   onLog     (text) => void, one line of flasher output
 *   onProgress(fraction 0..1, text)
 */
export async function flashFirmware({ port, images, onLog, onProgress }) {
  const { ESPLoader, Transport } = await import(ESPTOOL_URL);
  const terminal = {
    clean() {},
    writeLine(text) { onLog(text); },
    write(text) { if (text.trim()) onLog(text.trim()); },
  };
  const transport = new Transport(port, false);
  const totalBytes = images.reduce((n, im) => n + im.data.length, 0);
  const doneBefore = images.map((_, i) => images.slice(0, i).reduce((n, im) => n + im.data.length, 0));
  try {
    const loader = new ESPLoader({ transport, baudrate: 115200, romBaudrate: 115200, terminal });
    onProgress(0, '正在让芯片进入下载模式…');
    const chip = await loader.main();
    if (!/ESP32-S3/i.test(chip)) throw new Error('连上的芯片是 ' + chip + '，不是 ESP32-S3');
    await loader.writeFlash({
      fileArray: images.map((im) => ({ data: im.data, address: im.address })),
      // "keep": the bootloader image already carries the right flash settings (dio, 80m, 16MB)
      flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
      eraseAll: false,
      compress: true,
      calculateMD5Hash: md5Hex,
      reportProgress(index, written, total) {
        // written / total count compressed bytes of one file: scale to that file's share
        const share = images[index].data.length * (total ? written / total : 1);
        onProgress((doneBefore[index] + share) / totalBytes, '正在写入 ' + images[index].name);
      },
    });
    onProgress(1, '写入完成，正在重启…');
    await loader.after('hard_reset');
  } finally {
    try { await transport.disconnect(); } catch (e) { /* the port may already be gone */ }
  }
}
