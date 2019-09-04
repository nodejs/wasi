// TODO(cjihrig): Put WASI behind a flag.
// TODO(cjihrig): Provide a mechanism to bind to WASM modules.
'use strict';
const { Array, ArrayPrototype } = primordials;
const { ERR_INVALID_ARG_TYPE } = require('internal/errors').codes;
const { WASI: _WASI } = internalBinding('wasi');


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

    if (preopens == null) {
      preopens = null;
    } else if (typeof preopens !== 'object') {
      throw new ERR_INVALID_ARG_TYPE('options.preopens', 'Object', preopens);
    } else {
      //
    }

    // TODO(cjihrig): Validate preopen object schema.

    // TODO(cjihrig): Temporarily expose these for development.
    // eslint-disable-next-line no-undef
    const memory = Buffer.alloc(200000);
    this._memory = memory;
    this._view = new DataView(memory.buffer);
    this._wasi = new _WASI(args, envPairs, preopens, memory);
  }
}


module.exports = { WASI };
