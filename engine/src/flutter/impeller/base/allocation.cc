// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/base/allocation.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "flutter/fml/build_config.h"
#include "impeller/base/validation.h"

#if FML_OS_LINUX
#include <sys/mman.h>
#endif  // FML_OS_LINUX

namespace impeller {

#if FML_OS_LINUX
// Reservations at least this large bypass malloc and are mapped directly.
//
// glibc raises its mmap threshold (up to 32 MiB) after a large block is freed.
// From then on, image sized buffers (decoded pixels, staging buffers) are
// carved out of malloc arenas, and freeing them leaves the pages resident.
// Mapping them directly returns the pages to the system as soon as the
// allocation dies.
static constexpr uint64_t kDirectMappingThreshold = 256u * 1024u;
#endif  // FML_OS_LINUX

Allocation::Allocation() = default;

Allocation::~Allocation() {
#if FML_OS_LINUX
  if (mapped_) {
    ::munmap(buffer_, reserved_.GetByteSize());
    return;
  }
#endif  // FML_OS_LINUX
  ::free(buffer_);
}

uint8_t* Allocation::GetBuffer() const {
  return buffer_;
}

Bytes Allocation::GetLength() const {
  return length_;
}

Bytes Allocation::GetReservedLength() const {
  return reserved_;
}

bool Allocation::Truncate(Bytes length, bool npot) {
  const auto reserved = npot ? ReserveNPOT(length) : Reserve(length);
  if (!reserved) {
    return false;
  }
  FML_CHECK(reserved_ >= length);
  length_ = length;
  return true;
}

absl::StatusOr<uint64_t> Allocation::NextPowerOfTwoSize(uint64_t x) {
  if (x > (static_cast<uint64_t>(1) << 63)) {
    return absl::InvalidArgumentError("Out of range");
  }
  if (x == 0) {
    return 1;
  }

  --x;

  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x |= x >> 32;

  return x + 1;
}

bool Allocation::ReserveNPOT(Bytes reserved) {
  // Reserve at least one page of data.
  reserved = std::max(Bytes{4096u}, reserved);
  absl::StatusOr<uint64_t> npot_size =
      NextPowerOfTwoSize(reserved.GetByteSize());
  if (!npot_size.ok()) {
    return false;
  }
  return Reserve(Bytes{*npot_size});
}

bool Allocation::Reserve(Bytes reserved) {
  if (reserved <= reserved_) {
    return true;
  }

  if (reserved.GetByteSize() > std::numeric_limits<size_t>::max()) {
    VALIDATION_LOG << "Allocation failed. Size out of range.";
    return false;
  }

#if FML_OS_LINUX
  if (reserved.GetByteSize() >= kDirectMappingThreshold) {
    void* mapping =
        ::mmap(nullptr, reserved.GetByteSize(), PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
      VALIDATION_LOG << "Allocation failed. Out of host memory.";
      return false;
    }
    if (buffer_) {
      std::memcpy(mapping, buffer_, reserved_.GetByteSize());
      if (mapped_) {
        ::munmap(buffer_, reserved_.GetByteSize());
      } else {
        ::free(buffer_);
      }
    }
    buffer_ = static_cast<uint8_t*>(mapping);
    reserved_ = reserved;
    mapped_ = true;
    return true;
  }
#endif  // FML_OS_LINUX

  auto new_allocation = ::realloc(buffer_, reserved.GetByteSize());
  if (!new_allocation) {
    // If new length is zero, a minimum non-zero sized allocation is returned.
    // So this check will not trip and this routine will indicate success as
    // expected.
    VALIDATION_LOG << "Allocation failed. Out of host memory.";
    return false;
  }

  buffer_ = static_cast<uint8_t*>(new_allocation);
  reserved_ = reserved;

  return true;
}

std::shared_ptr<fml::Mapping> CreateMappingWithCopy(const uint8_t* contents,
                                                    Bytes length) {
  if (contents == nullptr) {
    return nullptr;
  }

  auto allocation = std::make_shared<Allocation>();
  if (!allocation->Truncate(length)) {
    return nullptr;
  }

  std::memmove(allocation->GetBuffer(), contents, length.GetByteSize());

  return CreateMappingFromAllocation(allocation);
}

std::shared_ptr<fml::Mapping> CreateMappingFromAllocation(
    const std::shared_ptr<Allocation>& allocation) {
  if (!allocation) {
    return nullptr;
  }
  return std::make_shared<fml::NonOwnedMapping>(
      reinterpret_cast<const uint8_t*>(allocation->GetBuffer()),  //
      allocation->GetLength().GetByteSize(),                      //
      [allocation](auto, auto) {}                                 //
  );
}

std::shared_ptr<fml::Mapping> CreateMappingWithString(std::string string) {
  auto buffer = std::make_shared<std::string>(std::move(string));
  return std::make_unique<fml::NonOwnedMapping>(
      reinterpret_cast<const uint8_t*>(buffer->c_str()), buffer->length(),
      [buffer](auto, auto) {});
}

}  // namespace impeller
