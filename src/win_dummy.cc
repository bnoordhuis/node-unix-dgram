#include <node.h>

namespace {

using v8::Context;
using v8::Local;
using v8::Object;
using v8::Value;

void Initialize(Local<Object> exports, Local<Value> module_,
                Local<Context> context, void* priv) {
}

}

NODE_MODULE_CONTEXT_AWARE(unix_dgram, Initialize)
