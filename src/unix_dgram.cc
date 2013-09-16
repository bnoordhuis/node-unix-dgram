// -D_GNU_SOURCE makes SOCK_NONBLOCK etc. available on linux
#undef  _GNU_SOURCE
#define _GNU_SOURCE

#include "uv.h"
#include "node.h"
#include "node_buffer.h"

#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <map>

#define offset_of(type, member)                                               \
  ((intptr_t) ((char *) &(((type *) 8)->member) - 8))

#define container_of(ptr, type, member)                                       \
  ((type *) ((char *) (ptr) - offset_of(type, member)))

namespace {

using node::Buffer;
using node::FatalException;
using v8::Arguments;
using v8::Context;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Null;
using v8::Object;
using v8::Persistent;
using v8::String;
using v8::TryCatch;
using v8::Value;

struct SocketContext {
  Persistent<Function> cb_;
  uv_poll_t handle_;
  int fd_;
};

typedef std::map<int, SocketContext*> watchers_t;


Persistent<String> errno_symbol;
watchers_t watchers;


void SetNonBlock(int fd) {
  int flags;
  int r;

  flags = fcntl(fd, F_GETFL);
  assert(flags != -1);

  r = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  assert(r != -1);
}


void SetCloExec(int fd) {
  int flags;
  int r;

  flags = fcntl(fd, F_GETFD);
  assert(flags != -1);

  r = fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  assert(r != -1);
}


void SetErrno(int errorno) {
  // set errno in the global context, this is the technique
  // that node uses to propagate system level errors to JS land
  Context::GetCurrent()->Global()->Set(errno_symbol, Integer::New(errorno));
}


void OnRecv(uv_poll_t* handle, int status, int events) {
  HandleScope scope;
  Handle<Value> argv[3];
  sockaddr_storage ss;
  SocketContext* sc;
  Buffer* buf;
  msghdr msg;
  iovec iov;
  ssize_t r;
  char scratch[65536];

  sc = container_of(handle, SocketContext, handle_);

  r = -1;
  buf = NULL;
  argv[0] = argv[1] = argv[2] = Null();

  assert(0 == status);
  assert(0 == (events & ~UV_READABLE));

  iov.iov_base = scratch;
  iov.iov_len = sizeof scratch;

  memset(&msg, 0, sizeof msg);
  msg.msg_iovlen = 1;
  msg.msg_iov = &iov;
  msg.msg_name = &ss;
  msg.msg_namelen = sizeof ss;

  do
    r = recvmsg(sc->fd_, &msg, 0);
  while (r == -1 && errno == EINTR);

  if (r == -1) {
    SetErrno(errno);
    goto err;
  }

  buf = Buffer::New(scratch, r);
  argv[1] = buf->handle_;

err:
  argv[0] = Integer::New(r);

  TryCatch tc;

  sc->cb_->Call(Context::GetCurrent()->Global(),
                sizeof(argv) / sizeof(argv[0]),
                argv);

  if (tc.HasCaught())
    FatalException(tc);
}


void StartWatcher(int fd, Handle<Value> callback) {
  // start listening for incoming dgrams
  SocketContext* sc = new SocketContext;
  sc->cb_ = Persistent<Function>::New(callback.As<Function>());
  sc->fd_ = fd;

  uv_poll_init(uv_default_loop(), &sc->handle_, fd);
  uv_poll_start(&sc->handle_, UV_READABLE, OnRecv);

  // so we can disarm the watcher when close(fd) is called
  watchers.insert(watchers_t::value_type(fd, sc));
}


void StopWatcher(int fd) {
  watchers_t::iterator iter = watchers.find(fd);
  assert(iter != watchers.end());

  SocketContext* sc = iter->second;
  sc->cb_.Dispose();
  sc->cb_.Clear();

  uv_poll_stop(&sc->handle_);
  watchers.erase(iter);
  delete sc;
}


Handle<Value> Socket(const Arguments& args) {
  HandleScope scope;
  Local<Value> cb;
  int protocol;
  int domain;
  int type;
  int fd;

  assert(args.Length() == 4);

  domain    = args[0]->Int32Value();
  type      = args[1]->Int32Value();
  protocol  = args[2]->Int32Value();
  cb        = args[3];

#if defined(SOCK_NONBLOCK)
  type |= SOCK_NONBLOCK;
#endif
#if defined(SOCK_CLOEXEC)
  type |= SOCK_CLOEXEC;
#endif

  if ((fd = socket(domain, type, protocol)) == -1) {
    SetErrno(errno);
    goto out;
  }

  #if !defined(SOCK_NONBLOCK)
  SetNonBlock(fd);
#endif
#if !defined(SOCK_CLOEXEC)
  SetCloExec(fd);
#endif

  StartWatcher(fd, cb);

out:
  return scope.Close(Integer::New(fd));
}


Handle<Value> Bind(const Arguments& args) {
  HandleScope scope;
  sockaddr_un s_un;
  int fd;
  int r;

  assert(args.Length() == 2);

  fd = args[0]->Int32Value();
  String::Utf8Value path(args[1]);

  strncpy(s_un.sun_path, *path, sizeof(s_un.sun_path) - 1);
  s_un.sun_path[sizeof(s_un.sun_path) - 1] = '\0';
  s_un.sun_family = AF_UNIX;

  if ((r = bind(fd, reinterpret_cast<sockaddr*>(&s_un), sizeof s_un)) == -1)
    SetErrno(errno);

  return scope.Close(Integer::New(r));
}


Handle<Value> Send(const Arguments& args) {
  HandleScope scope;
  Local<Object> buf;
  sockaddr_un s_un;
  size_t offset;
  size_t length;
  msghdr msg;
  iovec iov;
  int fd;
  int r;

  assert(args.Length() == 5);

  fd = args[0]->Int32Value();
  buf = args[1]->ToObject();
  offset = args[2]->Uint32Value();
  length = args[3]->Uint32Value();
  String::Utf8Value path(args[4]);

  assert(Buffer::HasInstance(buf));
  assert(offset + length <= Buffer::Length(buf));

  iov.iov_base = Buffer::Data(buf) + offset;
  iov.iov_len = length;

  strncpy(s_un.sun_path, *path, sizeof(s_un.sun_path) - 1);
  s_un.sun_path[sizeof(s_un.sun_path) - 1] = '\0';
  s_un.sun_family = AF_UNIX;

  memset(&msg, 0, sizeof msg);
  msg.msg_iovlen = 1;
  msg.msg_iov = &iov;
  msg.msg_name = reinterpret_cast<void*>(&s_un);
  msg.msg_namelen = sizeof s_un;

  do
    r = sendmsg(fd, &msg, 0);
  while (r == -1 && errno == EINTR);

  if (r == -1)
    SetErrno(errno);

  return scope.Close(Integer::New(r));
}


Handle<Value> Close(const Arguments& args) {
  HandleScope scope;
  int fd;
  int r;

  assert(args.Length() == 1);

  fd = args[0]->Int32Value();
  r = close(fd);

  if (r == -1)
    SetErrno(errno);

  StopWatcher(fd);

  return scope.Close(Integer::New(r));
}


void Initialize(Handle<Object> target) {
  errno_symbol = Persistent<String>::New(String::NewSymbol("errno"));

  // don't need to be read-only, only used by the JS shim
  target->Set(String::NewSymbol("AF_UNIX"), Integer::New(AF_UNIX));
  target->Set(String::NewSymbol("SOCK_DGRAM"), Integer::New(SOCK_DGRAM));

  target->Set(String::NewSymbol("socket"),
              FunctionTemplate::New(Socket)->GetFunction());

  target->Set(String::NewSymbol("bind"),
              FunctionTemplate::New(Bind)->GetFunction());

  target->Set(String::NewSymbol("send"),
              FunctionTemplate::New(Send)->GetFunction());

  target->Set(String::NewSymbol("close"),
              FunctionTemplate::New(Close)->GetFunction());
}


} // anonymous namespace

NODE_MODULE(unix_dgram, Initialize)
