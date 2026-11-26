// Command link to the firmware's debug shell.
//
// The shell is a line-based console: we send "servo 0 90\n", it prints the
// output and then a new "kero> " prompt. Link queues commands, sends them one
// at a time and resolves each with the lines printed before the next prompt.
// A transport only has to deliver text both ways: SerialTransport (USB) or
// SimTransport (no hardware).

const ANSI = /\x1b\[[0-9;?]*[ -\/]*[@-~]/g;
const PROMPT = /(^|\n)kero>[ \t]*$/;

/** Remove escape sequences and resolve carriage returns the way a terminal would. */
export function cleanText(raw) {
  return raw
    .replace(ANSI, '')
    .replace(/\r\n/g, '\n')
    .split('\n')
    .map((line) => {
      const i = line.lastIndexOf('\r');
      return i >= 0 ? line.slice(i + 1) : line;
    })
    .join('\n');
}

/** True when a reply looks like a failure report from the shell. */
export function isErrorReply(lines) {
  // The module table of "status" / "init all" may list failed modules: the command itself worked.
  if (lines.length && /^module\s+state/.test(lines[0])) return false;
  return lines.some((l) => /failed:|non-zero error code|not initiali[sz]ed|Unrecognized command|^usage:/i.test(l));
}

export class Link {
  constructor() {
    this.transport = null;
    this.queue = [];
    this.current = null;
    this.raw = '';
    this.sawEscape = false;
    this.listeners = { log: [], state: [] };
    this._settle = null;
  }

  on(event, fn) {
    this.listeners[event].push(fn);
  }

  _emit(event, ...args) {
    for (const fn of this.listeners[event]) fn(...args);
  }

  get connected() {
    return !!this.transport;
  }

  /** Attach an opened transport and wait for the first prompt. */
  async attach(transport) {
    await this.detach();
    this.transport = transport;
    this.raw = '';
    this.sawEscape = false;
    transport.onData = (chunk) => this._onData(chunk);
    transport.onClose = () => this._onClosed();
    this._emit('state');
    // An empty line makes the shell print a fresh prompt.
    try {
      await this.send('', { timeout: 4000, quiet: true });
      return true;
    } catch (e) {
      this._emit('log', 'sys', '没有收到 kero> 提示符：固件可能没在运行，或者这个串口不是机器人。');
      return false;
    }
  }

  async detach() {
    const t = this.transport;
    this.transport = null;
    this._failAll(new Error('disconnected'));
    if (t) {
      t.onData = null;
      t.onClose = null;
      try { await t.close(); } catch (e) { /* already gone */ }
    }
    this._emit('state');
  }

  _onClosed() {
    this.transport = null;
    this._failAll(new Error('disconnected'));
    this._emit('log', 'sys', '连接断开了。');
    this._emit('state');
  }

  _failAll(err) {
    clearTimeout(this._settle);
    const pending = this.current ? [this.current, ...this.queue] : [...this.queue];
    this.current = null;
    this.queue = [];
    for (const item of pending) {
      clearTimeout(item.timer);
      item.reject(err);
    }
  }

  /** True when nothing is waiting: background polling only runs then. */
  get idle() {
    return this.connected && !this.current && this.queue.length === 0;
  }

  /**
   * Queue a command. Options:
   *   key      commands with the same key replace each other while still queued (slider drags)
   *   urgent   jump the queue
   *   timeout  ms to wait for the prompt (default 3000)
   *   quiet    do not show in the console log (background polling)
   * Resolves with { lines, ok, superseded }.
   */
  send(cmd, opts = {}) {
    if (!this.transport) return Promise.reject(new Error('disconnected'));
    return new Promise((resolve, reject) => {
      const item = { cmd, resolve, reject, key: opts.key, timeout: opts.timeout || 3000, quiet: !!opts.quiet };
      if (item.key) {
        const i = this.queue.findIndex((q) => q.key === item.key);
        if (i >= 0) {
          this.queue[i].resolve({ lines: [], ok: true, superseded: true });
          this.queue[i] = item;
          return;
        }
      }
      if (opts.urgent) this.queue.unshift(item); else this.queue.push(item);
      this._pump();
    });
  }

  /** Drop everything that has not been sent yet (emergency stop). */
  clearQueue() {
    for (const item of this.queue) item.resolve({ lines: [], ok: true, superseded: true });
    this.queue = [];
  }

  _pump() {
    if (this.current || !this.transport || this.queue.length === 0) return;
    const item = this.queue.shift();
    this.current = item;
    this.raw = '';
    if (!item.quiet && item.cmd) this._emit('log', 'tx', 'kero> ' + item.cmd);
    item.timer = setTimeout(() => {
      if (this.current !== item) return;
      this.current = null;
      this.raw = '';
      if (!item.quiet) this._emit('log', 'err', '（超时，没有等到回应）');
      item.reject(new Error('timeout'));
      this._pump();
    }, item.timeout);
    this.transport.write(item.cmd + '\n').catch((e) => {
      if (this.current !== item) return;
      clearTimeout(item.timer);
      this.current = null;
      item.reject(e);
      this._pump();
    });
  }

  _onData(chunk) {
    if (chunk.includes('\x1b')) this.sawEscape = true;
    this.raw += chunk;
    if (!this.current) {
      // Unsolicited output (boot messages, log lines from other tasks): show complete lines.
      const text = cleanText(this.raw);
      const cut = text.lastIndexOf('\n');
      if (cut >= 0) {
        for (const line of text.slice(0, cut).split('\n')) {
          if (line.trim() && !/^kero>\s*$/.test(line)) this._emit('log', 'rx', line);
        }
        this.raw = text.slice(cut + 1);
      }
      return;
    }
    if (!PROMPT.test(cleanText(this.raw))) return;
    // With line editing active the shell redraws the prompt while it echoes, so a prompt
    // can show up mid-echo: wait for the output to go quiet before trusting it.
    clearTimeout(this._settle);
    if (this.sawEscape) {
      this._settle = setTimeout(() => this._finish(), 30);
    } else {
      this._finish();
    }
  }

  _finish() {
    const item = this.current;
    if (!item) return;
    const text = cleanText(this.raw);
    if (!PROMPT.test(text)) return;
    clearTimeout(item.timer);
    this.current = null;
    this.raw = '';
    const lines = text
      .replace(PROMPT, '')
      .split('\n')
      .map((l) => l.replace(/\s+$/, ''))
      .filter((l) => l.trim() !== '')
      .filter((l) => !/^kero>/.test(l));           // echoed command lines in line-editing mode
    if (lines.length && lines[0].trim() === item.cmd.trim()) lines.shift();   // plain echo
    const ok = !isErrorReply(lines);
    if (!item.quiet) {
      for (const l of lines) this._emit('log', ok ? 'rx' : 'err', l);
    }
    item.resolve({ lines, ok });
    this._pump();
  }
}

/** USB connection through the browser's Web Serial API (desktop Chrome / Edge). */
export class SerialTransport {
  static get supported() {
    return typeof navigator !== 'undefined' && 'serial' in navigator;
  }

  constructor() {
    this.port = null;
    this.onData = null;
    this.onClose = null;
    this._closing = false;
  }

  /** Must be called from a click: the browser shows its port picker. */
  async open() {
    // 0x303A is Espressif: the ESP32-S3's built-in USB Serial/JTAG port.
    this.port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a }] });
    await this.port.open({ baudRate: 115200 });
    this.writer = this.port.writable.getWriter();
    this.encoder = new TextEncoder();
    this._readLoop();
  }

  async _readLoop() {
    const decoder = new TextDecoder();
    try {
      while (this.port && this.port.readable && !this._closing) {
        this.reader = this.port.readable.getReader();
        try {
          for (;;) {
            const { value, done } = await this.reader.read();
            if (done) break;
            if (value && this.onData) this.onData(decoder.decode(value, { stream: true }));
          }
        } finally {
          this.reader.releaseLock();
        }
      }
    } catch (e) {
      /* unplugged: handled below */
    }
    if (!this._closing && this.onClose) this.onClose();
  }

  write(text) {
    return this.writer.write(this.encoder.encode(text));
  }

  async close() {
    this._closing = true;
    try { if (this.reader) await this.reader.cancel(); } catch (e) { /* ignore */ }
    try { if (this.writer) this.writer.releaseLock(); } catch (e) { /* ignore */ }
    try { if (this.port) await this.port.close(); } catch (e) { /* ignore */ }
    this.port = null;
  }
}
