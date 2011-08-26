SOCKNAME = '/tmp/unix_dgram.sock';

var unix = require('../build/default/unix_dgram');

var server = unix.createSocket('unix_dgram', function(buf, rinfo) {
  console.error('server recv', arguments);
  server.close();
  client.close();
  console.error('closed', server, client);
});
server.bind(SOCKNAME);

var client = unix.createSocket('unix_dgram', function(buf, rinfo) {
  console.error('client recv', arguments);
});
client.send(Buffer('PING'), 0, 4, SOCKNAME, function() {
  console.error('client send', arguments);
});