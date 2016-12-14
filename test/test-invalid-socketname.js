var assert = require('assert');
var fs = require('fs');

var unix = require('../lib/unix_dgram');
var SOCKNAME = '\0abstractsocket';
var SOCKNAME_INVALID = '\0' + (Array(120).join("a"));

var EINVAL = 22;

var client = unix.createSocket('unix_dgram', function(buf, rinfo) {
  console.error('client recv', arguments);
  assert(0);
});

client.once('error', function(err) {
  assert.notStrictEqual(err.code, -EINVAL);
  client.once('error', function(err) {
    assert.ifError(err);
  });

  client.send(Buffer('ERROR2'), 0, 6, SOCKNAME_INVALID, function(err) {
    assert.strictEqual(err.code, -EINVAL);
    client.close();
  });
});

client.send(Buffer('ERROR1'), 0, 6, SOCKNAME);
