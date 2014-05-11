var assert = require('assert');
var fs = require('fs');

var unix = require('../lib/unix_dgram');
var SOCKNAME = '/tmp/unix_dgram.sock';

var sentCount = 0;
var seenCount = 0;

process.on('exit', function() {
  assert(seenCount === sentCount);
});

try { fs.unlinkSync(SOCKNAME); } catch (e) { /* swallow */ }

var server = unix.createSocket('unix_dgram', function(buf, rinfo) {
  console.error('server recv', '' + buf, arguments);
  assert.equal('' + buf, 'PING' + seenCount);
  if (++ seenCount === 12) {
    server.close();
    client.close();
  }
});
server.bind(SOCKNAME);

var client = unix.createSocket('unix_dgram', function(buf, rinfo) {
  console.error('client recv', arguments);
  assert(0);
});

client.on('error', function(err) {
  console.error(err);
  assert(0);
});

client.on('connect', function() {
  console.error('connected');
  client.on('congestion', function() {
    console.error('congestion');
    client.on('writable', function() {
      console.error('writable');
      var msg = Buffer('PING' + sentCount);
      client.send(msg, function() {
        console.error('client send', msg.toString());
        ++ sentCount;
      });
    });
  });

  var i = 0;
  var msg;
  /*
   * Usually /proc/sys/net/unix/max_dgram_qlen is 10 so one that's being processed, 10 in the
   * recv queue and the 12th is dropped
   */
  while (i ++ < 12) {
    msg = Buffer('PING' + sentCount);
    client.send(msg, function() {
      console.error('client send', msg.toString());
      ++ sentCount;
    });
  }
});

client.connect(SOCKNAME);
