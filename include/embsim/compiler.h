/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* compiler.h -- the portability macros of SPEC-001 §9: the only place a
 * compiler is named. */
#ifndef EMBSIM_COMPILER_H
#define EMBSIM_COMPILER_H

#if defined(_WIN32) && defined(ES_SHARED)
#if defined(ES_BUILD_SHARED)
#define ES_API __declspec(dllexport)
#else
#define ES_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && defined(ES_BUILD_SHARED)
#define ES_API __attribute__((visibility("default")))
#else
#define ES_API
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__TINYC__)
#define ES_STATIC_ASSERT(cond, name) _Static_assert(cond, #name)
#else
#define ES_STATIC_ASSERT(cond, name) typedef char es_static_assert_##name[(cond) ? 1 : -1]
#endif

#if defined(_MSC_VER)
#define ES_INLINE static __inline
#define ES_NORETURN __declspec(noreturn)
#elif defined(__GNUC__)
#define ES_INLINE static inline
#define ES_NORETURN __attribute__((noreturn))
#else
#define ES_INLINE static
#define ES_NORETURN
#endif

#define ES_UNUSED(x) ((void)(x))
#define ES_COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

#endif
