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

  wasi.wasiImport.fd_prestat_get = function(fd, buf) {
    if (fd > 3) return 8;  // EBADF
    const view = new DataView(instance.exports.memory.buffer);
    view.setUint8(buf, 0);
    view.setUint32(buf + 4, Buffer.byteLength('/sandbox'), true);
    return 0;
  };

  wasi.wasiImport.fd_prestat_dir_name = function(fd, path, path_len) {
    const mem = Buffer.from(instance.exports.memory.buffer);
    mem.write('/sandbox', path, path_len, 'utf8');
    return 0;
  };

  wasi.wasiImport.fd_fdstat_get = function(fd, buf) {
    return 0;
  };
  // End of import object patching.

  const importObject = { wasi_unstable: wasi.wasiImport };
  const modulePath = path.join(wasmDir, `${process.argv[3]}.wasm`);
  const buffer = fs.readFileSync(modulePath);

  (async () => {
    ({ instance } = await WebAssembly.instantiate(buffer, importObject));

    WASI.start(instance);
  })();
} else {
  const assert = require('assert');
  const cp = require('child_process');

  function runWASI(options) {
    console.log('executing', options.test);
    const child = cp.fork(__filename, ['wasi-child', options.test]);

    child.on('exit', common.mustCall((code, signal) => {
      assert.strictEqual(code, options.exitCode || 0);
      assert.strictEqual(signal, null);
    }));
  }

  runWASI({ test: 'cant_dotdot' });
  runWASI({ test: 'exitcode', exitCode: 120 });
  runWASI({ test: 'fd_prestat_get_refresh' });
}
