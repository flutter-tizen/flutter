// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/embedder/embedder_external_texture_vulkan.h"

#include "flutter/fml/logging.h"
#include "flutter/impeller/display_list/dl_image_impeller.h"
#include "flutter/impeller/renderer/backend/vulkan/command_buffer_vk.h"
#include "flutter/shell/platform/embedder/embedder_external_texture_source_vulkan.h"
#include "impeller/core/texture_descriptor.h"
#include "impeller/display_list/aiks_context.h"
#include "impeller/renderer/backend/vulkan/texture_vk.h"
#include "impeller/renderer/context.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "third_party/skia/include/core/SkAlphaType.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/core/SkColorType.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkSize.h"
#include "third_party/skia/include/gpu/ganesh/GrBackendSurface.h"
#include "third_party/skia/include/gpu/ganesh/GrDirectContext.h"
#include "third_party/skia/include/gpu/ganesh/SkImageGanesh.h"
#include "third_party/skia/include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "third_party/skia/include/gpu/ganesh/vk/GrVkTypes.h"

namespace flutter {
EmbedderExternalTextureVulkan::EmbedderExternalTextureVulkan(
    int64_t texture_identifier,
    const ExternalTextureCallback& callback)
    : Texture(texture_identifier), external_texture_callback_(callback) {
  FML_DCHECK(external_texture_callback_);
}

// |flutter::Texture|
void EmbedderExternalTextureVulkan::Paint(PaintContext& context,
                                          const SkRect& bounds,
                                          bool freeze,
                                          const DlImageSampling sampling) {
  if (last_image_ == nullptr) {
    last_image_ =
        ResolveTexture(Id(),                                           //
                       context.gr_context,                             //
                       context.aiks_context,                           //
                       SkISize::Make(bounds.width(), bounds.height())  //
        );
  }

  DlCanvas* canvas = context.canvas;
  const DlPaint* paint = context.paint;

  if (last_image_) {
    SkRect image_bounds = SkRect::Make(last_image_->bounds());
    if (bounds != image_bounds) {
      canvas->DrawImageRect(last_image_, image_bounds, bounds, sampling, paint);
    } else {
      canvas->DrawImage(last_image_, SkPoint{bounds.x(), bounds.y()}, sampling,
                        paint);
    }
  }
}

sk_sp<DlImage> EmbedderExternalTextureVulkan::ResolveTexture(
    int64_t texture_id,
    GrDirectContext* context,
    impeller::AiksContext* aiks_context,
    const SkISize& size) {
  if (!!aiks_context) {
    return ResolveTextureImpeller(texture_id, aiks_context, size);
  } else {
    return ResolveTextureSkia(texture_id, context, size);
  }
}

bool IsYUVVkFormat(const VkFormat format) {
  switch (format) {
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
    case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
    case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
    case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
    case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
    case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
    case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
    case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
      return true;

    default:
      return false;
  }
}

sk_sp<DlImage> EmbedderExternalTextureVulkan::ResolveTextureSkia(
    int64_t texture_id,
    GrDirectContext* context,
    const SkISize& size) {
  context->flushAndSubmit();
  context->resetContext(kAll_GrBackendState);
  std::unique_ptr<FlutterVulkanTexture> texture =
      external_texture_callback_(texture_id, size.width(), size.height());

  if (!texture) {
    return nullptr;
  }

  size_t width = size.width();
  size_t height = size.height();

  if (texture->width != 0 && texture->height != 0) {
    width = texture->width;
    height = texture->height;
  }

  GrVkImageInfo image_info = {};
  if (IsYUVVkFormat(static_cast<VkFormat>(texture->format))) {
    FML_LOG(ERROR) << "try to create YUV image-002.....";
    skgpu::VulkanYcbcrConversionInfo ycbcr_info = {
        static_cast<VkFormat>(texture->format),
        0,
        VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709,
        VK_SAMPLER_YCBCR_RANGE_ITU_NARROW,
        VK_CHROMA_LOCATION_COSITED_EVEN,
        VK_CHROMA_LOCATION_COSITED_EVEN,
        VK_FILTER_LINEAR,
        false,
        static_cast<VkFormatFeatureFlags>(texture->format_features)};

    skgpu::VulkanAlloc alloc;
    alloc.fMemory = reinterpret_cast<VkDeviceMemory>(texture->image_memory);
    alloc.fOffset = 0;
    alloc.fSize = texture->alloc_size;

    image_info = {.fImage = reinterpret_cast<VkImage>(texture->image),
                  .fAlloc = alloc,
                  .fImageTiling = VK_IMAGE_TILING_LINEAR,
                  .fImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                  .fFormat = static_cast<VkFormat>(texture->format),
                  .fImageUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                      VK_IMAGE_USAGE_SAMPLED_BIT,
                  .fSampleCount = 1,
                  .fLevelCount = 1,
                  .fYcbcrConversionInfo = ycbcr_info};
  } else {
    image_info = {
        .fImage = reinterpret_cast<VkImage>(texture->image),
        .fImageTiling = VK_IMAGE_TILING_OPTIMAL,
        .fImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .fFormat = static_cast<VkFormat>(texture->format),
        .fImageUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT,
        .fSampleCount = 1,
        .fLevelCount = 1,
    };
  }

  auto gr_backend_texture =
      GrBackendTextures::MakeVk(width, height, image_info);
  FML_LOG(ERROR) << "backend texture isValid : "
                 << gr_backend_texture.isValid();
  // FML_LOG(ERROR) << "backend texture isValid : " << gr_backend_texture.;
  SkImages::TextureReleaseProc release_proc = texture->destruction_callback;
  auto image =
      SkImages::BorrowTextureFrom(context,                   // context
                                  gr_backend_texture,        // texture handle
                                  kTopLeft_GrSurfaceOrigin,  // origin
                                  kRGB_888x_SkColorType,     // color type
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
    FML_LOG(ERROR) << "Could not create external texture.....";
    return nullptr;
  }

  return DlImage::Make(std::move(image));
}

sk_sp<DlImage> EmbedderExternalTextureVulkan::ResolveTextureImpeller(
    int64_t texture_id,
    impeller::AiksContext* aiks_context,
    const SkISize& size) {
  std::unique_ptr<FlutterVulkanTexture> texture_desc =
      external_texture_callback_(texture_id, size.width(), size.height());
  if (!texture_desc) {
    return nullptr;
  }

  const auto& impeller_context =
      impeller::ContextVK::Cast(*aiks_context->GetContext());

  auto texture_source = std::make_shared<EmbedderExternalTextureSourceVulkan>(
      aiks_context->GetContext(), texture_desc.get());

  auto texture = std::make_shared<impeller::TextureVK>(
      aiks_context->GetContext(), texture_source);
  // Transition the layout to shader read.
  {
    auto buffer = impeller_context.CreateCommandBuffer();
    impeller::CommandBufferVK& buffer_vk =
        impeller::CommandBufferVK::Cast(*buffer);

    impeller::BarrierVK barrier;
    barrier.cmd_buffer = buffer_vk.GetCommandBuffer();
    barrier.src_access = impeller::vk::AccessFlagBits::eColorAttachmentWrite |
                         impeller::vk::AccessFlagBits::eTransferWrite;
    barrier.src_stage =
        impeller::vk::PipelineStageFlagBits::eColorAttachmentOutput |
        impeller::vk::PipelineStageFlagBits::eTransfer;
    barrier.dst_access = impeller::vk::AccessFlagBits::eShaderRead;
    barrier.dst_stage = impeller::vk::PipelineStageFlagBits::eFragmentShader;

    barrier.new_layout = impeller::vk::ImageLayout::eShaderReadOnlyOptimal;

    if (!texture_source->SetLayout(barrier).ok()) {
      return nullptr;
    }
    if (!impeller_context.GetCommandQueue()->Submit({buffer}).ok()) {
      return nullptr;
    }
  }

  return impeller::DlImageImpeller::Make(texture);
}

EmbedderExternalTextureVulkan::~EmbedderExternalTextureVulkan() = default;

// |flutter::Texture|
void EmbedderExternalTextureVulkan::OnGrContextCreated() {}

// |flutter::Texture|
void EmbedderExternalTextureVulkan::OnGrContextDestroyed() {}

// |flutter::Texture|
void EmbedderExternalTextureVulkan::MarkNewFrameAvailable() {
  last_image_ = nullptr;
}

// |flutter::Texture|
void EmbedderExternalTextureVulkan::OnTextureUnregistered() {}

}  // namespace flutter