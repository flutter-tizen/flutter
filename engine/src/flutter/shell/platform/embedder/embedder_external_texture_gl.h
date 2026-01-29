// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_EXTERNAL_TEXTURE_GL_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_EXTERNAL_TEXTURE_GL_H_

#include <list>
#include <memory>
#include <unordered_map>
#include "flutter/common/graphics/texture.h"
#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "impeller/renderer/backend/gles/texture_gles.h"
#include "third_party/skia/include/core/SkSize.h"

namespace flutter {
static constexpr size_t kTextureMaxSize = 6u;

class TextureLRU {
 public:
  struct Data {
    GLuint key = 0u;
    std::shared_ptr<impeller::TextureGLES> value;
    size_t width = 0;
    size_t height = 0;
  };

  TextureLRU() = default;

  ~TextureLRU() = default;

  /// @brief Retrieve the Texture associated with the given [key], or nullptr.
  std::optional<Data> FindTexture(std::optional<GLuint> key);

  /// @brief Add a new texture to the cache with a key, returning the key of the
  ///        LRU entry that was removed.
  ///
  /// The value may be `0`, in which case nothing was removed.
  GLuint AddTexture(Data data);

  /// @brief Remove all entires from the image cache.
  void Clear();

  /// @brief Remove a texture from the cache by key.
  void RemoveTexture(GLuint key);

  /// @brief Marks [key] as the most recently used.
  void UpdateTexture(Data data);

 private:
  std::array<Data, kTextureMaxSize> textures_;
};

class EmbedderExternalTextureGL : public flutter::Texture {
 public:
  using ExternalTextureCallback = std::function<
      std::unique_ptr<FlutterOpenGLTexture>(int64_t, size_t, size_t)>;

  EmbedderExternalTextureGL(int64_t texture_identifier,
                            const ExternalTextureCallback& callback);

  ~EmbedderExternalTextureGL();

 private:
  const ExternalTextureCallback& external_texture_callback_;
  sk_sp<DlImage> last_image_;
  TextureLRU texture_lru_ = TextureLRU();
  sk_sp<DlImage> ResolveTexture(int64_t texture_id,
                                GrDirectContext* context,
                                impeller::AiksContext* aiks_context,
                                const SkISize& size);

  sk_sp<DlImage> ResolveTextureSkia(int64_t texture_id,
                                    GrDirectContext* context,
                                    const SkISize& size);

  sk_sp<DlImage> ResolveTextureImpeller(int64_t texture_id,
                                        impeller::AiksContext* aiks_context,
                                        const SkISize& size);

  std::shared_ptr<impeller::TextureGLES> CreateTextureGLES(
      impeller::AiksContext* aiks_context,
      FlutterOpenGLTexture* texture);

  // |flutter::Texture|
  void Paint(PaintContext& context,
             const DlRect& bounds,
             bool freeze,
             const DlImageSampling sampling) override;

  // |flutter::Texture|
  void OnGrContextCreated() override;

  // |flutter::Texture|
  void OnGrContextDestroyed() override;

  // |flutter::Texture|
  void MarkNewFrameAvailable() override;

  // |flutter::Texture|
  void OnTextureUnregistered() override;

  FML_DISALLOW_COPY_AND_ASSIGN(EmbedderExternalTextureGL);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_EXTERNAL_TEXTURE_GL_H_
