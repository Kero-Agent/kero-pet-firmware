// Pictures for the robot's round screen: conversion to what the firmware
// wants, and the upload conversation of the "img recv" command
// (see main/app_image.c for the protocol).

export const PICTURE_SIZE = 360;

/** RGBA pixels (canvas ImageData) to RGB565, high byte first, as base64 text. */
export function rgbaToBase64(rgba, size = PICTURE_SIZE) {
  let bin = '';
  const line = new Uint8Array(size * 2);
  for (let row = 0; row < size; row++) {
    for (let x = 0; x < size; x++) {
      const i = (row * size + x) * 4;
      const v = ((rgba[i] & 0xF8) << 8) | ((rgba[i + 1] & 0xFC) << 3) | (rgba[i + 2] >> 3);
      line[2 * x] = v >> 8;
      line[2 * x + 1] = v & 0xFF;
    }
    bin += String.fromCharCode.apply(null, line);
  }
  return btoa(bin);
}

const errorLine = (text) => (/ERR[^\n]*/.exec(text) || ['ERR'])[0];

/**
 * Send a picture. `io` comes from Link.exclusive(): the shell is not asked
 * anything else meanwhile. The board acknowledges every block with a dot,
 * because its USB console can only buffer 256 bytes.
 */
export async function uploadPicture(io, base64, onProgress = () => {}) {
  const prompt = (t) => /(^|\n)kero>[ \t]*$/.test(t);
  io.clear();
  await io.write('img recv\n');
  await io.waitFor((t) => /READY \d+\s/.test(t) || /ERR/.test(t) || (prompt(t) && /Unrecognized/.test(t)), 5000, '开始传输');
  const ready = /READY (\d+)/.exec(io.text());
  if (!ready) {
    if (/Unrecognized/.test(io.text())) throw new Error('这个固件还不支持上传图片，先在左边"固件更新"里烧录最新固件');
    await io.waitFor(prompt, 2000, '等待提示符').catch(() => {});
    throw new Error('机器人拒绝接收：' + errorLine(io.text()));
  }
  const chunk = Number(ready[1]);
  for (let pos = 0; pos < base64.length; pos += chunk) {
    io.clear();
    await io.write(base64.slice(pos, pos + chunk));
    await io.waitFor((t) => t.includes('.') || /ERR/.test(t), 5000, '传输中断');
    if (/ERR/.test(io.text())) {
      await io.waitFor(prompt, 2000, '等待提示符').catch(() => {});
      throw new Error('机器人报错：' + errorLine(io.text()));
    }
    onProgress(Math.min(1, (pos + chunk) / base64.length));
  }
  await io.waitFor((t) => /DONE/.test(t) || /ERR/.test(t), 5000, '结束传输');
  const failed = !/DONE/.test(io.text());
  const text = io.text();
  await io.waitFor(prompt, 3000, '等待提示符').catch(() => {});
  if (failed) throw new Error('机器人报错：' + errorLine(text));
}
