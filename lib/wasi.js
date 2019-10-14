'use strict';
/* global WebAssembly */
const { Array, ArrayPrototype, Object } = primordials;
const { ERR_INVALID_ARG_TYPE } = require('internal/errors').codes;
const { emitExperimentalWarning } = require('internal/util');
const { WASI: _WASI } = internalBinding('wasi');
const kSetMemory = Symbol('setMemory');

emitExperimentalWarning('WASI');


class WASI {
  constructor(options = {}) {
    if (options === null || typeof options !== 'object')
      throw new ERR_INVALID_ARG_TYPE('options', 'object', options);

    // eslint-disable-next-line prefer-const
    let { args, env, preopens } = options;

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

    const preopenArray = [];

    if (typeof preopens === 'object' && preopens !== null) {
      Object.keys(preopens).forEach((key) => {
        preopenArray.push(String(key));
        preopenArray.push(String(preopens[key]));
      });
    } else if (preopens !== undefined) {
      throw new ERR_INVALID_ARG_TYPE('options.preopens', 'Object', preopens);
    }

    const wrap = new _WASI(args, envPairs, preopenArray);

    for (const prop in wrap) {
      wrap[prop] = wrap[prop].bind(wrap);
    }

    this[kSetMemory] = wrap._setMemory;
    delete wrap._setMemory;
    this.wasiImport = wrap;
  }

  start(instance) {
    if (!(instance instanceof WebAssembly.Instance)) {
      throw new ERR_INVALID_ARG_TYPE(
        'instance', 'WebAssembly.Instance', instance);
    }

    const exports = instance.exports;

    if (exports === null || typeof exports !== 'object')
      throw new ERR_INVALID_ARG_TYPE('instance.exports', 'Object', exports);

    const { memory } = exports;

    if (!(memory instanceof WebAssembly.Memory)) {
      throw new ERR_INVALID_ARG_TYPE(
        'instance.exports.memory', 'WebAssembly.Memory', memory);
    }

    this[kSetMemory](memory);

    if (exports._start)
      exports._start();
    else if (exports.__wasi_unstable_reactor_start)
      exports.__wasi_unstable_reactor_start();
  }
}


module.exports = { WASI };
