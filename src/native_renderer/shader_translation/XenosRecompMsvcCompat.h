#pragma once

// XenosRecomp is authored for Clang/GNU-style byte-swap builtins. Keep the
// upstream submodule unmodified and force-include this shim for local MSVC
// tool builds.
#if defined(_MSC_VER) && !defined(__clang__)
#include <cstdint>
#include <intrin.h>

#ifndef __builtin_bswap16
#define __builtin_bswap16(value) _byteswap_ushort(static_cast<unsigned short>(value))
#endif

#ifndef __builtin_bswap32
#define __builtin_bswap32(value) _byteswap_ulong(static_cast<unsigned long>(value))
#endif

#ifndef __builtin_bswap64
#define __builtin_bswap64(value) _byteswap_uint64(static_cast<unsigned __int64>(value))
#endif
#endif
