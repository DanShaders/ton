/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/
#pragma once

// Cross-platform branch prediction hints
#if defined(__GNUC__) || defined(__clang__)
#define TD_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define TD_LIKELY(x) __builtin_expect(!!(x), 1)
#elif defined(_MSC_VER)
#define TD_UNLIKELY(x) (x)
#define TD_LIKELY(x) (x)
#else
#define TD_UNLIKELY(x) (x)
#define TD_LIKELY(x) (x)
#endif

// Cross-platform force inline
#if defined(__GNUC__) || defined(__clang__)
#define TD_FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define TD_FORCE_INLINE __forceinline
#else
#define TD_FORCE_INLINE inline
#endif 