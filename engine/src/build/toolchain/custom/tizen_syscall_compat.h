// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUILD_TOOLCHAIN_CUSTOM_TIZEN_SYSCALL_COMPAT_H_
#define BUILD_TOOLCHAIN_CUSTOM_TIZEN_SYSCALL_COMPAT_H_

// Some Tizen cross-compilation sysroots do not expose __NR_getrandom even
// though the target Linux ABI supports the syscall. Dart's crypto_linux.cc
// references the syscall number directly, so provide the missing arch-specific
// values for custom Linux toolchains without modifying third_party sources.
#if defined(__linux__)

#if defined(__i386__) && !defined(__NR_getrandom)
#define __NR_getrandom 355
#endif

#if defined(__x86_64__) && !defined(__NR_getrandom)
#define __NR_getrandom 318
#endif

#if defined(__arm__) && !defined(__NR_getrandom)
#define __NR_getrandom 384
#endif

#if defined(__aarch64__) && !defined(__NR_getrandom)
#define __NR_getrandom 278
#endif

// Some Tizen sysroots ship a pre-3.17 <linux/random.h> UAPI header that
// predates the getrandom(2) flags. BoringSSL's urandom.cc references
// GRND_NONBLOCK directly, so provide the missing flag for custom toolchains.
#if !defined(GRND_NONBLOCK)
#define GRND_NONBLOCK 0x0001
#endif

#endif  // defined(__linux__)

#endif  // BUILD_TOOLCHAIN_CUSTOM_TIZEN_SYSCALL_COMPAT_H_
