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
#include <algorithm>

#define offset_of(type, member)                                               \
  ((intptr_t) ((char *) &(((type *) 8)->member) - 8))

#define container_of(ptr, type, member)                                       \
  ((type *) ((char *) (ptr) - offset_of(type, member)))

namespace {

void OnEvent(uv_poll_t* handle, int status, int events);

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
  Persistent<Function> recv_cb_;
  Persistent<Function> writable_cb_;
  uv_poll_t handle_;
  int fd_;
};

typedef std::map<int, SocketContext*> watchers_t;

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

void OnRecv(SocketContext* sc) {
  NanScope();
  Handle<Value> argv[3];
  msghdr msg;
  iovec iov;
  ssize_t err;
  char scratch[65536];

  /* Union to avoid breaking strict-aliasing rules */
  union {
    struct sockaddr_un sun;
    struct sockaddr_storage ss;
  } u_addr;

  argv[0] = argv[1] = argv[2] = NanNull();

  iov.iov_base = scratch;
  iov.iov_len = sizeof scratch;

  u_addr.sun.sun_path[0] = '\0';

  memset(&msg, 0, sizeof msg);
  msg.msg_iovlen = 1;
  msg.msg_iov = &iov;
  msg.msg_name = &u_addr.ss;
  msg.msg_namelen = sizeof u_addr.ss;

  do
    err = recvmsg(sc->fd_, &msg, 0);
  while (err == -1 && errno == EINTR);

  if (err == -1) {
    err = -errno;
  } else {
    argv[1] = NanNewBufferHandle(scratch, err);
    if (u_addr.sun.sun_path[0] != '\0') {
      argv[2] = NanNew<String>(u_addr.sun.sun_path);
    }
  }

  argv[0] = NanNew<Integer>(err);

  TryCatch tc;
  NanNew(sc->recv_cb_)->Call(NanGetCurrentContext()->Global(),
                             sizeof(argv) / sizeof(argv[0]),
                             argv);

  if (tc.HasCaught())
    FatalException(tc);
}

void OnWritable(SocketContext* sc) {
  NanScope();
  TryCatch tc;
  uv_poll_start(&sc->handle_, UV_READABLE, OnEvent);
  NanNew(sc->writable_cb_)->Call(NanGetCurrentContext()->Global(), 0, 0);
  if (tc.HasCaught())
    FatalException(tc);
}

void OnEvent(uv_poll_t* handle, int status, int events) {
  assert(0 == status);
  assert(0 == (events & ~(UV_READABLE | UV_WRITABLE)));
  SocketContext* sc = container_of(handle, SocketContext, handle_);
  if (events & UV_READABLE)
    OnRecv(sc);

  if (events & UV_WRITABLE)
    OnWritable(sc);
}

void StartWatcher(int fd, Handle<Value> recv_cb, Handle<Value> writable_cb) {
  // start listening for incoming dgrams
  SocketContext* sc = new SocketContext;
  NanAssignPersistent(sc->recv_cb_, recv_cb.As<Function>());
  NanAssignPersistent(sc->writable_cb_, writable_cb.As<Function>());
  sc->fd_ = fd;

  uv_poll_init(uv_default_loop(), &sc->handle_, fd);
  uv_poll_start(&sc->handle_, UV_READABLE, OnEvent);

  // so we can disarm the watcher when close(fd) is called
  watchers.insert(watchers_t::value_type(fd, sc));
}


void FreeSocketContext(uv_handle_t* handle) {
  SocketContext* sc = container_of(handle, SocketContext, handle_);
  delete sc;
}


void StopWatcher(int fd) {
  watchers_t::iterator iter = watchers.find(fd);
  assert(iter != watchers.end());

  SocketContext* sc = iter->second;
  NanDisposePersistent(sc->recv_cb_);
  NanDisposePersistent(sc->writable_cb_);
  watchers.erase(iter);

  uv_poll_stop(&sc->handle_);
  uv_close(reinterpret_cast<uv_handle_t*>(&sc->handle_), FreeSocketContext);
}


NAN_METHOD(Socket) {
  NanScope();
  Local<Value> recv_cb;
  Local<Value> writable_cb;
  int protocol;
  int domain;
  int type;
  int fd;

  assert(args.Length() == 5);

  domain      = args[0]->Int32Value();
  type        = args[1]->Int32Value();
  protocol    = args[2]->Int32Value();
  recv_cb     = args[3];
  writable_cb = args[4];

#if defined(SOCK_NONBLOCK)
  type |= SOCK_NONBLOCK;
#endif
#if defined(SOCK_CLOEXEC)
  type |= SOCK_CLOEXEC;
#endif

  fd = socket(domain, type, protocol);
  if (fd == -1) {
    fd = -errno;
    goto out;
  }

 #if !defined(SOCK_NONBLOCK)
  SetNonBlock(fd);
#endif
#if !defined(SOCK_CLOEXEC)
  SetCloExec(fd);
#endif

  StartWatcher(fd, recv_cb, writable_cb);

out:
  NanReturnValue(NanNew(fd));
}


NAN_METHOD(Bind) {
  NanScope();
  const sockaddr* s;
  int err;
  int fd;
  Local<Object> buf;

  assert(args.Length() == 2);

  fd = args[0]->Int32Value();
  buf = args[1]->ToObject();

  assert(node::Buffer::HasInstance(buf));

  s = reinterpret_cast<const sockaddr*>(node::Buffer::Data(buf));

  err = 0;
  if (bind(fd, s, node::Buffer::Length(buf)))
    err = -errno;

  NanReturnValue(NanNew(err));
}

NAN_METHOD(SendTo) {
  NanScope();
  Local<Object> buf;
  Local<Object> buf_sockaddr_un;
  sockaddr* s;
  size_t offset;
  size_t length;
  msghdr msg;
  iovec iov;
  int err;
  int fd;
  int r;

  assert(args.Length() == 5);

  fd = args[0]->Int32Value();
  buf = args[1]->ToObject();
  offset = args[2]->Uint32Value();
  length = args[3]->Uint32Value();
  buf_sockaddr_un = args[4]->ToObject();

  assert(node::Buffer::HasInstance(buf));
  assert(offset + length <= node::Buffer::Length(buf));
  assert(node::Buffer::HasInstance(buf_sockaddr_un));

  iov.iov_base = node::Buffer::Data(buf) + offset;
  iov.iov_len = length;

  s = reinterpret_cast<sockaddr*>(node::Buffer::Data(buf_sockaddr_un));

  memset(&msg, 0, sizeof msg);
  msg.msg_iovlen = 1;
  msg.msg_iov = &iov;
  msg.msg_name = reinterpret_cast<void*>(s);
  msg.msg_namelen = node::Buffer::Length(buf_sockaddr_un);

  do
    r = sendmsg(fd, &msg, 0);
  while (r == -1 && errno == EINTR);

  err = 0;
  if (r == -1)
    err = -errno;

  NanReturnValue(NanNew(err));
}

NAN_METHOD(Send) {
  NanScope();
  Local<Object> buf;
  msghdr msg;
  iovec iov;
  int err;
  int fd;
  int r;

  assert(args.Length() == 2);

  fd = args[0]->Int32Value();
  buf = args[1]->ToObject();
  assert(node::Buffer::HasInstance(buf));

  iov.iov_base = node::Buffer::Data(buf);
  iov.iov_len = node::Buffer::Length(buf);

  memset(&msg, 0, sizeof msg);
  msg.msg_iovlen = 1;
  msg.msg_iov = &iov;

  do
    r = sendmsg(fd, &msg, 0);
  while (r == -1 && errno == EINTR);

  err = 0;
  if (r == -1) {
    err = -errno;
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      watchers_t::iterator iter = watchers.find(fd);
      assert(iter != watchers.end());
      SocketContext* sc = iter->second;
      uv_poll_start(&sc->handle_, UV_READABLE | UV_WRITABLE, OnEvent);
      err = 1;
    }
  }

  NanReturnValue(NanNew(err));
}

NAN_METHOD(Connect) {
  NanScope();
  const sockaddr* s;
  int err;
  int fd;
  Local<Object> buf;

  assert(args.Length() == 2);

  fd = args[0]->Int32Value();
  buf = args[1]->ToObject();

  assert(node::Buffer::HasInstance(buf));

  s = reinterpret_cast<const sockaddr*>(node::Buffer::Data(buf));

  err = 0;
  if (connect(fd, s, node::Buffer::Length(buf)))
    err = -errno;

  NanReturnValue(NanNew(err));
}


NAN_METHOD(Close) {
  NanScope();
  int err;
  int fd;

  assert(args.Length() == 1);
  fd = args[0]->Int32Value();

  // Suppress EINTR and EINPROGRESS.  EINTR means that the close() system call
  // was interrupted by a signal.  According to POSIX, the file descriptor is
  // in an undefined state afterwards.  It's not safe to try closing it again
  // because it may have been closed, despite the signal.  If we call close()
  // again, then it would either:
  //
  //   a) fail with EBADF, or
  //
  //   b) close the wrong file descriptor if another thread or a signal handler
  //      has reused it in the mean time.
  //
  // Neither is what we want but scenario B is particularly bad.  Not retrying
  // the close() could, in theory, lead to file descriptor leaks but, in
  // practice, operating systems do the right thing and close the file
  // descriptor, regardless of whether the operation was interrupted by
  // a signal.
  //
  // EINPROGRESS is benign.  It means the close operation was interrupted but
  // that the file descriptor has been closed or is being closed in the
  // background.  It's informative, not an error.
  err = 0;
  if (close(fd))
    if (errno != EINTR && errno != EINPROGRESS)
      err = -errno;

  StopWatcher(fd);
  NanReturnValue(NanNew(err));
}

NAN_METHOD(ToUnixAddr) {
  NanScope();
  sockaddr_un s;

  assert(args.Length() == 1);

  String::Utf8Value path(args[0]);

  memset(&s, 0, sizeof(s));
  const size_t size = std::min(static_cast<const size_t>(path.length()), sizeof(s.sun_path) - 1);
  memcpy(s.sun_path, *path, size);
  s.sun_family = AF_UNIX;

  const char* addr_buf = reinterpret_cast<const char*>(const_cast<const sockaddr_un*>(&s));
  NanReturnValue(NanNewBufferHandle(addr_buf, sizeof(s)));
}

void Initialize(Handle<Object> target) {
  // don't need to be read-only, only used by the JS shim
  target->Set(NanNew("AF_UNIX"), NanNew(AF_UNIX));
  target->Set(NanNew("SOCK_DGRAM"), NanNew(SOCK_DGRAM));

  target->Set(NanNew("socket"),
              NanNew<FunctionTemplate>(Socket)->GetFunction());

  target->Set(NanNew("bind"),
              NanNew<FunctionTemplate>(Bind)->GetFunction());

  target->Set(NanNew("sendto"),
              NanNew<FunctionTemplate>(SendTo)->GetFunction());

  target->Set(NanNew("send"),
              NanNew<FunctionTemplate>(Send)->GetFunction());

  target->Set(NanNew("connect"),
              NanNew<FunctionTemplate>(Connect)->GetFunction());

  target->Set(NanNew("close"),
              NanNew<FunctionTemplate>(Close)->GetFunction());

  target->Set(NanNew("toUnixAddress"),
              NanNew<FunctionTemplate>(ToUnixAddr)->GetFunction());
}


} // anonymous namespace

NODE_MODULE(unix_dgram, Initialize)
