// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_THREAD_HOST_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_THREAD_HOST_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "flutter/common/task_runners.h"
#include "flutter/fml/macros.h"
#include "flutter/shell/common/thread_host.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/embedder/embedder_task_runner.h"

namespace flutter {

class EmbedderThreadHost {
 public:
  static std::unique_ptr<EmbedderThreadHost>
  CreateEmbedderOrEngineManagedThreadHost(
      const FlutterCustomTaskRunners* custom_task_runners,
      const flutter::ThreadConfigSetter& config_setter =
          fml::Thread::SetCurrentThreadName);

  EmbedderThreadHost(
      ThreadHost host,
      const flutter::TaskRunners& runners,
      const std::set<fml::RefPtr<EmbedderTaskRunner>>& embedder_task_runners);

  ~EmbedderThreadHost();

  bool IsValid() const;

  const flutter::TaskRunners& GetTaskRunners() const;

  bool PostTask(intptr_t runner, uint64_t task) const;

  static bool RunnerIsValid(intptr_t runner);

  using MicrotaskFlushCallback = std::function<void()>;

  // Registers an engine whose microtasks must be flushed after a task runs on
  // the custom UI task runner. Returns zero for an invalid callback.
  uint64_t RegisterMicrotaskFlushCallback(MicrotaskFlushCallback callback);

  void UnregisterMicrotaskFlushCallback(uint64_t callback_id);

  // Flushes all engines registered with this shared thread host. Must be
  // called on the custom UI task runner after executing a task.
  void FlushMicrotaskQueues();

  void InvalidateActiveRunners();

 private:
  ThreadHost host_;
  flutter::TaskRunners runners_;
  std::map<intptr_t, fml::RefPtr<EmbedderTaskRunner>> runners_map_;
  std::mutex microtask_flush_mutex_;
  uint64_t next_microtask_flush_callback_id_ = 0;
  std::map<uint64_t, MicrotaskFlushCallback> microtask_flush_callbacks_;

  static std::set<intptr_t> active_runners_;
  static std::mutex active_runners_mutex_;

  static std::unique_ptr<EmbedderThreadHost> CreateEmbedderManagedThreadHost(
      const FlutterCustomTaskRunners* custom_task_runners,
      const flutter::ThreadConfigSetter& config_setter =
          fml::Thread::SetCurrentThreadName);

  static std::unique_ptr<EmbedderThreadHost> CreateEngineManagedThreadHost(
      const flutter::ThreadConfigSetter& config_setter =
          fml::Thread::SetCurrentThreadName);

  FML_DISALLOW_COPY_AND_ASSIGN(EmbedderThreadHost);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_THREAD_HOST_H_
