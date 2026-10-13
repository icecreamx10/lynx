// Copyright 2026 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.
import { createRequire } from 'node:module';
import path from 'node:path';
const root=path.resolve(import.meta.dirname,'../..');
const require=createRequire(root+'/platform/wasm/app/package.json');
const esbuild=require('esbuild');
esbuild.buildSync({entryPoints:[root+'/js_libraries/lynx-core/src/index.build.ts'],outfile:root+'/out/lynx_core.js',bundle:true,format:'iife',target:'es2019',minify:true,legalComments:'none',
 nodePaths:[root+'/platform/wasm/app/node_modules'],
 alias:{'@lynx-js/ios-polyfill':root+'/js_libraries/lynx-core/kernel-build/android-polyfill.js','@lynx-js/ios-polyfill-promise':root+'/js_libraries/lynx-promise/src/index.js','@lynx-js/runtime-shared':root+'/js_libraries/lynx-runtime-shared/src/index.ts'},
 define:{__BUILD_VERSION__:'"4.4"',__VERSION__:'"4.4"',__COMMIT_HASH__:'"wasm-experiment"',__OPEN_INTERNAL_LOG__:'false',__WEB__:'false',NODE_ENV:'"production"'}
});
