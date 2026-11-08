#!/usr/bin/env node
// Runs a WebGPU test page in headless Chrome and turns its console output
// into an exit code. Works for webgpu_browser_test (PASS/FAIL lines) and for
// webgpu_gtest_suite (GoogleTest output); both end with
// "CAMPELLO_WEBGPU_TESTS_DONE failures=<n> [tests=<n>]". No npm dependencies: it serves the files with
// node:http and drives Chrome through the DevTools protocol (Node >= 22 for
// the global WebSocket).
//
// Usage:
//   node run_in_browser.mjs <path/to/webgpu_browser_test.html> [--timeout=<seconds>]
//
// Environment:
//   CHROME_PATH   Chrome/Chromium binary (default: common install locations)
//   CHROME_FLAGS  Extra Chrome flags, space-separated. On GPU-less Linux CI use
//                 "--use-webgpu-adapter=swiftshader --enable-features=Vulkan
//                  --use-vulkan=swiftshader --no-sandbox".
//
// Exit codes: 0 all checks passed, 1 a check failed or the page crashed,
// 2 timeout, 3 setup error (Chrome not found, page not found, ...).

import { spawn, execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, extname, join, resolve } from 'node:path';

const args = process.argv.slice(2);
const pagePath = args.find((a) => !a.startsWith('--'));
const timeoutArg = args.find((a) => a.startsWith('--timeout='));
const timeoutMs = (timeoutArg ? Number(timeoutArg.split('=')[1]) : 120) * 1000;

function setupError(message) {
  console.error(`run_in_browser: ${message}`);
  process.exit(3);
}

if (!pagePath || !existsSync(pagePath)) {
  setupError(`test page not found: ${pagePath ?? '(none given)'}`);
}

function findChrome() {
  if (process.env.CHROME_PATH) return process.env.CHROME_PATH;
  const candidates = [
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    '/Applications/Chromium.app/Contents/MacOS/Chromium',
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  ];
  for (const c of candidates) if (existsSync(c)) return c;
  for (const name of ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser']) {
    try {
      return execFileSync('which', [name], { encoding: 'utf8' }).trim();
    } catch {}
  }
  return null;
}

const chromePath = findChrome();
if (!chromePath) setupError('Chrome not found; set CHROME_PATH');

// --- Static file server -----------------------------------------------------
const root = dirname(resolve(pagePath));
const mimeTypes = {
  '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
  '.wasm': 'application/wasm', '.json': 'application/json',
};
const server = createServer((req, res) => {
  const urlPath = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
  const filePath = join(root, urlPath);
  if (!filePath.startsWith(root) || !existsSync(filePath)) {
    res.writeHead(404);
    res.end();
    return;
  }
  res.writeHead(200, { 'Content-Type': mimeTypes[extname(filePath)] ?? 'application/octet-stream' });
  res.end(readFileSync(filePath));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const pageUrl = `http://127.0.0.1:${server.address().port}/${basename(pagePath)}`;

// --- Chrome -----------------------------------------------------------------
const profileDir = mkdtempSync(join(tmpdir(), 'campello-webgpu-'));
const extraFlags = (process.env.CHROME_FLAGS ?? '').split(/\s+/).filter(Boolean);
const chrome = spawn(chromePath, [
  '--headless=new',
  `--user-data-dir=${profileDir}`,
  '--no-first-run',
  '--no-default-browser-check',
  '--enable-unsafe-webgpu',
  '--remote-debugging-port=0',
  ...extraFlags,
  'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe'] });

let finished = false;
function finish(code, message) {
  if (finished) return;
  finished = true;
  if (message) console.log(message);
  chrome.kill();
  server.close();
  try { rmSync(profileDir, { recursive: true, force: true }); } catch {}
  process.exit(code);
}

chrome.on('exit', (code) => {
  if (!finished) finish(3, `run_in_browser: Chrome exited early (code ${code})`);
});

// Chrome prints the DevTools endpoint on stderr.
const wsUrl = await new Promise((resolveUrl) => {
  let buffer = '';
  chrome.stderr.on('data', (chunk) => {
    buffer += chunk.toString();
    const match = buffer.match(/DevTools listening on (ws:\/\/\S+)/);
    if (match) resolveUrl(match[1]);
  });
  setTimeout(() => resolveUrl(null), 30000);
});
if (!wsUrl) finish(3, 'run_in_browser: Chrome did not expose a DevTools endpoint');

const browser = new WebSocket(wsUrl);
await new Promise((r, j) => { browser.onopen = r; browser.onerror = j; });

let nextId = 1;
const pending = new Map();
const send = (method, params = {}, sessionId) => new Promise((r) => {
  const id = nextId++;
  pending.set(id, r);
  browser.send(JSON.stringify({ id, method, params, sessionId }));
});

const summary = { pass: 0, fail: 0, done: false };

function onConsoleText(text) {
  for (const line of text.split('\n')) {
    if (line.length === 0) continue;
    console.log(line);
    // PASS/FAIL lines (webgpu_browser_test) or GoogleTest's per-test results.
    if (line.startsWith('PASS ') || line.startsWith('[       OK ]')) summary.pass++;
    if (line.startsWith('FAIL ')) summary.fail++;
    const done = line.match(/^CAMPELLO_WEBGPU_TESTS_DONE failures=(\d+)/);
    if (done) {
      summary.done = true;
      const failures = Number(done[1]);
      const ok = failures === 0 && summary.fail === 0 && summary.pass > 0;
      finish(ok ? 0 : 1,
        `\nrun_in_browser: ${summary.pass} passed, ${summary.fail} failed -> ${ok ? 'OK' : 'FAILED'}`);
    }
  }
}

browser.onmessage = (event) => {
  const msg = JSON.parse(event.data);
  if (msg.id && pending.has(msg.id)) {
    pending.get(msg.id)(msg);
    pending.delete(msg.id);
    return;
  }
  if (msg.method === 'Runtime.consoleAPICalled') {
    onConsoleText(msg.params.args.map((a) => a.value ?? a.description ?? '').join(' '));
  } else if (msg.method === 'Runtime.exceptionThrown') {
    const d = msg.params.exceptionDetails;
    finish(1, `run_in_browser: uncaught exception: ${d.exception?.description ?? d.text}`);
  }
};

const target = await send('Target.createTarget', { url: 'about:blank' });
const attached = await send('Target.attachToTarget', {
  targetId: target.result.targetId, flatten: true,
});
const sessionId = attached.result.sessionId;
await send('Runtime.enable', {}, sessionId);
await send('Page.enable', {}, sessionId);
await send('Page.navigate', { url: pageUrl }, sessionId);

setTimeout(() => finish(2,
  `run_in_browser: timed out after ${timeoutMs / 1000}s ` +
  `(${summary.pass} passed, ${summary.fail} failed, no completion line)`), timeoutMs);
