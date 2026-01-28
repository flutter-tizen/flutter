// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/embedder/embedder_external_texture_gl.h"

#include "flutter/fml/logging.h"
#include "impeller/core/texture_descriptor.h"
#include "impeller/display_list/aiks_context.h"
#include "impeller/display_list/dl_image_impeller.h"
#include "impeller/geometry/size.h"
#include "impeller/renderer/backend/gles/context_gles.h"
#include "impeller/renderer/backend/gles/handle_gles.h"
#include "impeller/renderer/backend/gles/texture_gles.h"

#include "include/core/SkPaint.h"
#include "third_party/skia/include/core/SkAlphaType.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/core/SkColorType.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkSize.h"
#include "third_party/skia/include/gpu/ganesh/GrBackendSurface.h"
#include "third_party/skia/include/gpu/ganesh/GrDirectContext.h"
#include "third_party/skia/include/gpu/ganesh/SkImageGanesh.h"
#include "third_party/skia/include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "third_party/skia/include/gpu/ganesh/gl/GrGLTypes.h"

namespace flutter {

std::optional<TextureLRU::Data> TextureLRU::FindTexture(
    std::optional<GLuint> key) {
  if (!key.has_value()) {
    return std::nullopt;
  }
  auto key_value = key.value();
  for (size_t i = 0u; i < kTextureMaxSize; i++) {
    if (textures_[i].key == key_value) {
      auto result = textures_[i].value;
      UpdateTexture(result, key_value, textures_[i].width, textures_[i].height);
      return std::make_optional(textures_[i]);
    }
  }
  return std::nullopt;
}

void TextureLRU::UpdateTexture(
    const std::shared_ptr<impeller::TextureGLES>& texture,
    GLuint key,
    size_t width,
    size_t height) {
  if (textures_[0].key == key) {
    textures_[0].value = texture;
    textures_[0].width = width;
    textures_[0].height = height;
    return;
  }
  size_t i = 1u;
  for (; i < kTextureMaxSize; i++) {
    if (textures_[i].key == key) {
      break;
    }
  }
  for (auto j = i; j > 0; j--) {
    textures_[j] = textures_[j - 1];
  }
  textures_[0] =
      Data{.key = key, .value = texture, .width = width, .height = height};
}

GLuint TextureLRU::AddTexture(
    const std::shared_ptr<impeller::TextureGLES>& texture,
    GLuint key,
    size_t width,
    size_t height) {
  GLuint lru_key = textures_[kTextureMaxSize - 1].key;
  bool updated_image = false;
  for (size_t i = 0u; i < kTextureMaxSize; i++) {
    if (textures_[i].key == lru_key) {
      updated_image = true;
      textures_[i] =
          Data{.key = key, .value = texture, .width = width, .height = height};
      break;
    }
  }
  if (!updated_image) {
    textures_[0] =
        Data{.key = key, .value = texture, .width = width, .height = height};
  }
  UpdateTexture(texture, key, width, height);
  return lru_key;
}

void TextureLRU::Clear() {
  for (size_t i = 0u; i < kTextureMaxSize; i++) {
    textures_[i] = Data{.key = 0u, .value = nullptr};
  }
}

void TextureLRU::RemoveTexture(GLuint key) {
  size_t i = 0u;
  for (; i < kTextureMaxSize; i++) {
    if (textures_[i].key == key) {
      break;
    }
  }

  // If key not found, return
  if (i == kTextureMaxSize) {
    return;
  }

  // Shift all entries after the found entry down by one position
  for (; i < kTextureMaxSize - 1; i++) {
    textures_[i] = textures_[i + 1];
  }

  // Clear the last entry
  textures_[kTextureMaxSize - 1] = Data{.key = 0u, .value = nullptr};
}

EmbedderExternalTextureGL::EmbedderExternalTextureGL(
    int64_t texture_identifier,
    const ExternalTextureCallback& callback)
    : Texture(texture_identifier), external_texture_callback_(callback) {
  FML_DCHECK(external_texture_callback_);
}

EmbedderExternalTextureGL::~EmbedderExternalTextureGL() = default;

// |flutter::Texture|
void EmbedderExternalTextureGL::Paint(PaintContext& context,
                                      const DlRect& bounds,
                                      bool freeze,
                                      const DlImageSampling sampling) {
  if (last_image_ == nullptr) {
    last_image_ =
        ResolveTexture(Id(),                                                 //
                       context.gr_context,                                   //
                       context.aiks_context,                                 //
                       SkISize::Make(bounds.GetWidth(), bounds.GetHeight())  //
        );
  }

  DlCanvas* canvas = context.canvas;
  const DlPaint* paint = context.paint;

  if (last_image_) {
    DlRect image_bounds = DlRect::Make(last_image_->GetBounds());
    if (bounds != image_bounds) {
      canvas->DrawImageRect(last_image_, image_bounds, bounds, sampling, paint);
    } else {
      canvas->DrawImage(last_image_, bounds.GetOrigin(), sampling, paint);
    }
  }
}

sk_sp<DlImage> EmbedderExternalTextureGL::ResolveTexture(
    int64_t texture_id,
    GrDirectContext* context,
    impeller::AiksContext* aiks_context,
    const SkISize& size) {
  if (!!aiks_context) {
    return ResolveTextureImpeller(texture_id, aiks_context, size);
  } else if (!!context) {
    return ResolveTextureSkia(texture_id, context, size);
  } else {
    return nullptr;
  }
}

sk_sp<DlImage> EmbedderExternalTextureGL::ResolveTextureSkia(
    int64_t texture_id,
    GrDirectContext* context,
    const SkISize& size) {
  context->flushAndSubmit();
  context->resetContext(kAll_GrBackendState);
  std::unique_ptr<FlutterOpenGLTexture> texture =
      external_texture_callback_(texture_id, size.width(), size.height());

  if (!texture) {
    return nullptr;
  }

  GrGLTextureInfo gr_texture_info = {texture->target, texture->name,
                                     texture->format};

  size_t width = size.width();
  size_t height = size.height();

  if (texture->width != 0 && texture->height != 0) {
    width = texture->width;
    height = texture->height;
  }

  auto gr_backend_texture = GrBackendTextures::MakeGL(
      width, height, skgpu::Mipmapped::kNo, gr_texture_info);
  SkImages::TextureReleaseProc release_proc = texture->destruction_callback;
  auto image =
      SkImages::BorrowTextureFrom(context,                   // context
                                  gr_backend_texture,        // texture handle
                                  kTopLeft_GrSurfaceOrigin,  // origin
                                  kRGBA_8888_SkColorType,    // color type
                                  kPremul_SkAlphaType,       // alpha type
                                  nullptr,                   // colorspace
                                  release_proc,       // texture release proc
                                  texture->user_data  // texture release context
      );

  if (!image) {
    // In case Skia rejects the image, call the release proc so that
    // embedders can perform collection of intermediates.
    if (release_proc) {
      release_proc(texture->user_data);
    }
    FML_LOG(ERROR) << "Could not create external texture->";
    return nullptr;
  }

  // This image should not escape local use by EmbedderExternalTextureGL
  return DlImage::Make(std::move(image));
}

std::shared_ptr<impeller::TextureGLES>
EmbedderExternalTextureGL::CreateTextureGLES(
    impeller::AiksContext* aiks_context,
    FlutterOpenGLTexture* texture) {
  impeller::TextureDescriptor desc;
  desc.size = impeller::ISize(texture->width, texture->height);
  desc.storage_mode = impeller::StorageMode::kDevicePrivate;
  desc.format = impeller::PixelFormat::kR8G8B8A8UNormInt;
  if (texture->target == GL_TEXTURE_EXTERNAL_OES) {
    desc.type = impeller::TextureType::kTextureExternalOES;
  } else {
    desc.type = impeller::TextureType::kTexture2D;
  }
  impeller::ContextGLES& context =
      impeller::ContextGLES::Cast(*aiks_context->GetContext());
  impeller::HandleGLES handle = context.GetReactor()->CreateHandle(
      impeller::HandleType::kTexture, texture->name);

  auto gles_texture =
      impeller::TextureGLES::WrapTexture(context.GetReactor(), desc, handle);
  if (!gles_texture) {
    // In case Skia rejects the image, call the release proc so that
    // embedders can perform collection of intermediates.
    if (texture->destruction_callback) {
      texture->destruction_callback(texture->user_data);
    }
    FML_LOG(ERROR) << "Could not create external texture";
    return nullptr;
  }

  gles_texture->SetCoordinateSystem(
      impeller::TextureCoordinateSystem::kUploadFromHost);

  if (texture->destruction_callback &&
      !context.GetReactor()->RegisterCleanupCallback(
          handle,
          [callback = texture->destruction_callback,
           user_data = texture->user_data]() { callback(user_data); })) {
    FML_LOG(ERROR) << "Could not register destruction callback";
    return nullptr;
  }
  return gles_texture;
}

sk_sp<DlImage> EmbedderExternalTextureGL::ResolveTextureImpeller(
    int64_t texture_id,
    impeller::AiksContext* aiks_context,
    const SkISize& size) {
  std::unique_ptr<FlutterOpenGLTexture> texture =
      external_texture_callback_(texture_id, size.width(), size.height());

  if (!texture) {
    return nullptr;
  }

  std::optional<TextureLRU::Data> texture_data =
      texture_lru_.FindTexture(texture->name);

  bool size_change = false;

  if (texture_data.has_value() &&
      (texture_data.value().width != texture->width ||
       texture_data.value().height != texture->height)) {
    size_change = true;
  }

  if (texture_data.has_value() && !size_change) {
    return impeller::DlImageImpeller::Make(texture_data.value().value);
  } else if (texture_data.has_value() && size_change) {
    std::shared_ptr<impeller::TextureGLES> old_gles_texture =
        texture_data.value().value;
    old_gles_texture->Leak();
    std::shared_ptr<impeller::TextureGLES> new_gles_texture =
        CreateTextureGLES(aiks_context, texture.get());
    if (new_gles_texture) {
      texture_lru_.UpdateTexture(new_gles_texture, texture->name, texture->width,
                                 texture->height);
      return impeller::DlImageImpeller::Make(new_gles_texture);
    } else {
      texture_lru_.RemoveTexture(texture->name);
      return nullptr;
    }
  } else {
    std::shared_ptr<impeller::TextureGLES> new_gles_texture =
        CreateTextureGLES(aiks_context, texture.get());
    if (new_gles_texture) {
      texture_lru_.AddTexture(new_gles_texture, texture->name, texture->width,
                              texture->height);
      return impeller::DlImageImpeller::Make(new_gles_texture);
    } else {
      return nullptr;
    }
  }
}

// |flutter::Texture|
void EmbedderExternalTextureGL::OnGrContextCreated() {}

// |flutter::Texture|
void EmbedderExternalTextureGL::OnGrContextDestroyed() {}

// |flutter::Texture|
void EmbedderExternalTextureGL::MarkNewFrameAvailable() {
  last_image_ = nullptr;
}

// |flutter::Texture|
void EmbedderExternalTextureGL::OnTextureUnregistered() {
  texture_lru_.Clear();
}

}  // namespace flutter
