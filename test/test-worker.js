"use strict"
const assert = require('node:assert');
const {Worker, isMainThread} = require('node:worker_threads');

if (isMainThread) {
  const worker = new Worker(__filename);
  worker.on('exit', function(code) {
    assert.equal(code, 0);
  });
} else {
  require(__dirname + "/test-dgram-unix.js");
}
