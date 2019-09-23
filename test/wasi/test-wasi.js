'use strict';
require('../common');

if (process.argv[2] === 'wasi-child') {
  const fixtures = require('../common/fixtures');
  const fs = require('fs');
  const path = require('path');
  const { WASI } = require('wasi');
  const wasmDir = path.join(__dirname, 'wasm');
  const wasi = new WASI({
    args: [],
    env: process.env,
    preopens: {
      '/sandbox': fixtures.path('wasi')
    }
  });
  const importObject = { wasi_unstable: wasi.wasiImport };
  const modulePath = path.join(wasmDir, `${process.argv[3]}.wasm`);
  const buffer = fs.readFileSync(modulePath);

  (async () => {
    const { instance } = await WebAssembly.instantiate(buffer, importObject);

    wasi.start(instance);
  })();
} else {
  const assert = require('assert');
  const cp = require('child_process');

  function runWASI(options) {
    console.log('executing', options.test);
    const child = cp.spawnSync(process.execPath, [
      '--experimental-wasm-bigint',
      __filename,
      'wasi-child',
      options.test
    ]);

    assert.strictEqual(child.status, options.exitCode || 0);
    assert.strictEqual(child.signal, null);
    assert.strictEqual(child.stdout.toString(), options.stdout || '');
  }

  runWASI({ test: 'cant_dotdot' });
  runWASI({ test: 'exitcode', exitCode: 120 });
  runWASI({ test: 'fd_prestat_get_refresh' });
  runWASI({ test: 'read_file', stdout: 'hello from input.txt\n' });
}
