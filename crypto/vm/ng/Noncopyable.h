/*
 * Copyright (c) 2025, Dan Klishch <danilklishch@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0
 */

#pragma once

#define TON_MAKE_NONCOPYABLE(Type) \
  Type(const Type&) = delete;      \
  Type& operator=(const Type&) = delete

#define TON_MAKE_NONMOVABLE(Type) \
  Type(Type&&) = delete;          \
  Type& operator=(Type&&) = delete

#define TON_MAKE_DEFAULT_COPYABLE(Type) \
  Type(const Type&) = default;          \
  Type& operator=(const Type&) = default

#define TON_MAKE_DEFAULT_MOVABLE(Type) \
  Type(Type&&) = default;              \
  Type& operator=(Type&&) = default
