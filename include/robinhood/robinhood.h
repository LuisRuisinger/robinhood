#ifndef LRUISINGER_ROBINHOOD_INCLUDE_ROBINHOOD_ROBINHOOD_H_
#define LRUISINGER_ROBINHOOD_INCLUDE_ROBINHOOD_ROBINHOOD_H_

// =================================================================================================
// robinhood.h
//
// Robin Hood hashmap via C macros, with an optional C++ wrapper.
// Metadata is 16 bits per slot (PSL | fingerprint), and lookups use SIMD where available.
//
// Author: Luis Simon Rusinger <luisruisinger.uni@gmail.com>
// SPDX-License-Identifier: MIT
// =================================================================================================

#ifdef _MSC_VER
#    error \
        "MSVC is not supported because this implementation requires GNU/Clang extensions such as __typeof__. Additionally go fuck yourself."
#endif

#ifdef __cplusplus
#    include <cassert>      // assert
#    include <cstddef>      // std::byte
#    include <cstdint>      // types
#    include <stdexcept>    // std::out_of_range
#    include <type_traits>  // std::is_convertible_v, std::enable_if_t
#else
#    include <assert.h>   // _Static_assert
#    include <stdbool.h>  // bool
#    include <stdint.h>   // types
#    include <stdlib.h>   // malloc, calloc, free, aligned_alloc
#    include <string.h>   // memset, memcmp, memcpy
#endif

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
#    if defined(__AVX512F__) || defined(__AVX2__)
#        include <immintrin.h>
#    elif defined(__SSE2__)
#        include <emmintrin.h>
#    endif
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t  i16;
typedef int32_t  i32;
typedef size_t   usize;

// =================================================================================================
// Helpers
// =================================================================================================

#define CONCAT_(a_, b_) a_##b_
#define CONCAT(a_, b_)  CONCAT_(a_, b_)

#define ROBINHOOD_SUM_1(a)                      ((a))
#define ROBINHOOD_SUM_2(a, b)                   ((a) + (b))
#define ROBINHOOD_SUM_3(a, b, c)                ((a) + (b) + (c))
#define ROBINHOOD_SUM_4(a, b, c, d)             ((a) + (b) + (c) + (d))
#define ROBINHOOD_SUM_5(a, b, c, d, e)          ((a) + (b) + (c) + (d) + (e))
#define ROBINHOOD_SUM_6(a, b, c, d, e, f)       ((a) + (b) + (c) + (d) + (e) + (f))
#define ROBINHOOD_SUM_7(a, b, c, d, e, f, g)    ((a) + (b) + (c) + (d) + (e) + (f) + (g))
#define ROBINHOOD_SUM_8(a, b, c, d, e, f, g, h) ((a) + (b) + (c) + (d) + (e) + (f) + (g) + (h))

#define ROBINHOOD_GET_SUM_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME

#define ROBINHOOD_TOTAL_BITS(...)                                                               \
    ROBINHOOD_GET_SUM_MACRO(__VA_ARGS__, ROBINHOOD_SUM_8, ROBINHOOD_SUM_7, ROBINHOOD_SUM_6,     \
                            ROBINHOOD_SUM_5, ROBINHOOD_SUM_4, ROBINHOOD_SUM_3, ROBINHOOD_SUM_2, \
                            ROBINHOOD_SUM_1)(__VA_ARGS__)

#ifdef __cplusplus
template <i32 N>
using robinhood_uint_t =
    std::conditional_t<(N <= 8), u8,
                       std::conditional_t<(N <= 16), u16, std::conditional_t<(N <= 32), u32, u64>>>;

#    define ROBINHOOD_TYPE_FOR_BITS(...) robinhood_uint_t<ROBINHOOD_TOTAL_BITS(__VA_ARGS__)>

#else
#    define ROBINHOOD_TYPE_FOR_BITS(...)                         \
        __typeof__(__builtin_choose_expr(                        \
            ROBINHOOD_TOTAL_BITS(__VA_ARGS__) <= 8, (u8)0,       \
            __builtin_choose_expr(                               \
                ROBINHOOD_TOTAL_BITS(__VA_ARGS__) <= 16, (u16)0, \
                __builtin_choose_expr(ROBINHOOD_TOTAL_BITS(__VA_ARGS__) <= 32, (u32)0, (u64)0))))
#endif

#define ROBINHOOD_TYPE_BITS(type__) (sizeof(type__) * __CHAR_BIT__)

#ifndef ROBINHOOD_CT_ASSERT_EXPR
#    define ROBINHOOD_CT_ASSERT_EXPR(cond__) \
        ((void)sizeof(u8[(cond__) ? 1 : -1]))  // needed since static_assert's are no expressions
#endif

#ifndef ROBINHOOD_STATIC_ASSERT
#    ifdef __cplusplus
#        define ROBINHOOD_STATIC_ASSERT(cond__, msg__) static_assert((cond__), msg__)
#    else
#        define ROBINHOOD_STATIC_ASSERT(cond__, msg__) ROBINHOOD_CT_ASSERT_EXPR(cond__)
#    endif
#endif

#define ROBINHOOD_NEXT_POW2(x__, ret__)                              \
    do {                                                             \
        ROBINHOOD_STATIC_ASSERT(ROBINHOOD_TYPE_BITS(x__) > 32,       \
                                "type must be bigger than 32 bits"); \
        __typeof__(x__) y__ = (x__);                                 \
                                                                     \
        --y__;                                                       \
        y__ |= y__ >> 1;                                             \
        y__ |= y__ >> 2;                                             \
        y__ |= y__ >> 4;                                             \
        y__ |= y__ >> 8;                                             \
        y__ |= y__ >> 16;                                            \
        y__ |= y__ >> 32;                                            \
        ++y__;                                                       \
                                                                     \
        (ret__) = (y__ == 0) ? 1 : y__;                              \
    } while (0)

// =================================================================================================
// Metadata definition
// =================================================================================================

#ifndef ROBINHOOD_HASH_PART_BIT_CNT
#    define ROBINHOOD_HASH_PART_BIT_CNT 11  // psl part of 16bit metadata
#endif

#ifndef ROBINHOOD_PSL_PART_BIT_CNT
#    define ROBINHOOD_PSL_PART_BIT_CNT 5  // psl part of 16bit metadata
#endif

ROBINHOOD_STATIC_ASSERT(ROBINHOOD_HASH_PART_BIT_CNT + ROBINHOOD_PSL_PART_BIT_CNT ==
                            sizeof(ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT,
                                                           ROBINHOOD_HASH_PART_BIT_CNT)) *
                                __CHAR_BIT__,
                        "hash fingerprint + psl must fill full type");

#define ROBINHOOD_HASH_MASK ((1 << ROBINHOOD_HASH_PART_BIT_CNT) - 1)
#define ROBINHOOD_PSL_MASK  ((1 << ROBINHOOD_PSL_PART_BIT_CNT) - 1)

#ifdef ROBINHOOD_EMPTY_SENTINEL
#    error "ROBINHOOD_EMPTY_SENTINEL must not be predefined"
#endif
#define ROBINHOOD_EMPTY_SENTINEL 0U

#ifdef ROBINHOOD_PSL_BASE
#    error "ROBINHOOD_PSL_BASE must not be predefined"
#endif
#define ROBINHOOD_PSL_BASE 0U

// =================================================================================================
// Strides definition
// =================================================================================================

#if defined(__AVX2__) && defined(ROBINHOOD_ASSUME_LONG_PROBE_SEQUENCES)
#    define ROBINHOOD_STRIDE_WIDTH \
        (sizeof(__m256) /          \
         sizeof(ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT, ROBINHOOD_HASH_PART_BIT_CNT)))
#elif defined(__SSE2__)
#    define ROBINHOOD_STRIDE_WIDTH \
        (sizeof(__m128) /          \
         sizeof(ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT, ROBINHOOD_HASH_PART_BIT_CNT)))
#else
#    define ROBINHOOD_STRIDE_WIDTH 1  // scalar
#endif

#define ROBINHOOD_STRIDE_BYTES \
    (ROBINHOOD_STRIDE_WIDTH *  \
     sizeof(ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT, ROBINHOOD_HASH_PART_BIT_CNT)))

// =================================================================================================
// Capacity definition
// =================================================================================================

#ifndef ROBINHOOD_DEFAULT_CAPACITY
#    define ROBINHOOD_DEFAULT_CAPACITY ROBINHOOD_STRIDE_WIDTH
#endif

#ifndef ROBINHOOD_LOAD_THRESHOLD
#    define ROBINHOOD_LOAD_THRESHOLD 95  // threshold for hashmap load pressure
#endif

// =================================================================================================
// Internal functions
// =================================================================================================

typedef void *(*alloc_fn_t)(void *ctx, usize align, usize len);
typedef void (*free_fn_t)(void *ctx, void *ptr, usize align, usize len);

static void *robinhood_alloc(void *ctx, usize align, usize len) {
    (void)ctx;

    return aligned_alloc(align, len);
}

static void robinhood_free(void *ctx, void *ptr, usize align, usize len) {
    (void)ctx;
    (void)align;
    (void)len;

    free(ptr);
}

// =================================================================================================
// Key hashing
// =================================================================================================

static inline u64 robinhood_hash_bytes_impl(const void *ptr, usize len) {
    const u8 *p = (const u8 *)ptr;

    u64 h = 14695981039346656037ULL;  // FNV-1a offset

    for (usize i = 0; i < len; ++i) {
        h ^= (u64)p[i];
        h *= 1099511628211ULL;  // FNV-1a prime
    }

    return h;
}

#ifdef __cplusplus
template <typename T>
static inline u64 robinhood_hash_auto_ptr(const T *key) {
    using U = std::remove_cv_t<T>;

    if constexpr (std::is_integral_v<U> || std::is_enum_v<U>) {
        return static_cast<u64>(*key);
    } else if constexpr (std::is_pointer_v<U>) {
        return static_cast<uintptr_t>(*key);
    } else {
        return robinhood_hash_bytes_impl(key, sizeof(U));
    }
}

#    define ROBINHOOD_HASH_PTR(key_ptr__) robinhood_hash_auto_ptr((key_ptr__))

#else
#    ifndef ROBINHOOD_HASH_PRIMITIVE_VALUE
#        define ROBINHOOD_HASH_PRIMITIVE_VALUE(x__) \
            ((u64)(ROBINHOOD_TYPE_FOR_BITS(sizeof(x__) * CHAR_BIT))(x__))
#    endif

#    ifndef ROBINHOOD_HASH_PTR_VALUE
#        define ROBINHOOD_HASH_PTR_VALUE(p__) ((u64)(uintptr_t)(p__))
#    endif

#    ifndef ROBINHOOD_HASH_BYTES_VALUE
#        define ROBINHOOD_HASH_BYTES_VALUE(x__) robinhood_hash_bytes_impl(&(x__), sizeof(x__))
#    endif

#    define ROBINHOOD_HASH_AUTO_VALUE(x__)                                              \
        ({                                                                              \
            __typeof__(x__) robinhood_hash_x__ = (x__);                                 \
                                                                                        \
            _Generic((robinhood_hash_x__),                                              \
                char: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),               \
                signed char: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),        \
                unsigned char: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),      \
                short: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),              \
                unsigned short: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),     \
                int: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),                \
                unsigned int: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),       \
                long: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),               \
                unsigned long: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),      \
                long long: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__),          \
                unsigned long long: ROBINHOOD_HASH_PRIMITIVE_VALUE(robinhood_hash_x__), \
                default: ROBINHOOD_HASH_BYTES_VALUE(robinhood_hash_x__))                \
        })

#    define ROBINHOOD_HASH_AUTO_PTR(key_ptr__) ROBINHOOD_HASH_AUTO_VALUE(*(key_ptr__))
#endif

// =================================================================================================
// Key equality
// =================================================================================================

#define ROBINHOOD_KEY_EQ_MEMCMP_PTR_UNCHECKED(a_ptr__, b_ptr__) \
    (memcmp((a_ptr__), (b_ptr__), sizeof(*(a_ptr__))) == 0)

#ifdef __cplusplus
#    define ROBINHOOD_KEY_EQ_MEMCMP_PTR(a_ptr__, b_ptr__)                                        \
        (ROBINHOOD_CT_ASSERT_EXPR(                                                               \
             (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*(a_ptr__))>>,    \
                             std::remove_cv_t<std::remove_reference_t<decltype(*(b_ptr__))>>>)), \
         ROBINHOOD_KEY_EQ_MEMCMP_PTR_UNCHECKED((a_ptr__), (b_ptr__)))

#elif defined(__GNUC__) || defined(__clang__)
#    define ROBINHOOD_KEY_EQ_MEMCMP_PTR(a_ptr__, b_ptr__)                                   \
        (ROBINHOOD_CT_ASSERT_EXPR(                                                          \
             __builtin_types_compatible_p(__typeof__(*(a_ptr__)), __typeof__(*(b_ptr__)))), \
         ROBINHOOD_KEY_EQ_MEMCMP_PTR_UNCHECKED((a_ptr__), (b_ptr__)))

#else
#    define ROBINHOOD_KEY_EQ_MEMCMP_PTR(a_ptr__, b_ptr__)                    \
        (ROBINHOOD_CT_ASSERT_EXPR(sizeof(*(a_ptr__)) == sizeof(*(b_ptr__))), \
         ROBINHOOD_KEY_EQ_MEMCMP_PTR_UNCHECKED((a_ptr__), (b_ptr__)))
#endif

// =================================================================================================
// Internals
// =================================================================================================

#define ROBINHOOD_ALIGN_UP(size__, align__) (((size__) + ((align__) - 1)) & ~((align__) - 1))

#define ROBINHOOD_META_ALLOC_SIZE(map__)                                                   \
    ROBINHOOD_ALIGN_UP(((map__).cap + ROBINHOOD_STRIDE_WIDTH) * sizeof(*(map__).metadata), \
                       ROBINHOOD_STRIDE_BYTES)

#define ROBINHOOD_SLOT_ALLOC_SIZE(map__)                                                \
    ROBINHOOD_ALIGN_UP(((map__).cap + ROBINHOOD_STRIDE_WIDTH) * sizeof(*(map__).slots), \
                       ROBINHOOD_STRIDE_BYTES)

#define ROBINHOOD_LOG2_POW2_U64(x__) (63U - (u32)__builtin_clzll((u64)(x__)))

#define ROBINHOOD_FIBONACCI_MIX64(x__) ((u64)(x__) * 11400714819323198485ULL)

#define ROBINHOOD_HASH_KEY(name__, map__, k__, idx__, fp__)                                   \
    do {                                                                                      \
        u64 raw_hash__ = name__##_hash_key(&(k__));                                           \
        u64 mix_hash__ = ROBINHOOD_FIBONACCI_MIX64(raw_hash__);                               \
                                                                                              \
        (idx__) = (usize)(mix_hash__ & (map__).msk);                                          \
                                                                                              \
        (fp__) = (ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_HASH_PART_BIT_CNT))((mix_hash__ >> 48) &  \
                                                                        ROBINHOOD_HASH_MASK); \
        (fp__) = (fp__) + !(fp__);                                                            \
    } while (0)

#define ROBINHOOD_KEY_EQ_FOR(name__, a__, b__) name__##_key_eq(&(a__), &(b__))

// =================================================================================================
// Declare
// =================================================================================================

#define ROBINHOOD_DECLARE_EX(name__, key_t__, val_t__, hash_expr__, eq_expr__)               \
    typedef struct {                                                                         \
    } name__##_void_t;                                                                       \
                                                                                             \
    static inline u64 name__##_hash_key(const key_t__ *key__) { return (u64)(hash_expr__); } \
                                                                                             \
    static inline bool name__##_key_eq(const key_t__ *a__, const key_t__ *b__) {             \
        return (bool)(eq_expr__);                                                            \
    }                                                                                        \
                                                                                             \
    typedef struct name__##_slot {                                                           \
        key_t__ key;                                                                         \
        val_t__ value;                                                                       \
    } name__##_slot_t;                                                                       \
                                                                                             \
    typedef struct name__ {                                                                  \
        ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT, ROBINHOOD_HASH_PART_BIT_CNT) *   \
            metadata;                                                                        \
        name__##_slot_t *slots;                                                              \
        void            *alloc_ctx;                                                          \
        usize            cap;                                                                \
        usize            len;                                                                \
        usize            msk;                                                                \
        usize            max_psl;                                                            \
        u32              idx_shift;                                                          \
        alloc_fn_t       alloc_fn;                                                           \
        free_fn_t        free_fn;                                                            \
    } name__##_t

// default declare
#define ROBINHOOD_DECLARE_3(name__, key_t__, val_t__)                         \
    ROBINHOOD_DECLARE_EX(name__, key_t__, val_t__, ROBINHOOD_HASH_PTR(key__), \
                         ROBINHOOD_KEY_EQ_MEMCMP_PTR(a__, b__))

// declare with custom hash fn
#define ROBINHOOD_DECLARE_4(name__, key_t__, val_t__, hash_expr__) \
    ROBINHOOD_DECLARE_EX(name__, key_t__, val_t__, hash_expr__,    \
                         ROBINHOOD_KEY_EQ_MEMCMP_PTR(a__, b__))

// decalre with custom hash fn and hash equality expr
#define ROBINHOOD_DECLARE_5(name__, key_t__, val_t__, hash_expr__, eq_expr__) \
    ROBINHOOD_DECLARE_EX(name__, key_t__, val_t__, hash_expr__, eq_expr__)

#define ROBINHOOD_DECLARE_BAD_ARITY(...) \
    ROBINHOOD_STATIC_ASSERT(0, "ROBINHOOD_DECLARE supports 3, 4, or 5 arguments")

#define ROBINHOOD_DECLARE_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME

#define ROBINHOOD_DECLARE(...)                                                      \
    ROBINHOOD_DECLARE_SELECT(__VA_ARGS__, ROBINHOOD_DECLARE_5, ROBINHOOD_DECLARE_4, \
                             ROBINHOOD_DECLARE_3, ROBINHOOD_DECLARE_BAD_ARITY,      \
                             ROBINHOOD_DECLARE_BAD_ARITY)(__VA_ARGS__)

#define ROBINHOOD_SET_DECLARE_EX(name__, key_t__, hash_expr__, eq_expr__) \
    ROBINHOOD_DECLARE_EX(name__, key_t__, name__##_void_t, hash_expr__, eq_expr__)

// default set declare
#define ROBINHOOD_SET_DECLARE_2(name__, key_t__)                         \
    ROBINHOOD_SET_DECLARE_EX(name__, key_t__, ROBINHOOD_HASH_PTR(key__), \
                             ROBINHOOD_KEY_EQ_MEMCMP_PTR(a__, b__))

// set declare with custom hash expr
#define ROBINHOOD_SET_DECLARE_3(name__, key_t__, hash_expr__) \
    ROBINHOOD_SET_DECLARE_EX(name__, key_t__, hash_expr__, ROBINHOOD_KEY_EQ_MEMCMP_PTR(a__, b__))

// set declare with custom hash expr and equality expr
#define ROBINHOOD_SET_DECLARE_4(name__, key_t__, hash_expr__, eq_expr__) \
    ROBINHOOD_SET_DECLARE_EX(name__, key_t__, hash_expr__, eq_expr__)

#define ROBINHOOD_SET_DECLARE_BAD_ARITY(...) \
    ROBINHOOD_STATIC_ASSERT(0, "ROBINHOOD_SET_DECLARE supports 2, 3, or 4 arguments")

#define ROBINHOOD_SET_DECLARE_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME

#define ROBINHOOD_SET_DECLARE(...)                                                 \
    ROBINHOOD_SET_DECLARE_SELECT(__VA_ARGS__, ROBINHOOD_SET_DECLARE_BAD_ARITY,     \
                                 ROBINHOOD_SET_DECLARE_4, ROBINHOOD_SET_DECLARE_3, \
                                 ROBINHOOD_SET_DECLARE_2,                          \
                                 ROBINHOOD_SET_DECLARE_BAD_ARITY)(__VA_ARGS__)

// =================================================================================================
// Init
// =================================================================================================

#define ROBINHOOD_INIT_EX(map__, cap__, alloc__, free__, ctx__)                          \
    do {                                                                                 \
        usize actual_cap__ =                                                             \
            (cap__) < ROBINHOOD_DEFAULT_CAPACITY ? ROBINHOOD_DEFAULT_CAPACITY : (cap__); \
        ROBINHOOD_NEXT_POW2(actual_cap__, actual_cap__);                                 \
                                                                                         \
        (map__).alloc_fn = (alloc__) ? (alloc__) : robinhood_alloc;                      \
        (map__).free_fn = (free__) ? (free__) : robinhood_free;                          \
        (map__).alloc_ctx = (ctx__);                                                     \
                                                                                         \
        (map__).cap = actual_cap__;                                                      \
        (map__).msk = actual_cap__ - 1;                                                  \
        (map__).idx_shift = 64U - ROBINHOOD_LOG2_POW2_U64((map__).cap);                  \
        (map__).len = 0;                                                                 \
        (map__).max_psl = 0;                                                             \
                                                                                         \
        usize meta_size__ = ROBINHOOD_META_ALLOC_SIZE(map__);                            \
        usize slot_size__ = ROBINHOOD_SLOT_ALLOC_SIZE(map__);                            \
                                                                                         \
        (map__).metadata = (__typeof__((map__).metadata))(map__).alloc_fn(               \
            (map__).alloc_ctx, ROBINHOOD_STRIDE_BYTES, meta_size__);                     \
        (map__).slots = (__typeof__((map__).slots))(map__).alloc_fn(                     \
            (map__).alloc_ctx, ROBINHOOD_STRIDE_BYTES, slot_size__);                     \
                                                                                         \
        assert((map__).metadata != NULL);                                                \
        assert((map__).slots != NULL);                                                   \
                                                                                         \
        memset((map__).metadata, 0, meta_size__);                                        \
    } while (0)

#define ROBINHOOD_SET_INIT_EX(map__, cap__, alloc__, free__, ctx__) \
    ROBINHOOD_INIT_EX(map__, cap__, alloc__, free__, ctx__)

// default init
#define ROBINHOOD_INIT_1(map__) \
    ROBINHOOD_INIT_EX((map__), ROBINHOOD_DEFAULT_CAPACITY, NULL, NULL, NULL)

// init with capacity
#define ROBINHOOD_INIT_2(map__, cap__) ROBINHOOD_INIT_EX((map__), (cap__), NULL, NULL, NULL)

// init with capacity and explicit memory management
#define ROBINHOOD_INIT_4(map__, cap__, alloc__, free__) \
    ROBINHOOD_INIT_EX((map__), (cap__), (alloc__), (free__), NULL)

// init with capacity and explicit memory management that needs a context, e.g. pmr
#define ROBINHOOD_INIT_5(map__, cap__, alloc__, free__, ctx__) \
    ROBINHOOD_INIT_EX((map__), (cap__), (alloc__), (free__), (ctx__))

#define ROBINHOOD_INIT_BAD_ARITY(...) \
    ROBINHOOD_STATIC_ASSERT(0, "ROBINHOOD_INIT supports 1, 2, 4, or 5 arguments")

#define ROBINHOOD_INIT_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME

#define ROBINHOOD_INIT(...)                                                \
    ROBINHOOD_INIT_SELECT(__VA_ARGS__, ROBINHOOD_INIT_5, ROBINHOOD_INIT_4, \
                          ROBINHOOD_INIT_BAD_ARITY, ROBINHOOD_INIT_2,      \
                          ROBINHOOD_INIT_1)(__VA_ARGS__)

// default set init
#define ROBINHOOD_SET_INIT_1(set__) ROBINHOOD_INIT_1(set__)

// set init with capacity
#define ROBINHOOD_SET_INIT_2(set__, cap__) ROBINHOOD_INIT_2(set__, cap__)

// set init with capacity and explicit memory management
#define ROBINHOOD_SET_INIT_4(set__, cap__, alloc__, free__) \
    ROBINHOOD_INIT_4(set__, cap__, alloc__, free__)

// set init with capacity and explicit memory management + context
#define ROBINHOOD_SET_INIT_5(set__, cap__, alloc__, free__, ctx__) \
    ROBINHOOD_INIT_5(set__, cap__, alloc__, free__, ctx__)

#define ROBINHOOD_SET_INIT_BAD_ARITY(...) \
    ROBINHOOD_STATIC_ASSERT(0, "ROBINHOOD_SET_INIT supports 1, 2, 4, or 5 arguments")

#define ROBINHOOD_SET_INIT_SELECT(_1, _2, _3, _4, _5, NAME, ...) NAME

#define ROBINHOOD_SET_INIT(...)                                                        \
    ROBINHOOD_SET_INIT_SELECT(__VA_ARGS__, ROBINHOOD_SET_INIT_5, ROBINHOOD_SET_INIT_4, \
                              ROBINHOOD_SET_INIT_BAD_ARITY, ROBINHOOD_SET_INIT_2,      \
                              ROBINHOOD_SET_INIT_1)(__VA_ARGS__)

// =================================================================================================
// Destroy
// =================================================================================================

#define ROBINHOOD_DESTROY(map__)                                                         \
    do {                                                                                 \
        usize meta_size__ = ROBINHOOD_META_ALLOC_SIZE(map__);                            \
        usize slot_size__ = ROBINHOOD_SLOT_ALLOC_SIZE(map__);                            \
                                                                                         \
        if ((map__).metadata != NULL)                                                    \
            (map__).free_fn((map__).alloc_ctx, (map__).metadata, ROBINHOOD_STRIDE_BYTES, \
                            meta_size__);                                                \
                                                                                         \
        if ((map__).slots != NULL)                                                       \
            (map__).free_fn((map__).alloc_ctx, (map__).slots, ROBINHOOD_STRIDE_BYTES,    \
                            slot_size__);                                                \
                                                                                         \
        memset(&(map__), 0, sizeof(map__));                                              \
    } while (0)

#define ROBINHOOD_SET_DESTROY(map__) ROBINHOOD_DESTROY(map__)

// =================================================================================================
// Robin Hood Insertion - raw
// =================================================================================================

#define ROBINHOOD_SYNC_MIRROR(map__, idx__)                                    \
    do {                                                                       \
        if ((idx__) < ROBINHOOD_STRIDE_WIDTH) {                                \
            (map__).metadata[(map__).cap + (idx__)] = (map__).metadata[idx__]; \
            (map__).slots[(map__).cap + (idx__)] = (map__).slots[idx__];       \
        }                                                                      \
    } while (0)

#define ROBINHOOD_SET_SYNC_MIRROR(map__, idx__) ROBINHOOD_SYNC_MIRROR(map__, idx__)

#define ROBINHOOD_INSERT_NO_RESIZE(name__, map__, k__, v__, out__)                                \
    do {                                                                                          \
        usize i__;                                                                                \
        ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_HASH_PART_BIT_CNT) hash_part__;                         \
        ROBINHOOD_HASH_KEY(name__, (map__), (k__), i__, hash_part__);                             \
                                                                                                  \
        ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT)                                       \
        cur_psl__ = ROBINHOOD_PSL_BASE;                                                           \
                                                                                                  \
        __typeof__((map__).slots[0]) cur_slot__;                                                  \
        cur_slot__.key = (k__);                                                                   \
        cur_slot__.value = (v__);                                                                 \
                                                                                                  \
        uintptr_t inserted_addr__ = 0;                                                            \
        uintptr_t first_insert__ = UINTPTR_MAX;                                                   \
        *(out__) = NULL;                                                                          \
                                                                                                  \
        for (;; i__ = (i__ + 1) & (map__).msk) {                                                  \
            __typeof__((map__).metadata[0]) entry__ = (map__).metadata[i__];                      \
                                                                                                  \
            if (entry__ == ROBINHOOD_EMPTY_SENTINEL) {                                            \
                (map__).metadata[i__] = (cur_psl__ << ROBINHOOD_HASH_PART_BIT_CNT) | hash_part__; \
                (map__).slots[i__] = cur_slot__;                                                  \
                                                                                                  \
                inserted_addr__ |= ((uintptr_t)&(map__).slots[i__]) & first_insert__;             \
                                                                                                  \
                ROBINHOOD_SYNC_MIRROR(map__, i__);                                                \
                (map__).len++;                                                                    \
                (map__).max_psl = (cur_psl__ > (map__).max_psl) ? cur_psl__ : (map__).max_psl;    \
                                                                                                  \
                break;                                                                            \
            }                                                                                     \
                                                                                                  \
            ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT)                                   \
            existing_psl__ = entry__ >> ROBINHOOD_HASH_PART_BIT_CNT;                              \
                                                                                                  \
            if (first_insert__ && (entry__ & ROBINHOOD_HASH_MASK) == hash_part__ &&               \
                ROBINHOOD_KEY_EQ_FOR(name__, (map__).slots[i__].key, (k__))) {                    \
                (map__).slots[i__].value = (v__);                                                 \
                                                                                                  \
                ROBINHOOD_SYNC_MIRROR(map__, i__);                                                \
                inserted_addr__ = (uintptr_t)&(map__).slots[i__];                                 \
                                                                                                  \
                break;                                                                            \
            }                                                                                     \
                                                                                                  \
            if (cur_psl__ > existing_psl__) {                                                     \
                __typeof__((map__).metadata[0]) tmp_meta__ = (map__).metadata[i__];               \
                __typeof__((map__).slots[0])    tmp_slot__ = (map__).slots[i__];                  \
                                                                                                  \
                (map__).metadata[i__] = (cur_psl__ << ROBINHOOD_HASH_PART_BIT_CNT) | hash_part__; \
                (map__).slots[i__] = cur_slot__;                                                  \
                                                                                                  \
                inserted_addr__ |= ((uintptr_t)&(map__).slots[i__]) & first_insert__;             \
                first_insert__ = 0;                                                               \
                                                                                                  \
                hash_part__ = tmp_meta__ & ROBINHOOD_HASH_MASK;                                   \
                cur_psl__ = existing_psl__;                                                       \
                cur_slot__ = tmp_slot__;                                                          \
                                                                                                  \
                ROBINHOOD_SYNC_MIRROR(map__, i__);                                                \
            }                                                                                     \
                                                                                                  \
            cur_psl__++;                                                                          \
        }                                                                                         \
                                                                                                  \
        *(out__) = (__typeof__(&(map__).slots[0]))inserted_addr__;                                \
    } while (0)

#define ROBINHOOD_SET_INSERT_NO_RESIZE(name__, set__, k__, out__) \
    ROBINHOOD_INSERT_NO_RESIZE(name__, set__, k__, ((name__##_void_t){}), out__)

// =================================================================================================
// Rehash
// =================================================================================================

#define ROBINHOOD_CAP_FOR_LEN(len__, pct__, ret__)                           \
    do {                                                                     \
        usize wanted_len__ = (len__);                                        \
        usize wanted_cap__ = (wanted_len__ * 100 + ((pct__) - 1)) / (pct__); \
                                                                             \
        if (wanted_cap__ < ROBINHOOD_DEFAULT_CAPACITY)                       \
            wanted_cap__ = ROBINHOOD_DEFAULT_CAPACITY;                       \
                                                                             \
        ROBINHOOD_NEXT_POW2(wanted_cap__, wanted_cap__);                     \
        (ret__) = wanted_cap__;                                              \
    } while (0)

#define ROBINHOOD_SET_CAP_FOR_LEN(len__, pct__, ret__) ROBINHOOD_CAP_FOR_LEN(len__, pct__, ret__)

#define ROBINHOOD_REHASH(name__, map__, new_cap__)                                               \
    do {                                                                                         \
        usize actual_cap__ = (new_cap__);                                                        \
                                                                                                 \
        if (actual_cap__ < ROBINHOOD_DEFAULT_CAPACITY)                                           \
            actual_cap__ = ROBINHOOD_DEFAULT_CAPACITY;                                           \
                                                                                                 \
        {                                                                                        \
            usize min_cap__;                                                                     \
            ROBINHOOD_CAP_FOR_LEN((map__).len, ROBINHOOD_LOAD_THRESHOLD, min_cap__);             \
            if (actual_cap__ < min_cap__)                                                        \
                actual_cap__ = min_cap__;                                                        \
        }                                                                                        \
                                                                                                 \
        ROBINHOOD_NEXT_POW2(actual_cap__, actual_cap__);                                         \
                                                                                                 \
        assert(actual_cap__ >= ROBINHOOD_DEFAULT_CAPACITY);                                      \
        assert((actual_cap__ & (actual_cap__ - 1)) == 0);                                        \
                                                                                                 \
        if (actual_cap__ != (map__).cap) {                                                       \
            usize old_cap__ = (map__).cap;                                                       \
            usize old_len__ = (map__).len;                                                       \
            usize old_meta_size__ = ROBINHOOD_META_ALLOC_SIZE(map__);                            \
            usize old_slot_size__ = ROBINHOOD_SLOT_ALLOC_SIZE(map__);                            \
                                                                                                 \
            __typeof__((map__).metadata[0]) *old_meta__ = (map__).metadata;                      \
            __typeof__((map__).slots[0])    *old_slots__ = (map__).slots;                        \
                                                                                                 \
            (map__).cap = actual_cap__;                                                          \
            (map__).msk = actual_cap__ - 1;                                                      \
            (map__).idx_shift = 64U - ROBINHOOD_LOG2_POW2_U64((map__).cap);                      \
            (map__).len = 0;                                                                     \
            (map__).max_psl = 0;                                                                 \
                                                                                                 \
            usize meta_size__ = ROBINHOOD_META_ALLOC_SIZE(map__);                                \
            usize slot_size__ = ROBINHOOD_SLOT_ALLOC_SIZE(map__);                                \
                                                                                                 \
            __typeof__((map__).metadata) new_meta__ =                                            \
                (__typeof__((map__).metadata))(map__).alloc_fn(                                  \
                    (map__).alloc_ctx, ROBINHOOD_STRIDE_BYTES, meta_size__);                     \
                                                                                                 \
            __typeof__((map__).slots) new_slots__ = (__typeof__((map__).slots))(map__).alloc_fn( \
                (map__).alloc_ctx, ROBINHOOD_STRIDE_BYTES, slot_size__);                         \
                                                                                                 \
            assert(new_meta__ != NULL);                                                          \
            assert(new_slots__ != NULL);                                                         \
                                                                                                 \
            (map__).metadata = new_meta__;                                                       \
            (map__).slots = new_slots__;                                                         \
                                                                                                 \
            memset((map__).metadata, ROBINHOOD_EMPTY_SENTINEL, meta_size__);                     \
                                                                                                 \
            for (usize rehash_i__ = 0; rehash_i__ < old_cap__; ++rehash_i__) {                   \
                if (old_meta__[rehash_i__] != ROBINHOOD_EMPTY_SENTINEL) {                        \
                    __typeof__(old_slots__[0].key)   rehash_key__ = old_slots__[rehash_i__].key; \
                    __typeof__(old_slots__[0].value) rehash_value__ =                            \
                        old_slots__[rehash_i__].value;                                           \
                                                                                                 \
                    __typeof__(&(map__).slots[0]) slot_dump__ = NULL;                            \
                                                                                                 \
                    ROBINHOOD_INSERT_NO_RESIZE(name__, (map__), rehash_key__, rehash_value__,    \
                                               &slot_dump__);                                    \
                }                                                                                \
            }                                                                                    \
                                                                                                 \
            assert((map__).len == old_len__);                                                    \
                                                                                                 \
            (map__).free_fn((map__).alloc_ctx, old_meta__, ROBINHOOD_STRIDE_BYTES,               \
                            old_meta_size__);                                                    \
            (map__).free_fn((map__).alloc_ctx, old_slots__, ROBINHOOD_STRIDE_BYTES,              \
                            old_slot_size__);                                                    \
        }                                                                                        \
    } while (0)

#define ROBINHOOD_SET_REHASH(name__, map__, new_cap__) ROBINHOOD_REHASH(name__, map__, new_cap__)

// =================================================================================================
// Grow
// =================================================================================================

#define ROBINHOOD_LOAD(map__) ((map__).len * 100 / (map__).cap)  // load factor

#define ROBINHOOD_SET_LOAD(map__) ROBINHOOD_LOAD(map__)

#define ROBINHOOD_GROW(name__, map__)                        \
    do {                                                     \
        ROBINHOOD_REHASH(name__, (map__), (map__).cap << 1); \
    } while (0)

#define ROBINHOOD_SET_GROW(map__) ROBINHOOD_GROW(map__, (map__).len)

#define ROBINHOOD_PSL_MAX_ALLOWED \
    ((1 << ROBINHOOD_PSL_PART_BIT_CNT) - 1)  // threshold until corruption

#define ROBINHOOD_SET_MAX_PSL_ALLOWED ROBINHOOD_PSL_MAX_ALLOWED

#define ROBINHOOD_MAYBE_GROW(name__, map__, pct__)                                            \
    do {                                                                                      \
        if (ROBINHOOD_LOAD(map__) >= (pct__) || (map__).max_psl >= ROBINHOOD_PSL_MAX_ALLOWED) \
            ROBINHOOD_GROW(name__, map__);                                                    \
    } while (0)

#define ROBINHOOD_SET_MAYBE_GROW(name__, map__, pct__) ROBINHOOD_MAYBE_GROW(name__, map__, pct__)

// =================================================================================================
// Robin Hood Insertion
// =================================================================================================

#define ROBINHOOD_INSERT(name__, map__, k__, v__, out__)               \
    do {                                                               \
        ROBINHOOD_MAYBE_GROW(name__, map__, ROBINHOOD_LOAD_THRESHOLD); \
        ROBINHOOD_INSERT_NO_RESIZE(name__, map__, k__, v__, out__);    \
    } while (0)

#define ROBINHOOD_SET_INSERT(name__, set__, k__, out__) \
    ROBINHOOD_INSERT(name__, set__, k__, ((name__##_void_t){}), out__)

// =================================================================================================
// Lookup
// =================================================================================================

#define ROBINHOOD_FIND_SCALAR(name__, map__, k__, out__, ret__)                                   \
    do {                                                                                          \
        usize i__;                                                                                \
        ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_HASH_PART_BIT_CNT) hash_part__;                         \
        ROBINHOOD_HASH_KEY(name__, (map__), (k__), i__, hash_part__);                             \
                                                                                                  \
        ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT)                                       \
        cur_psl__ = ROBINHOOD_PSL_BASE;                                                           \
        *(ret__) = false;                                                                         \
        *(out__) = NULL;                                                                          \
                                                                                                  \
        for (;; i__ = (i__ + 1) & (map__).msk) {                                                  \
            u32 entry__ = (map__).metadata[i__];                                                  \
            ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT)                                   \
            existing_psl__ = entry__ >> ROBINHOOD_HASH_PART_BIT_CNT;                              \
                                                                                                  \
            if (entry__ == ROBINHOOD_EMPTY_SENTINEL || cur_psl__ > existing_psl__)                \
                break;                                                                            \
                                                                                                  \
            if ((entry__ & ROBINHOOD_HASH_MASK) == hash_part__) {                                 \
                if (__builtin_expect(ROBINHOOD_KEY_EQ_FOR(name__, (map__).slots[i__].key, (k__)), \
                                     1)) {                                                        \
                    *(out__) = &(map__).slots[i__];                                               \
                    *(ret__) = true;                                                              \
                    break;                                                                        \
                }                                                                                 \
            }                                                                                     \
                                                                                                  \
            cur_psl__++;                                                                          \
        }                                                                                         \
    } while (0)

#define ROBINHOOD_SET_FIND_SCALAR(name__, map__, k__, out__, ret__) \
    ROBINHOOD_FIND_SCALAR(name__, map__, k__, out__, ret__)

#define ROBINHOOD_FIND_SIMD_128BIT(name__, map__, k__, out__, ret__)                             \
    do {                                                                                         \
        usize i__;                                                                               \
        ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_HASH_PART_BIT_CNT) fp__;                               \
        ROBINHOOD_HASH_KEY(name__, (map__), (k__), i__, fp__);                                   \
                                                                                                 \
        const __m128i lane_offsets__ =                                                           \
            _mm_set_epi16(7 << ROBINHOOD_HASH_PART_BIT_CNT, 6 << ROBINHOOD_HASH_PART_BIT_CNT,    \
                          5 << ROBINHOOD_HASH_PART_BIT_CNT, 4 << ROBINHOOD_HASH_PART_BIT_CNT,    \
                          3 << ROBINHOOD_HASH_PART_BIT_CNT, 2 << ROBINHOOD_HASH_PART_BIT_CNT,    \
                          1 << ROBINHOOD_HASH_PART_BIT_CNT, 0 << ROBINHOOD_HASH_PART_BIT_CNT);   \
                                                                                                 \
        const __m128i unsigned_flat__ = _mm_set1_epi16((u16)0x8000);                             \
        const __m128i empty_vec__ = _mm_setzero_si128();                                         \
        const __m128i fp_mask_vec__ = _mm_set1_epi16((u16)ROBINHOOD_HASH_MASK);                  \
        const __m128i fp_vec__ = _mm_set1_epi16(fp__);                                           \
                                                                                                 \
        *(ret__) = false;                                                                        \
        *(out__) = NULL;                                                                         \
                                                                                                 \
        u32 cur_psl__ = ROBINHOOD_PSL_BASE;                                                      \
        for (;; i__ = (i__ + 8) & (map__).msk, cur_psl__ += 8) {                                 \
            const __m128i chunk__ = _mm_loadu_si128((const __m128i *)&(map__).metadata[i__]);    \
                                                                                                 \
            const __m128i chunk_fp__ = _mm_and_si128(chunk__, fp_mask_vec__);                    \
            const __m128i match_mask_vec__ = _mm_cmpeq_epi16(chunk_fp__, fp_vec__);              \
                                                                                                 \
            const __m128i base_stop__ =                                                          \
                _mm_set1_epi16((u16)(cur_psl__ << ROBINHOOD_HASH_PART_BIT_CNT));                 \
            const __m128i stop_vec__ = _mm_add_epi16(base_stop__, lane_offsets__);               \
                                                                                                 \
            const __m128i chunk_u__ = _mm_xor_si128(chunk__, unsigned_flat__);                   \
            const __m128i stop_u__ = _mm_xor_si128(stop_vec__, unsigned_flat__);                 \
            const __m128i psl_stop_mask_vec__ = _mm_cmpgt_epi16(stop_u__, chunk_u__);            \
            const __m128i empty_mask_vec__ = _mm_cmpeq_epi16(chunk__, empty_vec__);              \
            const __m128i stop_mask_vec__ = _mm_or_si128(psl_stop_mask_vec__, empty_mask_vec__); \
                                                                                                 \
            u32 m_mask__ = (u32)_mm_movemask_epi8(match_mask_vec__);                             \
            u32 s_mask__ = (u32)_mm_movemask_epi8(stop_mask_vec__);                              \
                                                                                                 \
            while (m_mask__ != 0) {                                                              \
                i32   lane__ = __builtin_ctz((u32)m_mask__) >> 1;                                \
                usize idx__ = (i__ + (usize)lane__) & (map__).msk;                               \
                                                                                                 \
                if (__builtin_expect(                                                            \
                        ROBINHOOD_KEY_EQ_FOR(name__, (map__).slots[idx__].key, (k__)), 1)) {     \
                    *(out__) = &(map__).slots[idx__];                                            \
                    *(ret__) = true;                                                             \
                                                                                                 \
                    goto CONCAT(found_label__, __LINE__);                                        \
                }                                                                                \
                                                                                                 \
                m_mask__ &= m_mask__ - 1;                                                        \
                m_mask__ &= m_mask__ - 1;                                                        \
            }                                                                                    \
                                                                                                 \
            if (__builtin_expect(s_mask__ != 0, 0))                                              \
                break;                                                                           \
        }                                                                                        \
                                                                                                 \
        CONCAT(found_label__, __LINE__) :;                                                       \
    } while (0)

#define ROBINHOOD_SET_FIND_SIMD_128BIT(name__, map__, k__, out__, ret__) \
    ROBINHOOD_FIND_SIMD_128BIT(name__, map__, k__, out__, ret__)

#define ROBINHOOD_FIND_SIMD_256BIT(name__, map__, k__, out__, ret__)                               \
    do {                                                                                           \
        usize i__;                                                                                 \
        ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_HASH_PART_BIT_CNT) fp__;                                 \
        ROBINHOOD_HASH_KEY(name__, (map__), (k__), i__, fp__);                                     \
                                                                                                   \
        const __m256i lane_offsets__ =                                                             \
            _mm256_set_epi16(15 << ROBINHOOD_HASH_PART_BIT_CNT, 14 << ROBINHOOD_HASH_PART_BIT_CNT, \
                             13 << ROBINHOOD_HASH_PART_BIT_CNT, 12 << ROBINHOOD_HASH_PART_BIT_CNT, \
                             11 << ROBINHOOD_HASH_PART_BIT_CNT, 10 << ROBINHOOD_HASH_PART_BIT_CNT, \
                             9 << ROBINHOOD_HASH_PART_BIT_CNT, 8 << ROBINHOOD_HASH_PART_BIT_CNT,   \
                             7 << ROBINHOOD_HASH_PART_BIT_CNT, 6 << ROBINHOOD_HASH_PART_BIT_CNT,   \
                             5 << ROBINHOOD_HASH_PART_BIT_CNT, 4 << ROBINHOOD_HASH_PART_BIT_CNT,   \
                             3 << ROBINHOOD_HASH_PART_BIT_CNT, 2 << ROBINHOOD_HASH_PART_BIT_CNT,   \
                             1 << ROBINHOOD_HASH_PART_BIT_CNT, 0 << ROBINHOOD_HASH_PART_BIT_CNT);  \
                                                                                                   \
        const __m256i unsigned_flat__ = _mm256_set1_epi16((u16)0x8000);                            \
        const __m256i empty_vec__ = _mm256_setzero_si256();                                        \
        const __m256i fp_mask_vec__ = _mm256_set1_epi16((u16)ROBINHOOD_HASH_MASK);                 \
        const __m256i fp_vec__ = _mm256_set1_epi16(fp__);                                          \
                                                                                                   \
        *(ret__) = false;                                                                          \
        *(out__) = NULL;                                                                           \
                                                                                                   \
        u32 cur_psl__ = ROBINHOOD_PSL_BASE;                                                        \
        for (;; i__ = (i__ + 16) & (map__).msk, cur_psl__ += 16) {                                 \
            const __m256i chunk__ = _mm256_loadu_si256((const __m256i *)&(map__).metadata[i__]);   \
                                                                                                   \
            const __m256i chunk_fp__ = _mm256_and_si256(chunk__, fp_mask_vec__);                   \
            const __m256i match_mask_vec__ = _mm256_cmpeq_epi16(chunk_fp__, fp_vec__);             \
                                                                                                   \
            const __m256i base_stop__ =                                                            \
                _mm256_set1_epi16((u16)(cur_psl__ << ROBINHOOD_HASH_PART_BIT_CNT));                \
            const __m256i stop_vec__ = _mm256_add_epi16(base_stop__, lane_offsets__);              \
                                                                                                   \
            const __m256i chunk_u__ = _mm256_xor_si256(chunk__, unsigned_flat__);                  \
            const __m256i stop_u__ = _mm256_xor_si256(stop_vec__, unsigned_flat__);                \
            const __m256i psl_stop_mask_vec__ = _mm256_cmpgt_epi16(stop_u__, chunk_u__);           \
            const __m256i empty_mask_vec__ = _mm256_cmpeq_epi16(chunk__, empty_vec__);             \
            const __m256i stop_mask_vec__ =                                                        \
                _mm256_or_si256(psl_stop_mask_vec__, empty_mask_vec__);                            \
                                                                                                   \
            u32 m_mask__ = (u32)_mm256_movemask_epi8(match_mask_vec__);                            \
            u32 s_mask__ = (u32)_mm256_movemask_epi8(stop_mask_vec__);                             \
                                                                                                   \
            while (m_mask__ != 0) {                                                                \
                i32   lane__ = __builtin_ctz((u32)m_mask__) >> 1;                                  \
                usize idx__ = (i__ + (usize)lane__) & (map__).msk;                                 \
                                                                                                   \
                if (__builtin_expect(                                                              \
                        ROBINHOOD_KEY_EQ_FOR(name__, (map__).slots[idx__].key, (k__)), 1)) {       \
                    *(out__) = &(map__).slots[idx__];                                              \
                    *(ret__) = true;                                                               \
                                                                                                   \
                    goto CONCAT(found_label__, __LINE__);                                          \
                }                                                                                  \
                                                                                                   \
                m_mask__ &= m_mask__ - 1;                                                          \
                m_mask__ &= m_mask__ - 1;                                                          \
            }                                                                                      \
                                                                                                   \
            if (__builtin_expect(s_mask__ != 0, 0))                                                \
                break;                                                                             \
        }                                                                                          \
                                                                                                   \
        CONCAT(found_label__, __LINE__) :;                                                         \
    } while (0)

#define ROBINHOOD_SET_FIND_SIMD_256BIT(name__, map__, k__, out__, ret__) \
    ROBINHOOD_FIND_SIMD_256BIT(name__, map__, k__, out__, ret__)

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
#    if defined(__AVX2__) && defined(ROBINHOOD_ASSUME_LONG_PROBE_SEQUENCES)
#        define ROBINHOOD_FIND(name__, map__, k__, out__, ret__) \
            ROBINHOOD_FIND_SIMD_256BIT(name__, map__, k__, out__, ret__)

#        define ROBINHOOD_SET_FIND(name__, set__, k__, out__, ret__) \
            ROBINHOOD_FIND_SIMD_256BIT(name__, set__, k__, out__, ret__)

#    elif defined(__SSE2__)
#        define ROBINHOOD_FIND(name__, map__, k__, out__, ret__) \
            ROBINHOOD_FIND_SIMD_128BIT(name__, map__, k__, out__, ret__)

#        define ROBINHOOD_SET_FIND(name__, set__, k__, out__, ret__) \
            ROBINHOOD_FIND_SIMD_128BIT(name__, set__, k__, out__, ret__)

#    else
#        define ROBINHOOD_FIND(name__, map__, k__, out__, ret__) \
            ROBINHOOD_FIND_SCALAR(name__, map__, k__, out__, ret__)

#        define ROBINHOOD_SET_FIND(name__, set__, k__, out__, ret__) \
            ROBINHOOD_FIND_SCALAR(name__, set__, k__, out__, ret__)

#    endif

#else
#    define ROBINHOOD_FIND(name__, map__, k__, out__, ret__) \
        ROBINHOOD_FIND_SCALAR(name__, map__, k__, out__, ret__)

#    define ROBINHOOD_SET_FIND(name__, set__, k__, out__, ret__) \
        ROBINHOOD_FIND_SCALAR(name__, set__, k__, out__, ret__)

#endif

// =================================================================================================
// Delete
// =================================================================================================

#define ROBINHOOD_DELETE(name__, map__, k__, ret__)                                           \
    do {                                                                                      \
        usize i__;                                                                            \
        ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_HASH_PART_BIT_CNT) hash_part__;                     \
        ROBINHOOD_HASH_KEY(name__, (map__), (k__), i__, hash_part__);                         \
                                                                                              \
        ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT)                                   \
        cur_psl__ = ROBINHOOD_PSL_BASE;                                                       \
        *(ret__) = false;                                                                     \
                                                                                              \
        for (;; i__ = (i__ + 1) & (map__).msk) {                                              \
            __typeof__((map__).metadata[0]) entry__ = (map__).metadata[i__];                  \
                                                                                              \
            ROBINHOOD_TYPE_FOR_BITS(ROBINHOOD_PSL_PART_BIT_CNT)                               \
            existing_psl__ = entry__ >> ROBINHOOD_HASH_PART_BIT_CNT;                          \
                                                                                              \
            if (entry__ == ROBINHOOD_EMPTY_SENTINEL || cur_psl__ > existing_psl__)            \
                break;                                                                        \
                                                                                              \
            if ((entry__ & ROBINHOOD_HASH_MASK) == hash_part__ &&                             \
                __builtin_expect(ROBINHOOD_KEY_EQ_FOR(name__, (map__).slots[i__].key, (k__)), \
                                 1)) {                                                        \
                *(ret__) = true;                                                              \
                break;                                                                        \
            }                                                                                 \
                                                                                              \
            ++cur_psl__;                                                                      \
        }                                                                                     \
                                                                                              \
        if (*(ret__)) {                                                                       \
            usize j__ = i__;                                                                  \
                                                                                              \
            for (;;) {                                                                        \
                usize                           next__ = (j__ + 1) & (map__).msk;             \
                __typeof__((map__).metadata[0]) next_entry__ = (map__).metadata[next__];      \
                                                                                              \
                if (next_entry__ == ROBINHOOD_EMPTY_SENTINEL ||                               \
                    (next_entry__ >> ROBINHOOD_HASH_PART_BIT_CNT) == ROBINHOOD_PSL_BASE) {    \
                    (map__).metadata[j__] = ROBINHOOD_EMPTY_SENTINEL;                         \
                    ROBINHOOD_SYNC_MIRROR(map__, j__);                                        \
                    break;                                                                    \
                }                                                                             \
                                                                                              \
                __typeof__((map__).metadata[0]) shifted_psl__ =                               \
                    ((next_entry__ >> ROBINHOOD_HASH_PART_BIT_CNT) - 1)                       \
                    << ROBINHOOD_HASH_PART_BIT_CNT;                                           \
                                                                                              \
                (map__).metadata[j__] = shifted_psl__ | (next_entry__ & ROBINHOOD_HASH_MASK); \
                (map__).slots[j__] = (map__).slots[next__];                                   \
                                                                                              \
                ROBINHOOD_SYNC_MIRROR(map__, j__);                                            \
                                                                                              \
                j__ = next__;                                                                 \
            }                                                                                 \
                                                                                              \
            (map__).len--;                                                                    \
        }                                                                                     \
    } while (0)

#define ROBINHOOD_SET_DELETE(name__, map__, k__, ret__) ROBINHOOD_DELETE(name__, map__, k__, ret__)

// =================================================================================================
// Misc
// =================================================================================================

#define ROBINHOOD_CONTAINS(name__, map__, k__, ret__)      \
    do {                                                   \
        __typeof__(&(map__).slots[0]) out__ = NULL;        \
        ROBINHOOD_FIND(name__, map__, k__, &out__, ret__); \
    } while (0)

#define ROBINHOOD_SET_CONTAINS(name__, map__, k__, ret__) \
    ROBINHOOD_CONTAINS(name__, map__, k__, ret__)

#define ROBINHOOD_RESERVE(name__, map__, expected_len__)                                  \
    do {                                                                                  \
        usize reserve_cap__;                                                              \
        ROBINHOOD_CAP_FOR_LEN((expected_len__), ROBINHOOD_LOAD_THRESHOLD, reserve_cap__); \
                                                                                          \
        if (reserve_cap__ > (map__).cap)                                                  \
            ROBINHOOD_REHASH(name__, (map__), reserve_cap__);                             \
    } while (0)

#define ROBINHOOD_SET_RESERVE(name__, map__, expected_len__) \
    ROBINHOOD_RESERVE(name__, map__, expected_len__)

#define ROBINHOOD_SHRINK_TO_FIT(name__, map__)                                      \
    do {                                                                            \
        usize shrink_cap__;                                                         \
        ROBINHOOD_CAP_FOR_LEN((map__).len, ROBINHOOD_LOAD_THRESHOLD, shrink_cap__); \
                                                                                    \
        if (shrink_cap__ < (map__).cap)                                             \
            ROBINHOOD_REHASH(name__, (map__), shrink_cap__);                        \
    } while (0)

#define ROBINHOOD_SET_SHRINK_TO_FIT(name__, map__) ROBINHOOD_SHRINK_TO_FIT(name__, map__)

#endif  // LRUISINGER_ROBINHOOD_INCLUDE_ROBINHOOD_ROBINHOOD_H_
