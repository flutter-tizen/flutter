// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>

#include "flutter/display_list/testing/dl_test_snippets.h"
#include "flutter/testing/testing.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "impeller/core/host_buffer.h"
#include "impeller/playground/playground.h"
#include "impeller/playground/playground_test.h"
#include "impeller/renderer/testing/mocks.h"
#include "impeller/typographer/backends/skia/text_frame_skia.h"
#include "impeller/typographer/backends/skia/typographer_context_skia.h"
#include "impeller/typographer/font_glyph_pair.h"
#include "impeller/typographer/lazy_glyph_atlas.h"
#include "impeller/typographer/rectangle_packer.h"
#include "third_party/skia/include/core/SkFont.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkTextBlob.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "txt/platform.h"

// TODO(zanderso): https://github.com/flutter/flutter/issues/127701
// NOLINTBEGIN(bugprone-unchecked-optional-access)

namespace impeller {
namespace testing {

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

using TypographerTest = PlaygroundTest;
INSTANTIATE_PLAYGROUND_SUITE(TypographerTest);

static std::shared_ptr<GlyphAtlas> CreateGlyphAtlas(
    Context& context,
    const TypographerContext* typographer_context,
    HostBuffer& data_host_buffer,
    GlyphAtlas::Type type,
    const Matrix& transform,
    const std::shared_ptr<GlyphAtlasContext>& atlas_context,
    const std::shared_ptr<TextFrame>& frame) {
  RenderableText render_frame{
      .text_frame = frame,
      .origin_transform = transform,
      .properties = GlyphProperties{},
  };
  return typographer_context->CreateGlyphAtlas(context, type, data_host_buffer,
                                               atlas_context, {render_frame});
}

static std::shared_ptr<GlyphAtlas> CreateGlyphAtlas(
    Context& context,
    const TypographerContext* typographer_context,
    HostBuffer& data_host_buffer,
    GlyphAtlas::Type type,
    const Matrix& transform,
    const std::shared_ptr<GlyphAtlasContext>& atlas_context,
    const std::vector<std::shared_ptr<TextFrame>>& frames,
    const std::vector<GlyphProperties>& properties) {
  size_t offset = 0;
  std::vector<RenderableText> render_frames;
  render_frames.reserve(frames.size());
  for (auto& frame : frames) {
    render_frames.emplace_back(frame, transform, properties[offset++]);
  }
  return typographer_context->CreateGlyphAtlas(context, type, data_host_buffer,
                                               atlas_context, render_frames);
}

TEST_P(TypographerTest, CanConvertTextBlob) {
  SkFont font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString(
      "the quick brown fox jumped over the lazy dog.", font);
  ASSERT_TRUE(blob);
  auto frame = MakeTextFrameFromTextBlobSkia(blob);
  ASSERT_EQ(frame->GetRunCount(), 1u);
  for (const auto& run : frame->GetRuns()) {
    ASSERT_TRUE(run.IsValid());
    ASSERT_EQ(run.GetGlyphCount(), 45u);
  }
}

TEST_P(TypographerTest, CanCreateRenderContext) {
  auto context = TypographerContextSkia::Make();
  ASSERT_TRUE(context && context->IsValid());
}

TEST_P(TypographerTest, CanCreateGlyphAtlas) {
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  ASSERT_TRUE(context && context->IsValid());
  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString("hello", sk_font);
  ASSERT_TRUE(blob);
  auto atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob));

  ASSERT_NE(atlas, nullptr);
  ASSERT_NE(atlas->GetTexture(), nullptr);
  ASSERT_EQ(atlas->GetType(), GlyphAtlas::Type::kAlphaBitmap);
  ASSERT_EQ(atlas->GetGlyphCount(), 4llu);

  std::optional<impeller::ScaledFont> first_scaled_font;
  std::optional<impeller::SubpixelGlyph> first_glyph;
  Rect first_rect;
  atlas->IterateGlyphs([&](const ScaledFont& scaled_font,
                           const SubpixelGlyph& glyph,
                           const Rect& rect) -> bool {
    first_scaled_font = scaled_font;
    first_glyph = glyph;
    first_rect = rect;
    return false;
  });

  ASSERT_TRUE(first_scaled_font.has_value());
  ASSERT_TRUE(atlas
                  ->FindFontGlyphBounds(
                      {first_scaled_font.value(), first_glyph.value()})
                  .has_value());
}

TEST_P(TypographerTest, LazyAtlasTracksColor) {
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
#if FML_OS_MACOSX
  auto mapping = flutter::testing::OpenFixtureAsSkData("Apple Color Emoji.ttc");
#else
  auto mapping = flutter::testing::OpenFixtureAsSkData("NotoColorEmoji.ttf");
#endif
  ASSERT_TRUE(mapping);
  sk_sp<SkFontMgr> font_mgr = txt::GetDefaultFontManager();
  SkFont emoji_font(font_mgr->makeFromData(mapping), 50.0);
  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);

  auto blob = SkTextBlob::MakeFromString("hello", sk_font);
  ASSERT_TRUE(blob);
  auto frame = MakeTextFrameFromTextBlobSkia(blob);

  ASSERT_FALSE(frame->GetAtlasType() == GlyphAtlas::Type::kColorBitmap);

  LazyGlyphAtlas lazy_atlas(TypographerContextSkia::Make());

  lazy_atlas.AddTextFrame(frame, {0, 0}, Matrix(), {});

  frame = MakeTextFrameFromTextBlobSkia(
      SkTextBlob::MakeFromString("😀 ", emoji_font));

  ASSERT_TRUE(frame->GetAtlasType() == GlyphAtlas::Type::kColorBitmap);

  lazy_atlas.AddTextFrame(frame, {0, 0}, Matrix(), {});

  // Creates different atlases for color and red bitmap.
  auto color_atlas = lazy_atlas.CreateOrGetGlyphAtlas(
      *GetContext(), *data_host_buffer, GlyphAtlas::Type::kColorBitmap);

  auto bitmap_atlas = lazy_atlas.CreateOrGetGlyphAtlas(
      *GetContext(), *data_host_buffer, GlyphAtlas::Type::kAlphaBitmap);

  ASSERT_FALSE(color_atlas == bitmap_atlas);
}

TEST_P(TypographerTest, GlyphAtlasWithOddUniqueGlyphSize) {
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  ASSERT_TRUE(context && context->IsValid());
  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString("AGH", sk_font);
  ASSERT_TRUE(blob);
  auto atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob));
  ASSERT_NE(atlas, nullptr);
  ASSERT_NE(atlas->GetTexture(), nullptr);

  EXPECT_EQ(atlas->GetTexture()->GetSize().width, 4096u);
  const bool is_gles = GetBackend() == PlaygroundBackend::kOpenGLES ||
                       GetBackend() == PlaygroundBackend::kOpenGLESSDF;
  EXPECT_EQ(atlas->GetTexture()->GetSize().height, is_gles ? 256u : 1024u);
}

TEST_P(TypographerTest, GlyphAtlasIsRecycledIfUnchanged) {
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  ASSERT_TRUE(context && context->IsValid());
  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString("spooky skellingtons", sk_font);
  ASSERT_TRUE(blob);
  auto atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob));
  ASSERT_NE(atlas, nullptr);
  ASSERT_NE(atlas->GetTexture(), nullptr);
  ASSERT_EQ(atlas, atlas_context->GetGlyphAtlas());

  // now attempt to re-create an atlas with the same text blob.

  auto next_atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob));
  ASSERT_EQ(atlas, next_atlas);
  ASSERT_EQ(atlas_context->GetGlyphAtlas(), atlas);
}

TEST_P(TypographerTest, GlyphAtlasWithLotsOfdUniqueGlyphSize) {
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);
  ASSERT_TRUE(context && context->IsValid());

  const char* test_string =
      "QWERTYUIOPASDFGHJKLZXCVBNMqewrtyuiopasdfghjklzxcvbnm,.<>[]{};':"
      "2134567890-=!@#$%^&*()_+"
      "œ∑´®†¥¨ˆøπ““‘‘åß∂ƒ©˙∆˚¬…æ≈ç√∫˜µ≤≥≥≥≥÷¡™£¢∞§¶•ªº–≠⁄€‹›ﬁﬂ‡°·‚—±Œ„´‰Á¨Ø∏”’/"
      "* Í˝ */¸˛Ç◊ı˜Â¯˘¿";

  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString(test_string, sk_font);
  ASSERT_TRUE(blob);

  size_t size_count = 8;
  std::vector<RenderableText> render_frames;
  for (size_t index = 0; index < size_count; index += 1) {
    Scalar scale = 6.0f * index / 10.0f;
    render_frames.emplace_back(MakeTextFrameFromTextBlobSkia(blob),
                               Matrix::MakeScale({scale, scale, 1.0f}),
                               GlyphProperties{});
  };
  auto atlas = context->CreateGlyphAtlas(
      *GetContext(), GlyphAtlas::Type::kAlphaBitmap, *data_host_buffer,
      atlas_context, render_frames);
  ASSERT_NE(atlas, nullptr);
  ASSERT_NE(atlas->GetTexture(), nullptr);

  std::set<uint16_t> unique_glyphs;
  std::vector<uint16_t> total_glyphs;
  atlas->IterateGlyphs([&](const ScaledFont& scaled_font,
                           const SubpixelGlyph& glyph, const Rect& rect) {
    unique_glyphs.insert(glyph.glyph.index);
    total_glyphs.push_back(glyph.glyph.index);
    return true;
  });

  // These numbers may be different due to subpixel positions.
  EXPECT_LE(unique_glyphs.size() * size_count, atlas->GetGlyphCount());
  EXPECT_EQ(total_glyphs.size(), atlas->GetGlyphCount());

  EXPECT_TRUE(atlas->GetGlyphCount() > 0);
  EXPECT_TRUE(atlas->GetTexture()->GetSize().width > 0);
  EXPECT_TRUE(atlas->GetTexture()->GetSize().height > 0);
}

TEST_P(TypographerTest, GlyphAtlasTextureIsRecycledIfUnchanged) {
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);
  ASSERT_TRUE(context && context->IsValid());
  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString("spooky 1", sk_font);
  ASSERT_TRUE(blob);
  auto atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob));
  auto old_packer = atlas_context->GetRectPacker();

  ASSERT_NE(atlas, nullptr);
  ASSERT_NE(atlas->GetTexture(), nullptr);
  ASSERT_EQ(atlas, atlas_context->GetGlyphAtlas());

  auto* first_texture = atlas->GetTexture().get();

  // Now create a new glyph atlas with a nearly identical blob.

  auto blob2 = SkTextBlob::MakeFromString("spooky 2", sk_font);
  auto next_atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob2));
  ASSERT_EQ(atlas, next_atlas);
  auto* second_texture = next_atlas->GetTexture().get();

  auto new_packer = atlas_context->GetRectPacker();

  ASSERT_EQ(second_texture, first_texture);
  ASSERT_EQ(old_packer, new_packer);
}

TEST_P(TypographerTest, GlyphColorIsPartOfCacheKey) {
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
#if FML_OS_MACOSX
  auto mapping = flutter::testing::OpenFixtureAsSkData("Apple Color Emoji.ttc");
#else
  auto mapping = flutter::testing::OpenFixtureAsSkData("NotoColorEmoji.ttf");
#endif
  ASSERT_TRUE(mapping);
  sk_sp<SkFontMgr> font_mgr = txt::GetDefaultFontManager();
  SkFont emoji_font(font_mgr->makeFromData(mapping), 50.0);

  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kColorBitmap);

  // Create two frames with the same character and a different color, expect
  // that it adds a character.
  auto frame = MakeTextFrameFromTextBlobSkia(
      SkTextBlob::MakeFromString("😂", emoji_font));
  auto frame_2 = MakeTextFrameFromTextBlobSkia(
      SkTextBlob::MakeFromString("😂", emoji_font));
  std::vector<GlyphProperties> properties = {
      GlyphProperties{.tone_or_color = Color::Red()},
      GlyphProperties{.tone_or_color = Color::Blue()},
  };

  auto next_atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kColorBitmap, Matrix(), atlas_context,
                       {frame, frame_2}, properties);

  EXPECT_EQ(next_atlas->GetGlyphCount(), 2u);
}

TEST_P(TypographerTest, GlyphColorIsIgnoredForNonEmojiFonts) {
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  sk_sp<SkFontMgr> font_mgr = txt::GetDefaultFontManager();
  sk_sp<SkTypeface> typeface =
      font_mgr->matchFamilyStyle("Arial", SkFontStyle::Normal());
  SkFont sk_font(typeface, 0.5f);

  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kColorBitmap);

  // Create two frames with the same character and a different color, but as a
  // non-emoji font the text frame constructor will ignore it.
  auto frame =
      MakeTextFrameFromTextBlobSkia(SkTextBlob::MakeFromString("A", sk_font));
  auto frame_2 =
      MakeTextFrameFromTextBlobSkia(SkTextBlob::MakeFromString("A", sk_font));
  std::vector<GlyphProperties> properties = {
      GlyphProperties{},
      GlyphProperties{},
  };

  auto next_atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kColorBitmap, Matrix(), atlas_context,
                       {frame, frame_2}, properties);

  EXPECT_EQ(next_atlas->GetGlyphCount(), 1u);
}

TEST_P(TypographerTest, GlyphIsLightIsPartOfCacheKey) {
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);

  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob1 = SkTextBlob::MakeFromString("A", sk_font);
  auto blob2 = SkTextBlob::MakeFromString("A", sk_font);
  auto frame1 = MakeTextFrameFromTextBlobSkia(blob1);
  auto frame2 = MakeTextFrameFromTextBlobSkia(blob2);

  std::vector<GlyphProperties> properties = {
      GlyphProperties{},
      GlyphProperties{},
  };
  // One light color, one dark color
  properties[0].tone_or_color = GlyphProperties::ComputeTone(Color::Beige());
  properties[1].tone_or_color = GlyphProperties::ComputeTone(Color::Crimson());

  auto next_atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       {frame1, frame2}, properties);

#if defined(FML_OS_MACOSX)
  // On Apple platforms, they should be separated into 2 entries
  EXPECT_EQ(next_atlas->GetGlyphCount(), 2u);
#else
  // On other platforms, both are classified as "dark" (false) and share 1 entry
  EXPECT_EQ(next_atlas->GetGlyphCount(), 1u);
#endif
}

TEST_P(TypographerTest, RectanglePackerAddsNonoverlapingRectangles) {
  auto packer = RectanglePacker::Factory(200, 100);
  ASSERT_NE(packer, nullptr);
  ASSERT_EQ(packer->PercentFull(), 0);

  const SkIRect packer_area = SkIRect::MakeXYWH(0, 0, 200, 100);

  IPoint16 first_output = {-1, -1};  // Fill with sentinel values
  ASSERT_TRUE(packer->AddRect(20, 20, &first_output));
  // Make sure the rectangle is placed such that it is inside the bounds of
  // the packer's area.
  const SkIRect first_rect =
      SkIRect::MakeXYWH(first_output.x(), first_output.y(), 20, 20);
  ASSERT_TRUE(SkIRect::Intersects(packer_area, first_rect));

  // Initial area was 200 x 100 = 20_000
  // We added 20x20 = 400. 400 / 20_000 == 0.02 == 2%
  ASSERT_TRUE(flutter::testing::NumberNear(packer->PercentFull(), 0.02));

  IPoint16 second_output = {-1, -1};
  ASSERT_TRUE(packer->AddRect(140, 90, &second_output));
  const SkIRect second_rect =
      SkIRect::MakeXYWH(second_output.x(), second_output.y(), 140, 90);
  // Make sure the rectangle is placed such that it is inside the bounds of
  // the packer's area but not in the are of the first rectangle.
  ASSERT_TRUE(SkIRect::Intersects(packer_area, second_rect));
  ASSERT_FALSE(SkIRect::Intersects(first_rect, second_rect));

  // We added another 90 x 140 = 12_600 units, now taking us to 13_000
  // 13_000 / 20_000 == 0.65 == 65%
  ASSERT_TRUE(flutter::testing::NumberNear(packer->PercentFull(), 0.65));

  // There's enough area to add this rectangle, but no space big enough for
  // the 50 units of width.
  IPoint16 output;
  ASSERT_FALSE(packer->AddRect(50, 50, &output));
  // Should be unchanged.
  ASSERT_TRUE(flutter::testing::NumberNear(packer->PercentFull(), 0.65));

  packer->Reset();
  // Should be empty now.
  ASSERT_EQ(packer->PercentFull(), 0);
}

TEST(TypographerTest, RectanglePackerFillsRows) {
  auto skyline = RectanglePacker::Factory(257, 256);

  // Fill up the first row.
  IPoint16 loc;
  for (auto i = 0u; i < 16; i++) {
    skyline->AddRect(16, 16, &loc);
  }
  // Last rectangle still in first row.
  EXPECT_EQ(loc.x(), 256 - 16);
  EXPECT_EQ(loc.y(), 0);

  // Fill up second row.
  for (auto i = 0u; i < 16; i++) {
    skyline->AddRect(16, 16, &loc);
  }

  EXPECT_EQ(loc.x(), 256 - 16);
  EXPECT_EQ(loc.y(), 16);
}

TEST_P(TypographerTest, GlyphAtlasTextureWillGrowTilMaxTextureSize) {
  if (GetBackend() == PlaygroundBackend::kOpenGLES ||
      GetBackend() == PlaygroundBackend::kOpenGLESSDF) {
    GTEST_SKIP() << "Atlas growth isn't supported for OpenGLES currently.";
  }

  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);
  ASSERT_TRUE(context && context->IsValid());
  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString("A", sk_font);
  ASSERT_TRUE(blob);
  auto atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob));

  SkFont sk_font_small = flutter::testing::CreateTestFontOfSize(10);

  ISize prev_atlas_size;
  for (int i = 0; i < 100; i++) {
    SkTextBlobBuilder builder;

    auto add_char = [&](const SkFont& sk_font, char c) {
      int count = sk_font.countText(&c, 1, SkTextEncoding::kUTF8);
      auto buffer = builder.allocRunPos(sk_font, count);
      sk_font.textToGlyphs(&c, 1, SkTextEncoding::kUTF8,
                           {buffer.glyphs, count});
      sk_font.getPos({buffer.glyphs, count}, {buffer.points(), count},
                     {0, 0} /*=origin*/);
    };

    SkFont sk_font = flutter::testing::CreateTestFontOfSize(50 + i);
    add_char(sk_font, 'A');
    add_char(sk_font_small, 'B');
    auto blob = builder.make();

    Matrix transform = Matrix::MakeScale({50.0f + i, 50.0f + i, 1.0f});
    atlas =
        CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                         GlyphAtlas::Type::kAlphaBitmap, transform,
                         atlas_context, MakeTextFrameFromTextBlobSkia(blob));
    ASSERT_TRUE(!!atlas);
    ISize atlas_size = atlas->GetTexture()->GetTextureDescriptor().size;
    if (atlas_size.width < prev_atlas_size.width ||
        atlas_size.height < prev_atlas_size.height) {
      // We've triggered an atlas flush and recreate. We're done.

      // The final atlas should contain both the "A" glyph (which was not
      // present in the previous atlas) and the "B" glyph (which existed
      // in the previous atlas).
      ASSERT_EQ(atlas->GetGlyphCount(), 2u);
      return;
    }
    prev_atlas_size = atlas_size;
  }

  // We never triggered the atlas recreate after 100 text calls.
  // Either something is wrong with the atlas code or we need to fix this test.
  GTEST_FAIL() << "Test did not encounter an atlas growth situation";
}

TEST_P(TypographerTest, InvalidAtlasForcesRepopulation) {
  SkFont font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString(
      "the quick brown fox jumped over the lazy dog.", font);
  ASSERT_TRUE(blob);
  auto frame = MakeTextFrameFromTextBlobSkia(blob);

  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());

  auto atlas = CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                                GlyphAtlas::Type::kAlphaBitmap, Matrix(),
                                atlas_context, frame);

  auto second_context = TypographerContextSkia::Make();
  auto second_atlas_context =
      second_context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);

  EXPECT_FALSE(second_atlas_context->GetGlyphAtlas()->IsValid());

  atlas = CreateGlyphAtlas(*GetContext(), second_context.get(),
                           *data_host_buffer, GlyphAtlas::Type::kAlphaBitmap,
                           Matrix(), second_atlas_context, frame);

  EXPECT_TRUE(second_atlas_context->GetGlyphAtlas()->IsValid());
}

TEST_P(TypographerTest, ColorBitmapAtlasSkiaBitmapIsRGBA8888) {
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kColorBitmap);
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  ASSERT_TRUE(context && context->IsValid());
  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString("A", sk_font);
  ASSERT_TRUE(blob);
  auto atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kColorBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob));

  SkImageInfo image_info = TypographerContextSkia::GetImageInfo(
      *atlas, Size{100, 100}, /*support_light_glyphs=*/false);
  EXPECT_EQ(image_info.colorType(), kRGBA_8888_SkColorType);
}

TEST_P(TypographerTest, AlphaBitmapAtlasSkiaBitmapIsA8) {
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  ASSERT_TRUE(context && context->IsValid());
  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString("A", sk_font);
  ASSERT_TRUE(blob);
  auto atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob));

  SkImageInfo image_info = TypographerContextSkia::GetImageInfo(
      *atlas, Size{100, 100}, /*support_light_glyphs=*/false);
  EXPECT_EQ(image_info.colorType(), kAlpha_8_SkColorType);
}

TEST_P(TypographerTest, AlphaBitmapAtlasWithLightGlyphsSkiaBitmapIsRGBA8888) {
  auto context = TypographerContextSkia::Make();
  auto atlas_context =
      context->CreateGlyphAtlasContext(GlyphAtlas::Type::kAlphaBitmap);
  auto data_host_buffer = HostBuffer::Create(
      GetContext()->GetResourceAllocator(), GetContext()->GetIdleWaiter(),
      GetContext()->GetCapabilities()->GetMinimumUniformAlignment());
  ASSERT_TRUE(context && context->IsValid());
  SkFont sk_font = flutter::testing::CreateTestFontOfSize(12);
  auto blob = SkTextBlob::MakeFromString("A", sk_font);
  ASSERT_TRUE(blob);
  auto atlas =
      CreateGlyphAtlas(*GetContext(), context.get(), *data_host_buffer,
                       GlyphAtlas::Type::kAlphaBitmap, Matrix(), atlas_context,
                       MakeTextFrameFromTextBlobSkia(blob));

  SkImageInfo image_info = TypographerContextSkia::GetImageInfo(
      *atlas, Size{100, 100}, /*support_light_glyphs=*/true);
  EXPECT_EQ(image_info.colorType(), kRGBA_8888_SkColorType);
}

namespace {

class HostMemoryAllocator : public Allocator {
 public:
  ISize GetMaxTextureSizeSupported() const override { return {4096, 4096}; }

  std::shared_ptr<DeviceBuffer> OnCreateBuffer(
      const DeviceBufferDescriptor& desc) override {
    storage_.push_back(std::make_unique<uint8_t[]>(desc.size));
    uint8_t* contents = storage_.back().get();
    auto buffer = std::make_shared<NiceMock<MockDeviceBuffer>>(desc);
    ON_CALL(*buffer, OnGetContents()).WillByDefault(Return(contents));
    ON_CALL(*buffer, OnCopyHostBuffer)
        .WillByDefault([contents](const uint8_t* source, Range source_range,
                                  size_t offset) {
          memcpy(contents + offset, source + source_range.offset,
                 source_range.length);
          return true;
        });
    return buffer;
  }

  std::shared_ptr<Texture> OnCreateTexture(const TextureDescriptor& desc,
                                           bool threadsafe) override {
    auto texture = std::make_shared<NiceMock<MockTexture>>(desc);
    ON_CALL(*texture, IsValid()).WillByDefault(Return(true));
    ON_CALL(*texture, GetSize()).WillByDefault(Return(desc.size));
    return texture;
  }

 private:
  std::vector<std::unique_ptr<uint8_t[]>> storage_;
};

}  // namespace

// GLES rebuilds a full glyph atlas instead of growing it. Text that alternates
// between two glyph sets, which fit in the first atlas one at a time but not
// together, must not rebuild the atlas every frame.
TEST(TypographerGLESTest, RebuiltGlyphAtlasHasRoomForAlternatingGlyphs) {
  auto allocator = std::make_shared<HostMemoryAllocator>();
  auto capabilities = std::make_shared<NiceMock<MockCapabilities>>();
  ON_CALL(*capabilities, GetDefaultGlyphAtlasFormat())
      .WillByDefault(Return(PixelFormat::kA8UNormInt));
  std::shared_ptr<const Capabilities> const_capabilities = capabilities;
  auto queue = std::make_shared<NiceMock<MockCommandQueue>>();
  auto context = std::make_shared<NiceMock<MockImpellerContext>>();
  ON_CALL(*context, GetBackendType())
      .WillByDefault(Return(Context::BackendType::kOpenGLES));
  ON_CALL(*context, GetResourceAllocator()).WillByDefault(Return(allocator));
  ON_CALL(*context, GetCapabilities())
      .WillByDefault(ReturnRef(const_capabilities));
  ON_CALL(*context, GetCommandQueue()).WillByDefault(Return(queue));
  ON_CALL(*context, CreateCommandBuffer()).WillByDefault([&context]() {
    auto command_buffer =
        std::make_shared<NiceMock<MockCommandBuffer>>(context);
    ON_CALL(*command_buffer, IsValid()).WillByDefault(Return(true));
    ON_CALL(*command_buffer, OnCreateBlitPass()).WillByDefault([]() {
      auto blit_pass = std::make_shared<NiceMock<MockBlitPass>>();
      ON_CALL(*blit_pass, IsValid()).WillByDefault(Return(true));
      ON_CALL(*blit_pass, EncodeCommands()).WillByDefault(Return(true));
      ON_CALL(*blit_pass, OnCopyBufferToTextureCommand)
          .WillByDefault(Return(true));
      return blit_pass;
    });
    return command_buffer;
  });

  auto typographer_context = TypographerContextSkia::Make();
  auto atlas_context = typographer_context->CreateGlyphAtlasContext(
      GlyphAtlas::Type::kAlphaBitmap);
  auto host_buffer = HostBuffer::Create(allocator, /*idle_waiter=*/nullptr,
                                        /*minimum_uniform_alignment=*/256);
  auto make_frame = [](Scalar font_size) {
    return MakeTextFrameFromTextBlobSkia(SkTextBlob::MakeFromString(
        "ABCDEFGHIJKLMNOPQRST",
        flutter::testing::CreateTestFontOfSize(font_size)));
  };
  // About 140 px tall glyphs: one set takes most of a 4096 px row.
  auto frame_a = make_frame(200);
  auto frame_b = make_frame(210);
  auto create_atlas = [&](const std::shared_ptr<TextFrame>& frame) {
    return CreateGlyphAtlas(*context, typographer_context.get(), *host_buffer,
                            GlyphAtlas::Type::kAlphaBitmap, Matrix(),
                            atlas_context, frame);
  };

  auto atlas = create_atlas(frame_a);
  ASSERT_TRUE(atlas && atlas->GetTexture());
  EXPECT_EQ(atlas->GetTexture()->GetSize().height, 256);
  const std::shared_ptr<Texture> first_texture = atlas->GetTexture();

  atlas = create_atlas(frame_b);
  ASSERT_TRUE(atlas && atlas->GetTexture());
  // The second set did not fit next to the first one.
  ASSERT_NE(atlas->GetTexture(), first_texture);
  EXPECT_EQ(atlas->GetTexture()->GetSize().height, 512);
  const std::shared_ptr<Texture> rebuilt_texture = atlas->GetTexture();

  for (int i = 0; i < 3; i++) {
    for (const auto& frame : {frame_a, frame_b}) {
      atlas = create_atlas(frame);
      ASSERT_TRUE(atlas);
      EXPECT_EQ(atlas->GetTexture(), rebuilt_texture);
    }
  }
}

}  // namespace testing
}  // namespace impeller

// NOLINTEND(bugprone-unchecked-optional-access)
