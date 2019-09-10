'use strict';

require('../common');

const assert = require('assert');
const fixtures = require('../common/fixtures');
const buffer = fixtures.readSync('simple-wasi.wasm');
const { WASI } = require('wasi');

const memory = new WebAssembly.Memory({ initial: 1 });
const wasi = new WASI({ args: [], env: process.env, memory });

const importObject = {
  wasi_unstable: wasi.wasiImport
};

WebAssembly.instantiate(buffer, importObject).then((results) => {
  assert(results.instance.exports._start);
  results.instance.exports._start();
});
