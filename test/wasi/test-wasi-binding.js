'use strict';

const common = require('../common');

const assert = require('assert');
const fixtures = require('../common/fixtures');
const buffer = fixtures.readSync(['wasi', 'simple-wasi.wasm']);
const { WASI } = require('wasi');

const memory = new WebAssembly.Memory({ initial: 1 });
const wasi = new WASI({ args: [], env: process.env, memory });

const importObject = {
  wasi_unstable: wasi.wasiImport
};

WebAssembly.instantiate(buffer, importObject)
.then(common.mustCall((results) => {
  assert(results.instance.exports._start);
  // Currently when running this the exit status of the process will be 71
  // which if I'm reading the uvwasi error codes correctly matches __WASI_ESRCH
  // which is "No such process".
  // TODO(danbev) enable start when the above issue has been sorted out.
  // WASI.start(results.instance);
}));
