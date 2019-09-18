// Flags: --experimental-modules

import { mustCall } from '../common/index.mjs';
import { ok, deepStrictEqual, strictEqual } from 'assert';

import { requireFixture, importFixture } from '../fixtures/pkgexports.mjs';

[requireFixture, importFixture].forEach((loadFixture) => {
  const isRequire = loadFixture === requireFixture;

  const validSpecifiers = new Map([
    // A simple mapping of a path.
    ['pkgexports/valid-cjs', { default: 'asdf' }],
    // A directory mapping, pointing to the package root.
    ['pkgexports/sub/asdf.js', { default: 'asdf' }],
    // A mapping pointing to a file that needs special encoding (%20) in URLs.
    ['pkgexports/space', { default: 'encoded path' }],
    // Verifying that normal packages still work with exports turned on.
    isRequire ? ['baz/index', { default: 'eye catcher' }] : [null],
    // Fallbacks
    ['pkgexports/fallbackdir/asdf.js', { default: 'asdf' }],
    ['pkgexports/fallbackfile', { default: 'asdf' }],
    // Dot main
    ['pkgexports', { default: 'asdf' }],
  ]);
  for (const [validSpecifier, expected] of validSpecifiers) {
    if (validSpecifier === null) continue;

    loadFixture(validSpecifier)
      .then(mustCall((actual) => {
        deepStrictEqual({ ...actual }, expected);
      }));
  }

  const undefinedExports = new Map([
    // There's no such export - so there's nothing to do.
    ['pkgexports/missing', './missing'],
    // The file exists but isn't exported. The exports is a number which counts
    // as a non-null value without any properties, just like `{}`.
    ['pkgexports-number/hidden.js', './hidden.js'],
  ]);

  const invalidExports = new Map([
    // Even though 'pkgexports/sub/asdf.js' works, alternate "path-like"
    // variants do not to prevent confusion and accidental loopholes.
    ['pkgexports/sub/./../asdf.js', './sub/./../asdf.js'],
    // This path steps back inside the package but goes through an exports
    // target that escapes the package, so we still catch that as invalid
    ['pkgexports/belowdir/pkgexports/asdf.js', './belowdir/pkgexports/asdf.js'],
    // This target file steps below the package
    ['pkgexports/belowfile', './belowfile'],
    // Directory mappings require a trailing / to work
    ['pkgexports/missingtrailer/x', './missingtrailer/x'],
    // Invalid target handling
    ['pkgexports/null', './null'],
    ['pkgexports/invalid1', './invalid1'],
    ['pkgexports/invalid2', './invalid2'],
    ['pkgexports/invalid3', './invalid3'],
    ['pkgexports/invalid4', './invalid4'],
    // Missing / invalid fallbacks
    ['pkgexports/nofallback1', './nofallback1'],
    ['pkgexports/nofallback2', './nofallback2'],
    // Reaching into nested node_modules
    ['pkgexports/nodemodules', './nodemodules'],
  ]);

  for (const [specifier, subpath] of undefinedExports) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code, (isRequire ? '' : 'ERR_') + 'MODULE_NOT_FOUND');
      assertStartsWith(err.message, 'Package exports');
      assertIncludes(err.message, `do not define a '${subpath}' subpath`);
    }));
  }

  for (const [specifier, subpath] of invalidExports) {
    loadFixture(specifier).catch(mustCall((err) => {
      strictEqual(err.code, (isRequire ? '' : 'ERR_') + 'MODULE_NOT_FOUND');
      assertStartsWith(err.message, (isRequire ? 'Package exports' :
        'Cannot resolve'));
      assertIncludes(err.message, isRequire ?
        `do not define a valid '${subpath}' subpath` :
        `matched for '${subpath}'`);
    }));
  }

  // Covering out bases - not a file is still not a file after dir mapping.
  loadFixture('pkgexports/sub/not-a-file.js').catch(mustCall((err) => {
    strictEqual(err.code, (isRequire ? '' : 'ERR_') + 'MODULE_NOT_FOUND');
    // ESM returns a full file path
    assertStartsWith(err.message, isRequire ?
      'Cannot find module \'pkgexports/sub/not-a-file.js\'' :
      'Cannot find module');
  }));

  // THe use of %2F escapes in paths fails loading
  loadFixture('pkgexports/sub/..%2F..%2Fbar.js').catch(mustCall((err) => {
    strictEqual(err.code, isRequire ? 'ERR_INVALID_FILE_URL_PATH' :
      'ERR_MODULE_NOT_FOUND');
  }));
});

function assertStartsWith(actual, expected) {
  const start = actual.toString().substr(0, expected.length);
  strictEqual(start, expected);
}

function assertIncludes(actual, expected) {
  ok(actual.toString().indexOf(expected),
     `${JSON.stringify(actual)} includes ${JSON.stringify(expected)}`);
}
