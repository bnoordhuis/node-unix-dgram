var assert = require('assert');
var fs = require('fs');

var unix = require('../src/unix_dgram');
var SOCKNAME = '/tmp/unix_dgram.sock';

var sentPing = false;
var seenPing = false;

process.on('exit', function() {
  assert.equal(sentPing, true);
  assert.equal(seenPing, true);
});

try { fs.unlinkSync(SOCKNAME); } catch (e) { /* swallow */ }

var server = unix.createSocket('unix_dgram', function(buf, rinfo) {
  console.error('server recv', arguments);
  assert.equal('' + buf, 'PING');
  seenPing = true;
  server.close();
  client.close();
});
server.bind(SOCKNAME);

var client = unix.createSocket('unix_dgram', function(buf, rinfo) {
  console.error('client recv', arguments);
  assert(0);
});

client.send(Buffer('PING'), 0, 4, SOCKNAME, function() {
  console.error('client send', arguments);
  sentPing = true;
});
