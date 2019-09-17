'use strict';
const common = require('../common');

if (process.argv[2] === 'wasi-child') {
  const fs = require('fs');
  const path = require('path');
  const { WASI } = require('wasi');
  const wasmDir = path.join(__dirname, 'wasm');
  const memory = new WebAssembly.Memory({ initial: 3 });
  const wasi = new WASI({ args: [], env: process.env, memory });

  // TODO(cjihrig): Patch the import object until the native bindings are ready.
  let instance = null;

  wasi.wasiImport.path_open = function() {
    // Called by the 'cant_dotdot' test.
    return 76; // ENOTCAPABLE
  };
  // End of import object patching.

  const importObject = { wasi_unstable: wasi.wasiImport };
  const modulePath = path.join(wasmDir, `${process.argv[3]}.wasm`);
  const buffer = fs.readFileSync(modulePath);

  (async () => {
    ({ instance } = await WebAssembly.instantiate(buffer, importObject));

    wasi.start(instance);
  })();
} else {
  const assert = require('assert');
  const cp = require('child_process');

  function runWASI(options) {
    console.log('executing', options.test);
    const child = cp.fork(__filename, ['wasi-child', options.test], {
      execArgv: ['--experimental-wasm-bigint']
    });

    child.on('exit', common.mustCall((code, signal) => {
      assert.strictEqual(code, options.exitCode || 0);
      assert.strictEqual(signal, null);
    }));
  }

  runWASI({ test: 'cant_dotdot' });
  runWASI({ test: 'exitcode', exitCode: 120 });
  runWASI({ test: 'fd_prestat_get_refresh' });
}
