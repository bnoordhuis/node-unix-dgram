var assert = require('assert');
var fs = require('fs');
var path = require('path');
var os = require('os');

var unix = require('../lib/unix_dgram');
var SOCKNAME = '/tmp/unix_dgram_fd_test.sock';

var receivedFds = [];
var sentFds = [];
var messageReceived = false;
var testFiles = [];

process.on('exit', function() {
  assert.equal(messageReceived, true, 'Message should have been received');
  assert.equal(receivedFds.length, sentFds.length, 'Should receive same number of FDs as sent');
  
  // Verify the file descriptors work by reading from them
  for (var i = 0; i < receivedFds.length; i++) {
    var originalContent = fs.readFileSync(testFiles[i]);
    var receivedContent = fs.readFileSync(receivedFds[i]);
    assert.deepEqual(originalContent, receivedContent, 'File contents should match');
    
    // Close the received file descriptors
    fs.closeSync(receivedFds[i]);
  }
  
  // Clean up temp files
  testFiles.forEach(function(file) {
    try { fs.unlinkSync(file); } catch (e) {}
  });
  
  console.log('File descriptor passing test passed!');
});

try { fs.unlinkSync(SOCKNAME); } catch (e) { /* swallow */ }

// Create some temporary files to send as file descriptors
var tempDir = os.tmpdir();
var testFile1 = path.join(tempDir, 'fd_test1.txt');
var testFile2 = path.join(tempDir, 'fd_test2.txt');
testFiles = [testFile1, testFile2];

fs.writeFileSync(testFile1, 'Hello from file 1');
fs.writeFileSync(testFile2, 'Hello from file 2');

var fd1 = fs.openSync(testFile1, 'r');
var fd2 = fs.openSync(testFile2, 'r');
sentFds = [fd1, fd2];

var server = unix.createSocket('unix_dgram', function(buf, rinfo) {
  console.log('Server received message:', buf.toString());
  console.log('Received file descriptors:', rinfo.fds);
  
  assert.equal(buf.toString(), 'Hello with FDs');
  assert(rinfo.fds, 'Should have received file descriptors');
  assert.equal(rinfo.fds.length, 2, 'Should have received 2 file descriptors');
  
  receivedFds = rinfo.fds;
  messageReceived = true;
  
  server.close();
  client.close();
  
  // Clean up sent file descriptors
  fs.closeSync(fd1);
  fs.closeSync(fd2);
});

server.bind(SOCKNAME);

var client = unix.createSocket('unix_dgram');

// Test sending with file descriptors using the simplified API
var message = Buffer.from('Hello with FDs');
client.send(message, 0, message.length, SOCKNAME, [fd1, fd2], function(err) {
  if (err) {
    console.error('Error sending message with FDs:', err);
    process.exit(1);
  }
  console.log('Message with FDs sent successfully');
});
