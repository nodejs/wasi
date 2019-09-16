#include "env-inl.h"
#include "util-inl.h"
#include "node.h"
#include "uv.h"
#include "uvwasi.h"
#include "node_wasi.h"

namespace node {
namespace wasi {

using v8::Array;
using v8::ArrayBuffer;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Uint32;
using v8::Value;


WASI::WASI(Environment* env,
           Local<Object> object,
           Local<Value> memory,
           uvwasi_options_t* options) : BaseObject(env, object) {
  /* uvwasi_errno_t err = */ uvwasi_init(&uvw_, options);

  memory_.Reset(env->isolate(), memory.As<ArrayBuffer>());
}


WASI::~WASI() {
  /* TODO(cjihrig): Free memory. */
}


void WASI::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  CHECK_EQ(args.Length(), 4);
  CHECK(args[0]->IsArray());
  CHECK(args[1]->IsArray());
  // CHECK(args[2]->IsArray());
  CHECK(args[3]->IsArrayBuffer() || args[3]->IsSharedArrayBuffer());

  Environment* env = Environment::GetCurrent(args);
  Local<Context> context = env->context();
  Local<Array> argv = args[0].As<Array>();
  const uint32_t argc = argv->Length();
  uvwasi_options_t options;

  options.fd_table_size = 3;
  options.argc = argc;
  options.argv = argc == 0 ? nullptr : new char*[argc];

  for (uint32_t i = 0; i < argc; i++) {
    auto arg = argv->Get(context, i).ToLocalChecked();
    CHECK(arg->IsString());
    node::Utf8Value str(env->isolate(), arg);
    options.argv[i] = strdup(*str);
    CHECK_NOT_NULL(options.argv[i]);
  }

  Local<Array> env_pairs = args[1].As<Array>();
  const uint32_t envc = env_pairs->Length();
  options.envp = new char*[envc + 1];
  for (uint32_t i = 0; i < envc; i++) {
    auto pair = env_pairs->Get(context, i).ToLocalChecked();
    CHECK(pair->IsString());
    node::Utf8Value str(env->isolate(), pair);
    options.envp[i] = strdup(*str);
    CHECK_NOT_NULL(options.envp[i]);
  }
  options.envp[envc] = nullptr;

  // TODO(cjihrig): Process the preopens for real.
  options.preopenc = 1;
  options.preopens =
    static_cast<uvwasi_preopen_t*>(calloc(1, sizeof(uvwasi_preopen_t)));
  options.preopens[0].mapped_path = "/sandbox";
  options.preopens[0].real_path = ".";

  new WASI(env, args.This(), args[3], &options);

  if (options.argv != nullptr) {
    for (uint32_t i = 0; i < argc; i++)
      free(options.argv[i]);
    delete[] options.argv;
  }

  if (options.envp != nullptr) {
    for (uint32_t i = 0; options.envp[i]; i++)
      free(options.envp[i]);
    delete[] options.envp;
  }
}


void WASI::ArgsGet(const FunctionCallbackInfo<Value>& args) {
  WASI* wasi;
  CHECK_EQ(args.Length(), 2);
  CHECK(args[0]->IsUint32());
  CHECK(args[1]->IsUint32());
  ASSIGN_OR_RETURN_UNWRAP(&wasi, args.This());
  Environment* env = wasi->env();
  Local<ArrayBuffer> ab = PersistentToLocal::Default(env->isolate(),
                                                     wasi->memory_);

  // TODO(cjihrig): Check for buffer overflows.

  uint32_t argv_offset = args[0].As<Uint32>()->Value();
  uint32_t argv_buf_offset = args[1].As<Uint32>()->Value();
  char* buf = static_cast<char*>(ab->GetContents().Data());
  char** argv = new char*[wasi->uvw_.argc];
  char* argv_buf = &buf[argv_buf_offset];
  uvwasi_errno_t err = uvwasi_args_get(&wasi->uvw_, argv, argv_buf);

  if (err == UVWASI_ESUCCESS) {
    for (size_t i = 0; i < wasi->uvw_.argc; i++) {
      uint32_t offset = argv_buf_offset + (argv[i] - argv[0]);
      err = wasi->writeUInt32(offset, argv_offset + (i * 4));
    }
  }

  delete[] argv;
  args.GetReturnValue().Set(err);
}


void WASI::ArgsSizesGet(const FunctionCallbackInfo<Value>& args) {
  WASI* wasi;
  size_t argc;
  size_t argv_buf_size;
  CHECK_EQ(args.Length(), 2);
  CHECK(args[0]->IsUint32());
  CHECK(args[1]->IsUint32());
  ASSIGN_OR_RETURN_UNWRAP(&wasi, args.This());
  uvwasi_errno_t err = uvwasi_args_sizes_get(&wasi->uvw_,
                                             &argc,
                                             &argv_buf_size);
  if (err == UVWASI_ESUCCESS)
    err = wasi->writeUInt32(argc, args[0].As<Uint32>()->Value());

  if (err == UVWASI_ESUCCESS)
    err = wasi->writeUInt32(argv_buf_size, args[1].As<Uint32>()->Value());

  args.GetReturnValue().Set(err);
}


void WASI::ClockResGet(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::ClockTimeGet(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::EnvironGet(const FunctionCallbackInfo<Value>& args) {
  WASI* wasi;
  CHECK_EQ(args.Length(), 2);
  CHECK(args[0]->IsUint32());
  CHECK(args[1]->IsUint32());
  ASSIGN_OR_RETURN_UNWRAP(&wasi, args.This());
  Environment* env = wasi->env();
  Local<ArrayBuffer> ab = PersistentToLocal::Default(env->isolate(),
                                                     wasi->memory_);

  // TODO(cjihrig): Check for buffer overflows.

  uint32_t environ_offset = args[0].As<Uint32>()->Value();
  uint32_t environ_buf_offset = args[1].As<Uint32>()->Value();
  char* buf = static_cast<char*>(ab->GetContents().Data());
  char** environ = new char*[wasi->uvw_.envc];
  char* environ_buf = &buf[environ_buf_offset];
  uvwasi_errno_t err = uvwasi_environ_get(&wasi->uvw_, environ, environ_buf);

  if (err == UVWASI_ESUCCESS) {
    for (size_t i = 0; i < wasi->uvw_.envc; i++) {
      uint32_t offset = environ_buf_offset + (environ[i] - environ[0]);
      err = wasi->writeUInt32(offset, environ_offset + (i * 4));
    }
  }

  delete[] environ;
  args.GetReturnValue().Set(err);
}


void WASI::EnvironSizesGet(const FunctionCallbackInfo<Value>& args) {
  WASI* wasi;
  size_t envc;
  size_t env_buf_size;
  CHECK_EQ(args.Length(), 2);
  CHECK(args[0]->IsUint32());
  CHECK(args[1]->IsUint32());
  ASSIGN_OR_RETURN_UNWRAP(&wasi, args.This());
  uvwasi_errno_t err = uvwasi_environ_sizes_get(&wasi->uvw_,
                                                &envc,
                                                &env_buf_size);
  if (err == UVWASI_ESUCCESS)
    err = wasi->writeUInt32(envc, args[0].As<Uint32>()->Value());

  if (err == UVWASI_ESUCCESS)
    err = wasi->writeUInt32(env_buf_size, args[1].As<Uint32>()->Value());

  args.GetReturnValue().Set(err);
}


void WASI::FdAdvise(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdAllocate(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdClose(const FunctionCallbackInfo<Value>& args) {
  WASI* wasi;
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsUint32());
  ASSIGN_OR_RETURN_UNWRAP(&wasi, args.This());
  uint32_t fd = args[0].As<Uint32>()->Value();
  uvwasi_errno_t err = uvwasi_fd_close(&wasi->uvw_, fd);
  args.GetReturnValue().Set(err);
}


void WASI::FdDatasync(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdFdstatGet(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdFdstatSetFlags(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdFdstatSetRights(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdFilestatGet(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdFilestatSetSize(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdFilestatSetTimes(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdPread(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdPrestatGet(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdPrestatDirName(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdPwrite(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdRead(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdReaddir(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdRenumber(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdSeek(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdSync(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdTell(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::FdWrite(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathCreateDirectory(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathFilestatGet(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathFilestatSetTimes(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathLink(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathOpen(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathReadlink(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathRemoveDirectory(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathRename(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathSymlink(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PathUnlinkFile(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::PollOneoff(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::ProcExit(const FunctionCallbackInfo<Value>& args) {
  WASI* wasi;
  CHECK_EQ(args.Length(), 1);
  CHECK(args[0]->IsUint32());
  ASSIGN_OR_RETURN_UNWRAP(&wasi, args.This());
  uint32_t code = args[0].As<Uint32>()->Value();
  args.GetReturnValue().Set(uvwasi_proc_exit(&wasi->uvw_, code));
}


void WASI::ProcRaise(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::RandomGet(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::SchedYield(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::SockRecv(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::SockSend(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


void WASI::SockShutdown(const FunctionCallbackInfo<Value>& args) {
  args.GetReturnValue().Set(UVWASI_ENOTSUP);
}


inline uvwasi_errno_t WASI::writeUInt32(uint32_t value, uint32_t offset) {
  Environment* env = this->env();
  Local<ArrayBuffer> ab = PersistentToLocal::Default(env->isolate(),
                                                     this->memory_);
  uint8_t* buf = static_cast<uint8_t*>(ab->GetContents().Data());
  // Bounds check. UVWASI_EOVERFLOW

  buf[offset++] = value & 0xFF;
  buf[offset++] = (value >> 8) & 0xFF;
  buf[offset++] = (value >> 16) & 0xFF;
  buf[offset] = (value >> 24) & 0xFF;
  return UVWASI_ESUCCESS;
}


static void Initialize(Local<Object> target,
                       Local<Value> unused,
                       Local<Context> context,
                       void* priv) {
  Environment* env = Environment::GetCurrent(context);

  Local<FunctionTemplate> tmpl = env->NewFunctionTemplate(WASI::New);
  auto wasi_wrap_string = FIXED_ONE_BYTE_STRING(env->isolate(), "WASI");
  tmpl->InstanceTemplate()->SetInternalFieldCount(1);
  tmpl->SetClassName(wasi_wrap_string);

  env->SetProtoMethod(tmpl, "args_get", WASI::ArgsGet);
  env->SetProtoMethod(tmpl, "args_sizes_get", WASI::ArgsSizesGet);
  env->SetProtoMethod(tmpl, "clock_res_get", WASI::ClockResGet);
  env->SetProtoMethod(tmpl, "clock_time_get", WASI::ClockTimeGet);
  env->SetProtoMethod(tmpl, "environ_get", WASI::EnvironGet);
  env->SetProtoMethod(tmpl, "environ_sizes_get", WASI::EnvironSizesGet);
  env->SetProtoMethod(tmpl, "fd_advise", WASI::FdAdvise);
  env->SetProtoMethod(tmpl, "fd_allocate", WASI::FdAllocate);
  env->SetProtoMethod(tmpl, "fd_close", WASI::FdClose);
  env->SetProtoMethod(tmpl, "fd_datasync", WASI::FdDatasync);
  env->SetProtoMethod(tmpl, "fd_fdstat_get", WASI::FdFdstatGet);
  env->SetProtoMethod(tmpl, "fd_fdstat_set_flags", WASI::FdFdstatSetFlags);
  env->SetProtoMethod(tmpl, "fd_fdstat_set_rights", WASI::FdFdstatSetRights);
  env->SetProtoMethod(tmpl, "fd_filestat_get", WASI::FdFilestatGet);
  env->SetProtoMethod(tmpl, "fd_filestat_set_size", WASI::FdFilestatSetSize);
  env->SetProtoMethod(tmpl, "fd_filestat_set_times", WASI::FdFilestatSetTimes);
  env->SetProtoMethod(tmpl, "fd_pread", WASI::FdPread);
  env->SetProtoMethod(tmpl, "fd_prestat_get", WASI::FdPrestatGet);
  env->SetProtoMethod(tmpl, "fd_prestat_dir_name", WASI::FdPrestatDirName);
  env->SetProtoMethod(tmpl, "fd_pwrite", WASI::FdPwrite);
  env->SetProtoMethod(tmpl, "fd_read", WASI::FdRead);
  env->SetProtoMethod(tmpl, "fd_readdir", WASI::FdReaddir);
  env->SetProtoMethod(tmpl, "fd_renumber", WASI::FdRenumber);
  env->SetProtoMethod(tmpl, "fd_seek", WASI::FdSeek);
  env->SetProtoMethod(tmpl, "fd_sync", WASI::FdSync);
  env->SetProtoMethod(tmpl, "fd_tell", WASI::FdTell);
  env->SetProtoMethod(tmpl, "fd_write", WASI::FdWrite);
  env->SetProtoMethod(tmpl, "path_create_directory", WASI::PathCreateDirectory);
  env->SetProtoMethod(tmpl, "path_filestat_get", WASI::PathFilestatGet);
  env->SetProtoMethod(tmpl,
                      "path_filestat_set_times",
                      WASI::PathFilestatSetTimes);
  env->SetProtoMethod(tmpl, "path_link", WASI::PathLink);
  env->SetProtoMethod(tmpl, "path_open", WASI::PathOpen);
  env->SetProtoMethod(tmpl, "path_readlink", WASI::PathReadlink);
  env->SetProtoMethod(tmpl, "path_remove_directory", WASI::PathRemoveDirectory);
  env->SetProtoMethod(tmpl, "path_rename", WASI::PathRename);
  env->SetProtoMethod(tmpl, "path_symlink", WASI::PathSymlink);
  env->SetProtoMethod(tmpl, "path_unlink_file", WASI::PathUnlinkFile);
  env->SetProtoMethod(tmpl, "poll_oneoff", WASI::PollOneoff);
  env->SetProtoMethod(tmpl, "proc_exit", WASI::ProcExit);
  env->SetProtoMethod(tmpl, "proc_raise", WASI::ProcRaise);
  env->SetProtoMethod(tmpl, "random_get", WASI::RandomGet);
  env->SetProtoMethod(tmpl, "sched_yield", WASI::SchedYield);
  env->SetProtoMethod(tmpl, "sock_recv", WASI::SockRecv);
  env->SetProtoMethod(tmpl, "sock_send", WASI::SockSend);
  env->SetProtoMethod(tmpl, "sock_shutdown", WASI::SockShutdown);

  target->Set(env->context(),
              wasi_wrap_string,
              tmpl->GetFunction(context).ToLocalChecked()).ToChecked();
}


}  // namespace wasi
}  // namespace node

NODE_MODULE_CONTEXT_AWARE_INTERNAL(wasi, node::wasi::Initialize)
