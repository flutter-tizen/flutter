// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_ENGINE_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_ENGINE_H_

#include <memory>
#include <unordered_map>

#include "flutter/fml/macros.h"
#include "flutter/shell/common/shell.h"
#include "flutter/shell/common/thread_host.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/embedder/embedder_external_texture_resolver.h"
#include "flutter/shell/platform/embedder/embedder_thread_host.h"
namespace flutter {

struct ShellArgs;

// The object that is returned to the embedder as an opaque pointer to the
// instance of the Flutter engine.
class EmbedderEngine {
 public:
  EmbedderEngine(
      std::unique_ptr<EmbedderThreadHost> thread_host,
      const TaskRunners& task_runners,
      const Settings& settings,
      RunConfiguration run_configuration,
      const Shell::CreateCallback<PlatformView>& on_create_platform_view,
      const Shell::CreateCallback<Rasterizer>& on_create_rasterizer,
      std::unique_ptr<EmbedderExternalTextureResolver>
          external_texture_resolver);

  ~EmbedderEngine();

  bool LaunchShell();

  bool CollectShell();

  void CollectThreadHost();

  const TaskRunners& GetTaskRunners() const;

  bool NotifyCreated();

  bool NotifyDestroyed();

  bool RunRootIsolate();

  bool IsValid() const;

  bool SetViewportMetrics(int64_t view_id,
                          const flutter::ViewportMetrics& metrics);

  bool DispatchPointerDataPacket(
      std::unique_ptr<flutter::PointerDataPacket> packet);

  bool SendPlatformMessage(std::unique_ptr<PlatformMessage> message);

  bool RegisterTexture(int64_t texture);

  bool UnregisterTexture(int64_t texture);

  bool MarkTextureFrameAvailable(int64_t texture);

  bool SetSemanticsEnabled(bool enabled);

  bool SetAccessibilityFeatures(int32_t flags);

  bool DispatchSemanticsAction(int64_t view_id,
                               int node_id,
                               flutter::SemanticsAction action,
                               fml::MallocMapping args);

  bool OnVsyncEvent(intptr_t baton,
                    fml::TimePoint frame_start_time,
                    fml::TimePoint frame_target_time);

  bool ReloadSystemFonts();

  bool PostRenderThreadTask(const fml::closure& task);

  bool RunTask(const FlutterTask* task);

  bool PostTaskOnEngineManagedNativeThreads(
      const std::function<void(FlutterNativeThreadType)>& closure) const;

  bool ScheduleFrame();

  Shell& GetShell();

  // --------------------------------------------------------------------------
  // Engine group spawning support. These methods back the |FlutterEngineSpawn|
  // embedder API declared in embedder.h.
  // --------------------------------------------------------------------------

  //----------------------------------------------------------------------------
  /// @brief      Creates an embedder engine that adopts an already running
  ///             shell produced by |Shell::Spawn|. The adopted shell shares
  ///             its task runners (and therefore its threads) with the shell
  ///             it was spawned from, so no thread host is owned.
  ///
  EmbedderEngine(const TaskRunners& task_runners,
                 std::unique_ptr<Shell> running_shell,
                 std::unique_ptr<EmbedderExternalTextureResolver>
                     external_texture_resolver);

  //----------------------------------------------------------------------------
  /// @brief      Spawns a new engine in the same isolate group as this
  ///             engine. The new engine shares this engine's threads, Dart VM
  ///             and isolate snapshot, and is returned in a running state.
  ///             This engine must be running and must outlive the spawned
  ///             engine. Returns nullptr on failure.
  ///
  std::unique_ptr<EmbedderEngine> Spawn(
      RunConfiguration run_configuration,
      const std::string& initial_route,
      const Shell::CreateCallback<PlatformView>& on_create_platform_view,
      const Shell::CreateCallback<Rasterizer>& on_create_rasterizer,
      std::unique_ptr<EmbedderExternalTextureResolver>
          external_texture_resolver);

  //----------------------------------------------------------------------------
  /// @brief      Retains the renderer configuration and the project argument
  ///             fields required to construct the callbacks of engines
  ///             spawned from this engine.
  ///
  void SetSpawnSources(const FlutterRendererConfig& renderer_config,
                       const FlutterProjectArgs& project_args);

  const FlutterRendererConfig& GetSpawnRendererConfig() const;

  const FlutterProjectArgs& GetSpawnProjectArgs() const;

 private:
  std::unique_ptr<EmbedderThreadHost> thread_host_;
  TaskRunners task_runners_;
  RunConfiguration run_configuration_;
  std::unique_ptr<ShellArgs> shell_args_;
  std::unique_ptr<Shell> shell_;
  std::unique_ptr<EmbedderExternalTextureResolver> external_texture_resolver_;

  // Retained spawn sources. See |SetSpawnSources|.
  FlutterRendererConfig renderer_config_ = {};
  FlutterProjectArgs project_args_ = {};

  FML_DISALLOW_COPY_AND_ASSIGN(EmbedderEngine);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_ENGINE_H_
