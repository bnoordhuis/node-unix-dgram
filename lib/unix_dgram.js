var events = require('events');
var dgram = require('dgram');
var util = require('util');
var binding = require('bindings')('unix_dgram.node');

var SOCK_DGRAM  = binding.SOCK_DGRAM;
var AF_UNIX     = binding.AF_UNIX;

var socket  = binding.socket;
var bind    = binding.bind;
var send    = binding.send;
var close   = binding.close;


function errnoException(errorno, syscall) {
  var e = new Error(syscall + ' ' + errorno);
  e.errno = e.code = errorno;
  e.syscall = syscall;
  return e;
}


function recv(status, buf) {
  var rinfo = {
    size: buf.length,
    address: {}
  };
  this.emit('message', buf, rinfo);
}


exports.createSocket = function(type, listener) {
  if (type == 'udp4' || type == 'udp6')
    return dgram.createSocket(type, listener);

  return new Socket(type, listener);
};


function Socket(type, listener) {
  if (type != 'unix_dgram')
    throw new Error('Unsupported socket type: ' + type);

  var err = socket(AF_UNIX, SOCK_DGRAM, 0, recv.bind(this));
  if (err < 0)
    throw errnoException(err, 'socket');

  this.fd = err;
  this.type = type;

  if (typeof listener === 'function')
    this.on('message', listener);
}
util.inherits(Socket, events.EventEmitter);


Socket.prototype.bind = function(path) {
  var err = bind(this.fd, path);
  if (err < 0)
    this.emit('error', errnoException(err, 'bind'));
  else
    this.emit('listening');
};


Socket.prototype.send = function(buf, offset, length, path, callback) {
  // FIXME defer error and callback to next tick?
  var err = send(this.fd, buf, offset, length, path);
  if (err < 0)
    this.emit('error', errnoException(err, 'send'));
  else if (typeof callback === 'function')
    callback();
};


// compatibility
Socket.prototype.sendto = function(buf, offset, length, path, callback) {
  return this.send(buf, offset, length, path, callback);
};


Socket.prototype.close = function() {
  var err = close(this.fd);
  if (err < 0)
    throw new errnoException(err, 'close');
  this.fd = -1;
};


Socket.prototype.address = function() {
  throw new Error('not implemented');
};


Socket.prototype.setTTL = function(ttl) {
  throw new Error('not implemented');
};


Socket.prototype.setBroadcast = function(flag) {
  throw new Error('not implemented');
};


Socket.prototype.setMulticastTTL = function(ttl) {
  throw new Error('not implemented');
};


Socket.prototype.setMulticastLoopback = function(flag) {
  throw new Error('not implemented');
};


Socket.prototype.addMembership = function(multicastAddress, multicastInterface) {
  throw new Error('not implemented');
};


Socket.prototype.dropMembership = function(multicastAddress, multicastInterface) {
  throw new Error('not implemented');
};
