// TODO(cjihrig): Put WASI behind a flag.
// TODO(cjihrig): Provide a mechanism to bind to WASM modules.
'use strict';
/* global WebAssembly */
const { Array, ArrayPrototype } = primordials;
const { ERR_INVALID_ARG_TYPE } = require('internal/errors').codes;
const { WASI: _WASI } = internalBinding('wasi');


class WASI {
  constructor(options = {}) {
    if (options === null || typeof options !== 'object')
      throw new ERR_INVALID_ARG_TYPE('options', 'object', options);

    // eslint-disable-next-line prefer-const
    let { args, env, preopens, memory } = options;

    if (Array.isArray(args))
      args = ArrayPrototype.map(args, (arg) => { return String(arg); });
    else if (args === undefined)
      args = [];
    else
      throw new ERR_INVALID_ARG_TYPE('options.args', 'Array', args);

    const envPairs = [];

    if (env !== null && typeof env === 'object') {
      for (const key in env) {
        const value = env[key];
        if (value !== undefined)
          envPairs.push(`${key}=${value}`);
      }
    } else if (env !== undefined) {
      throw new ERR_INVALID_ARG_TYPE('options.env', 'Object', env);
    }

    if (preopens == null) {
      preopens = null;
    } else if (typeof preopens !== 'object') {
      throw new ERR_INVALID_ARG_TYPE('options.preopens', 'Object', preopens);
    } else {
      //
    }

    // TODO(cjihrig): Validate preopen object schema.

    if (memory instanceof WebAssembly.Memory) {
      memory = memory.buffer;
    } else {
      throw new ERR_INVALID_ARG_TYPE(
        'options.memory', 'WebAssembly.Memory', memory);
    }

    const wrap = new _WASI(args, envPairs, preopens, memory);

    for (const prop in wrap) {
      wrap[prop] = wrap[prop].bind(wrap);
    }

    this.wasiImport = wrap;
  }

  static start(instance) {
    if (!(instance instanceof WebAssembly.Instance)) {
      throw new ERR_INVALID_ARG_TYPE(
        'instance', 'WebAssembly.Instance', instance);
    }

    const exports = instance.exports;

    if (exports === null || typeof exports !== 'object')
      throw new ERR_INVALID_ARG_TYPE('instance.exports', 'Object', exports);

    if (exports._start)
      exports._start();
    else if (exports.__wasi_unstable_reactor_start)
      exports.__wasi_unstable_reactor_start();
  }
}


module.exports = { WASI };
