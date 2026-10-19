var assert = require('assert');
var fs = require('fs');
var os = require('os');
var path = require('path');

var unix = require('../lib/unix_dgram');
var binding = require('bindings')('unix_dgram.node');

var SOCKNAME = path.join(os.tmpdir(), 'unix_dgram_types.sock');
var sandbox = fs.mkdtempSync(path.join(os.tmpdir(), 'unix_dgram_types-'));
var received = [];
var done = false;

process.on('exit', function() {
  assert.equal(done, true);
});

try { fs.unlinkSync(SOCKNAME); } catch (e) { /* swallow */ }

var server = unix.createSocket('unix_dgram', function(buf, rinfo) {
  received.push('' + buf);
});
server.bind(SOCKNAME);

var client = unix.createSocket('unix_dgram', function(buf, rinfo) {
  assert(0);
});

assert.throws(function() { client.send(null, 0, 0, SOCKNAME); }, TypeError);
assert.throws(function() { client.send({}, 0, 0, SOCKNAME); }, TypeError);

assert.throws(function() {
  client.send(Buffer.from('PING'), Symbol('offset'), 4, SOCKNAME);
}, TypeError);
assert.throws(function() {
  client.send(Buffer.from('PING'), 0, Symbol('length'), SOCKNAME);
}, TypeError);

process.chdir(sandbox);
assert.throws(function() {
  client.send(Buffer.from('PING'), 0, 4, Symbol('path'));
}, TypeError);
assert.throws(function() { client.bind(Symbol('path')); }, TypeError);
assert.throws(function() { client.connect(Symbol('path')); }, TypeError);
assert.deepEqual(fs.readdirSync(sandbox), []);
assert.notEqual(client.connected, true);

assert.throws(function() { binding.close(Symbol('fd')); }, TypeError);

assert.throws(function() {
  binding.socket(binding.AF_UNIX, binding.SOCK_DGRAM, 0, null, function() {});
}, TypeError);
assert.throws(function() {
  binding.socket(binding.AF_UNIX, binding.SOCK_DGRAM, 0, function() {}, null);
}, TypeError);

setTimeout(function() {
  assert.deepEqual(received, []);

  client.send(Buffer.from('PING'), 0, 4, SOCKNAME, function(err) {
    assert.ifError(err);
    setTimeout(function() {
      assert.deepEqual(received, ['PING']);
      client.close();
      server.close();
      process.chdir(os.tmpdir());
      fs.rmdirSync(sandbox);
      fs.unlinkSync(SOCKNAME);
      done = true;
    }, 100);
  });
}, 100);
