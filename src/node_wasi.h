#ifndef SRC_NODE_WASI_H_
#define SRC_NODE_WASI_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object-inl.h"
#include "uvwasi.h"

namespace node {
namespace wasi {


class WASI : public BaseObject {
 public:
  WASI(Environment* env,
       v8::Local<v8::Object> object,
       v8::Local<v8::Value> memory,
       uvwasi_options_t* options);
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(WASI)
  SET_SELF_SIZE(WASI)

  static void ArgsGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ArgsSizesGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ClockResGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ClockTimeGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void EnvironGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void EnvironSizesGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdAdvise(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdAllocate(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdClose(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdDatasync(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdFdstatGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdFdstatSetFlags(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdFdstatSetRights(
    const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdFilestatGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdFilestatSetSize(
    const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdFilestatSetTimes(
    const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdPread(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdPrestatGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdPrestatDirName(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdPwrite(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdRead(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdReaddir(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdRenumber(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdSeek(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdSync(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdTell(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void FdWrite(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathCreateDirectory(
    const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathFilestatGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathFilestatSetTimes(
    const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathLink(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathOpen(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathReadlink(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathRemoveDirectory(
    const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathRename(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathSymlink(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PathUnlinkFile(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void PollOneoff(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ProcExit(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ProcRaise(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void RandomGet(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SchedYield(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SockRecv(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SockSend(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SockShutdown(const v8::FunctionCallbackInfo<v8::Value>& args);

 private:
  ~WASI() override;
  inline uvwasi_errno_t writeUInt32(uint32_t value, uint32_t offset);
  uvwasi_t uvw_;
  v8::Persistent<v8::ArrayBufferView> memory_;
};


}  // namespace wasi
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_WASI_H_
