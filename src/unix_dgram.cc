// -D_GNU_SOURCE makes SOCK_NONBLOCK etc. available on linux
#undef  _GNU_SOURCE
#define _GNU_SOURCE

#include <nan.h>

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

using node::FatalException;
using v8::Context;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
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
  Context::GetCurrent()->Global()->Set(NanSymbol("errno"), Integer::New(errorno));
}


void OnRecv(uv_poll_t* handle, int status, int events) {
  NanScope();
  Handle<Value> argv[3];
  sockaddr_storage ss;
  SocketContext* sc;
  msghdr msg;
  iovec iov;
  ssize_t r;
  char scratch[65536];

  sc = container_of(handle, SocketContext, handle_);

  r = -1;
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

  argv[1] = NanNewBufferHandle(scratch, r);

err:
  argv[0] = Integer::New(r);

  TryCatch tc;

  NanPersistentToLocal(sc->cb_)->Call(Context::GetCurrent()->Global(),
                                      sizeof(argv) / sizeof(argv[0]),
                                      argv);

  if (tc.HasCaught())
    FatalException(tc);
}


void StartWatcher(int fd, Handle<Value> callback) {
  // start listening for incoming dgrams
  SocketContext* sc = new SocketContext;
  NanAssignPersistent(Function, sc->cb_, callback.As<Function>());
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
  NanDispose(sc->cb_);

  uv_poll_stop(&sc->handle_);
  watchers.erase(iter);
  delete sc;
}


NAN_METHOD(Socket) {
  NanScope();
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
  NanReturnValue(Integer::New(fd));
}


NAN_METHOD(Bind) {
  NanScope();
  sockaddr_un sun;
  int fd;
  int r;

  assert(args.Length() == 2);

  fd = args[0]->Int32Value();
  String::Utf8Value path(args[1]);

  strncpy(sun.sun_path, *path, sizeof(sun.sun_path) - 1);
  sun.sun_path[sizeof(sun.sun_path) - 1] = '\0';
  sun.sun_family = AF_UNIX;

  if ((r = bind(fd, reinterpret_cast<sockaddr*>(&sun), sizeof sun)) == -1)
    SetErrno(errno);

  NanReturnValue(Integer::New(r));
}


NAN_METHOD(Send) {
  NanScope();
  Local<Object> buf;
  sockaddr_un sun;
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

  assert(node::Buffer::HasInstance(buf));
  assert(offset + length <= node::Buffer::Length(buf));

  iov.iov_base = node::Buffer::Data(buf) + offset;
  iov.iov_len = length;

  strncpy(sun.sun_path, *path, sizeof(sun.sun_path) - 1);
  sun.sun_path[sizeof(sun.sun_path) - 1] = '\0';
  sun.sun_family = AF_UNIX;

  memset(&msg, 0, sizeof msg);
  msg.msg_iovlen = 1;
  msg.msg_iov = &iov;
  msg.msg_name = reinterpret_cast<void*>(&sun);
  msg.msg_namelen = sizeof sun;

  do
    r = sendmsg(fd, &msg, 0);
  while (r == -1 && errno == EINTR);

  if (r == -1)
    SetErrno(errno);

  NanReturnValue(Integer::New(r));
}


NAN_METHOD(Close) {
  NanScope();
  int fd;
  int r;

  assert(args.Length() == 1);

  fd = args[0]->Int32Value();
  r = close(fd);

  if (r == -1)
    SetErrno(errno);

  StopWatcher(fd);

  NanReturnValue(Integer::New(r));
}


void Initialize(Handle<Object> target) {
  // don't need to be read-only, only used by the JS shim
  target->Set(NanSymbol("AF_UNIX"), Integer::New(AF_UNIX));
  target->Set(NanSymbol("SOCK_DGRAM"), Integer::New(SOCK_DGRAM));

  target->Set(NanSymbol("socket"),
              FunctionTemplate::New(Socket)->GetFunction());

  target->Set(NanSymbol("bind"),
              FunctionTemplate::New(Bind)->GetFunction());

  target->Set(NanSymbol("send"),
              FunctionTemplate::New(Send)->GetFunction());

  target->Set(NanSymbol("close"),
              FunctionTemplate::New(Close)->GetFunction());
}


} // anonymous namespace

NODE_MODULE(unix_dgram, Initialize)
