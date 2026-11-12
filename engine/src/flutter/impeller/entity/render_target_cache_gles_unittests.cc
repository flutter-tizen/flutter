// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "flutter/testing/testing.h"
#include "gmock/gmock.h"
#include "impeller/entity/render_target_cache.h"
#include "impeller/renderer/testing/mocks.h"

namespace impeller {
namespace testing {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

namespace {

class ValidTextureAllocator : public Allocator {
 public:
  ISize GetMaxTextureSizeSupported() const override { return {4096, 4096}; }

  std::shared_ptr<DeviceBuffer> OnCreateBuffer(
      const DeviceBufferDescriptor& desc) override {
    return std::make_shared<MockDeviceBuffer>(desc);
  }

  std::shared_ptr<Texture> OnCreateTexture(const TextureDescriptor& desc,
                                           bool threadsafe) override {
    auto texture = std::make_shared<NiceMock<MockTexture>>(desc);
    ON_CALL(*texture, IsValid()).WillByDefault(Return(true));
    ON_CALL(*texture, GetSize()).WillByDefault(Return(desc.size));
    return texture;
  }
};

struct Fixture {
  explicit Fixture(Context::BackendType backend, bool implicit_msaa = false) {
    auto mock_capabilities = std::make_shared<NiceMock<MockCapabilities>>();
    ON_CALL(*mock_capabilities, SupportsOffscreenMSAA())
        .WillByDefault(Return(true));
    ON_CALL(*mock_capabilities, SupportsImplicitResolvingMSAA())
        .WillByDefault(Return(implicit_msaa));
    ON_CALL(*mock_capabilities, GetDefaultColorFormat())
        .WillByDefault(Return(PixelFormat::kR8G8B8A8UNormInt));
    ON_CALL(*mock_capabilities, GetDefaultDepthStencilFormat())
        .WillByDefault(Return(PixelFormat::kD24UnormS8Uint));
    capabilities = mock_capabilities;
    ON_CALL(context, GetBackendType()).WillByDefault(Return(backend));
    ON_CALL(context, GetCapabilities()).WillByDefault(ReturnRef(capabilities));
  }

  std::shared_ptr<const Capabilities> capabilities;
  NiceMock<MockImpellerContext> context;
  std::shared_ptr<Allocator> allocator =
      std::make_shared<ValidTextureAllocator>();
};

}  // namespace

TEST(RenderTargetCacheGLESTest, SharesTransientMSAAAttachments) {
  Fixture fixture(Context::BackendType::kOpenGLES);
  RenderTargetCache cache(fixture.allocator, /*keep_alive_frame_count=*/0);

  cache.Start();
  RenderTarget a = cache.CreateOffscreenMSAA(fixture.context, {100, 100}, 1);
  RenderTarget b = cache.CreateOffscreenMSAA(fixture.context, {100, 100}, 1);
  RenderTarget other_size =
      cache.CreateOffscreenMSAA(fixture.context, {50, 100}, 1);
  cache.End();

  ASSERT_TRUE(a.IsValid() && b.IsValid() && other_size.IsValid());
  EXPECT_EQ(a.GetColorAttachment(0).texture, b.GetColorAttachment(0).texture);
  EXPECT_EQ(a.GetDepthAttachment()->texture, b.GetDepthAttachment()->texture);
  // Each layer keeps its own contents.
  EXPECT_NE(a.GetColorAttachment(0).resolve_texture,
            b.GetColorAttachment(0).resolve_texture);
  EXPECT_NE(a.GetColorAttachment(0).texture,
            other_size.GetColorAttachment(0).texture);
  EXPECT_EQ(cache.CachedTextureCount(), 3u);

  // The next frame reuses the targets and keeps them shared.
  cache.Start();
  RenderTarget a2 = cache.CreateOffscreenMSAA(fixture.context, {100, 100}, 1);
  RenderTarget b2 = cache.CreateOffscreenMSAA(fixture.context, {100, 100}, 1);
  cache.End();
  EXPECT_EQ(a2.GetColorAttachment(0).texture, b2.GetColorAttachment(0).texture);
  EXPECT_NE(a2.GetColorAttachment(0).resolve_texture,
            b2.GetColorAttachment(0).resolve_texture);
  // The unused size was dropped (keep_alive_frame_count is 0).
  EXPECT_EQ(cache.CachedTextureCount(), 2u);
}

TEST(RenderTargetCacheGLESTest, DoesNotShareWithImplicitMSAAOrOtherBackends) {
  for (auto [backend, implicit] :
       {std::pair{Context::BackendType::kOpenGLES, true},
        std::pair{Context::BackendType::kVulkan, false},
        std::pair{Context::BackendType::kMetal, false}}) {
    Fixture fixture(backend, implicit);
    RenderTargetCache cache(fixture.allocator, /*keep_alive_frame_count=*/0);

    cache.Start();
    RenderTarget a = cache.CreateOffscreenMSAA(fixture.context, {100, 100}, 1);
    RenderTarget b = cache.CreateOffscreenMSAA(fixture.context, {100, 100}, 1);
    cache.End();

    EXPECT_NE(a.GetColorAttachment(0).texture, b.GetColorAttachment(0).texture);
    EXPECT_NE(a.GetDepthAttachment()->texture, b.GetDepthAttachment()->texture);
  }
}

}  // namespace testing
}  // namespace impeller
