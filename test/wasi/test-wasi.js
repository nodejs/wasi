'use strict';
const common = require('../common');

if (process.argv[2] === 'wasi-child') {
  const fixtures = require('../common/fixtures');
  const tmpdir = require('../../test/common/tmpdir');
  const fs = require('fs');
  const path = require('path');

  common.expectWarning('ExperimentalWarning',
                       'WASI is an experimental feature. This feature could ' +
                       'change at any time');

  const { WASI } = require('wasi');
  tmpdir.refresh();
  const wasmDir = path.join(__dirname, 'wasm');
  const wasi = new WASI({
    args: [],
    env: process.env,
    preopens: {
      '/sandbox': fixtures.path('wasi'),
      '/tmp': tmpdir.path
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
    const opts = {};

    if (options.stdin !== undefined)
      opts.input = options.stdin;

    const child = cp.spawnSync(process.execPath, [
      '--experimental-wasi',
      '--experimental-wasm-bigint',
      __filename,
      'wasi-child',
      options.test
    ], opts);

    assert.strictEqual(child.status, options.exitCode || 0);
    assert.strictEqual(child.signal, null);
    assert.strictEqual(child.stdout.toString(), options.stdout || '');
  }

  runWASI({ test: 'cant_dotdot' });
  runWASI({ test: 'clock_getres' });
  runWASI({ test: 'exitcode', exitCode: 120 });
  runWASI({ test: 'fd_prestat_get_refresh' });
  runWASI({ test: 'follow_symlink', stdout: 'hello from input.txt\n' });
  runWASI({ test: 'getentropy' });
  runWASI({ test: 'getrusage' });
  runWASI({ test: 'gettimeofday' });
  runWASI({ test: 'notdir' });
  // runWASI({ test: 'poll' });
  runWASI({ test: 'preopen_populates' });
  runWASI({ test: 'read_file', stdout: 'hello from input.txt\n' });
  runWASI({
    test: 'read_file_twice',
    stdout: 'hello from input.txt\nhello from input.txt\n'
  });
  runWASI({ test: 'stat' });
  runWASI({ test: 'stdin', stdin: 'hello world', stdout: 'hello world' });
  runWASI({ test: 'symlink_escape' });
  runWASI({ test: 'symlink_loop' });
  runWASI({ test: 'write_file' });
}
