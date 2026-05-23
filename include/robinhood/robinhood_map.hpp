#ifndef LRUISINGER_ROBINHOOD_INCLUDE_ROBINHOOD_ROBINHOOD_MAP_HPP_
#define LRUISINGER_ROBINHOOD_INCLUDE_ROBINHOOD_ROBINHOOD_MAP_HPP_

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

#    include <cassert>          // assert
#    include <cstddef>          // std::byte
#    include <cstring>          // memset, memcmp
#    include <functional>       // std::hash
#    include <iterator>         // robinhood iterator
#    include <memory>           // std::allocator, std::allocator_traits
#    include <memory_resource>  // PMR
#    include <optional>         // std::optional
#    include <stdexcept>        // std::out_of_range
#    include <type_traits>      // std::is_convertible_v, std::enable_if_t

// =================================================================================================
// Project files
// =================================================================================================

#include "robinhood.h"

namespace lsr::robinhood {

template <typename K, typename V, typename Hash = std::hash<K>,
          typename KeyEqual = std::equal_to<K>>
class RobinHoodMap {
    static_assert(std::is_trivially_copyable_v<K>,
                  "RobinHoodMap currently requires trivially copyable keys");
    static_assert(std::is_trivially_copyable_v<V>,
                  "RobinHoodMap currently requires trivially copyable values");

    // =============================================================================================
    // Declare
    // =============================================================================================

    ROBINHOOD_DECLARE(rh_map, K, V, Hash{}(*key__), KeyEqual{}(*a__, *b__));

    using inner_t = rh_map_t;
    using slot_t = rh_map_slot_t;

    inner_t m_map;

    // =============================================================================================
    // Iterator
    // =============================================================================================

    class iterator {
        friend class const_iterator;

        inner_t *m_map = nullptr;
        usize    m_idx = 0;

        void skip_empty() {
            while (m_map != nullptr && m_idx < m_map->cap &&
                   m_map->metadata[m_idx] == ROBINHOOD_EMPTY_SENTINEL) {
                ++m_idx;
            }
        }

       public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = slot_t;
        using difference_type = std::ptrdiff_t;
        using pointer = slot_t *;
        using reference = slot_t &;

        iterator() = default;

        iterator(inner_t *map, usize idx) : m_map{map}, m_idx{idx} { skip_empty(); }

        reference operator*() const { return m_map->slots[m_idx]; }

        pointer operator->() const { return &m_map->slots[m_idx]; }

        iterator &operator++() {
            ++m_idx;
            skip_empty();
            return *this;
        }

        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const iterator &a, const iterator &b) {
            return a.m_map == b.m_map && a.m_idx == b.m_idx;
        }

        friend bool operator!=(const iterator &a, const iterator &b) { return !(a == b); }
    };

    class const_iterator {
        const inner_t *m_map = nullptr;
        usize          m_idx = 0;

        void skip_empty() {
            while (m_map != nullptr && m_idx < m_map->cap &&
                   m_map->metadata[m_idx] == ROBINHOOD_EMPTY_SENTINEL) {
                ++m_idx;
            }
        }

       public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const slot_t;
        using difference_type = std::ptrdiff_t;
        using pointer = const slot_t *;
        using reference = const slot_t &;

        const_iterator() = default;

        const_iterator(const inner_t *map, usize idx) : m_map{map}, m_idx{idx} { skip_empty(); }

        const_iterator(const iterator &it) : m_map{it.m_map}, m_idx{it.m_idx} {}

        reference operator*() const { return m_map->slots[m_idx]; }

        pointer operator->() const { return &m_map->slots[m_idx]; }

        const_iterator &operator++() {
            ++m_idx;
            skip_empty();
            return *this;
        }

        const_iterator operator++(int) {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        friend bool operator==(const const_iterator &a, const const_iterator &b) {
            return a.m_map == b.m_map && a.m_idx == b.m_idx;
        }

        friend bool operator!=(const const_iterator &a, const const_iterator &b) {
            return !(a == b);
        }
    };

    // =============================================================================================
    // Wrapper functions
    // =============================================================================================

    static void *pmr_alloc(void *ctx, usize align, usize len) {
        auto *resource = static_cast<std::pmr::memory_resource *>(ctx);
        return resource->allocate(len, align);
    }

    static void pmr_free(void *ctx, void *ptr, usize align, usize len) {
        auto *resource = static_cast<std::pmr::memory_resource *>(ctx);
        resource->deallocate(ptr, len, align);
    }

   public:
    using key_type = K;
    using mapped_type = V;
    using slot_type = slot_t;
    using size_type = usize;
    using hasher = Hash;
    using key_equal = KeyEqual;

    // =============================================================================================
    // Constructors
    // =============================================================================================

    explicit RobinHoodMap(usize                      expected_size = ROBINHOOD_DEFAULT_CAPACITY,
                          std::pmr::memory_resource *resource = std::pmr::get_default_resource()) {
        ROBINHOOD_INIT_EX(m_map, expected_size, pmr_alloc, pmr_free, resource);
    }

    explicit RobinHoodMap(std::pmr::memory_resource *resource)
        : RobinHoodMap(ROBINHOOD_DEFAULT_CAPACITY, resource) {}

    RobinHoodMap(RobinHoodMap &&o) noexcept : m_map{o.m_map} {
        std::memset(&o.m_map, 0, sizeof(o.m_map));
    }

    RobinHoodMap(const RobinHoodMap &) = delete;

    // =============================================================================================
    // Destructor
    // =============================================================================================

    ~RobinHoodMap() {
        if (m_map.metadata != nullptr) {
            ROBINHOOD_DESTROY(m_map);
        }
    }

    // =============================================================================================
    // Operators
    // =============================================================================================

    RobinHoodMap &operator=(RobinHoodMap &&o) noexcept {
        if (this != &o) {
            if (m_map.metadata != nullptr) {
                ROBINHOOD_DESTROY(m_map);
            }

            m_map = o.m_map;
            std::memset(&o.m_map, 0, sizeof(o.m_map));
        }

        return *this;
    }

    RobinHoodMap &operator=(const RobinHoodMap &) = delete;

    // =============================================================================================
    // API
    // =============================================================================================

    void insert(const K &k, const V &v) {
        slot_type *slot = nullptr;
        ROBINHOOD_INSERT(rh_map, m_map, k, v, &slot);
    }

    slot_type *insert_slot(const K &k, const V &v) {
        slot_type *slot = nullptr;
        ROBINHOOD_INSERT(rh_map, m_map, k, v, &slot);
        return slot;
    }

    bool erase(const K &k) {
        bool r = false;
        ROBINHOOD_DELETE(rh_map, m_map, k, &r);
        return r;
    }

    bool remove(const K &k) { return erase(k); }

    [[nodiscard]] bool contains(const K &k) const {
        bool r = false;
        ROBINHOOD_CONTAINS(rh_map, m_map, k, &r);
        return r;
    }

    [[nodiscard]] V *find(const K &k) {
        slot_t *slot = nullptr;
        bool    found = false;

        ROBINHOOD_FIND(rh_map, m_map, k, &slot, &found);
        return found ? &slot->value : nullptr;
    }

    [[nodiscard]] const V *find(const K &k) const {
        slot_t *slot = nullptr;
        bool    found = false;

        ROBINHOOD_FIND(rh_map, m_map, k, &slot, &found);
        return found ? &slot->value : nullptr;
    }

    [[nodiscard]] slot_t *find_slot(const K &k) {
        slot_t *slot = nullptr;
        bool    found = false;

        ROBINHOOD_FIND(rh_map, m_map, k, &slot, &found);
        return found ? slot : nullptr;
    }

    [[nodiscard]] const slot_t *find_slot(const K &k) const {
        slot_t *slot = nullptr;
        bool    found = false;

        ROBINHOOD_FIND(rh_map, m_map, k, &slot, &found);
        return found ? slot : nullptr;
    }

    [[nodiscard]] std::optional<V> get(const K &k) const {
        const V *v = find(k);
        return v ? std::optional<V>{*v} : std::nullopt;
    }

    V &at(const K &k) {
        V *v = find(k);
        if (!v) {
            throw std::out_of_range("RobinHoodMap::at: key not found");
        }

        return *v;
    }

    const V &at(const K &k) const {
        const V *v = find(k);
        if (!v) {
            throw std::out_of_range("RobinHoodMap::at: key not found");
        }

        return *v;
    }

    void reserve(usize expected_size) { ROBINHOOD_RESERVE(rh_map, m_map, expected_size); }

    void shrink_to_fit() { ROBINHOOD_SHRINK_TO_FIT(rh_map, m_map); }

    // =============================================================================================
    // Iterator
    // =============================================================================================

    iterator begin() { return iterator(&m_map, 0); }

    iterator end() { return iterator(&m_map, m_map.cap); }

    const_iterator begin() const { return const_iterator(&m_map, 0); }

    const_iterator end() const { return const_iterator(&m_map, m_map.cap); }

    const_iterator cbegin() const { return const_iterator(&m_map, 0); }

    const_iterator cend() const { return const_iterator(&m_map, m_map.cap); }

    // =============================================================================================
    // Misc
    // =============================================================================================

    [[nodiscard]] bool empty() const { return m_map.len == 0; }

    [[nodiscard]] usize size() const { return m_map.len; }

    [[nodiscard]] usize capacity() const { return m_map.cap; }

    [[nodiscard]] usize load() const { return ROBINHOOD_LOAD(m_map); }

    [[nodiscard]] usize max_psl() const { return m_map.max_psl; }

    inner_t &raw() { return m_map; }

    const inner_t &raw() const { return m_map; }
};

}  // namespace lsr::robinhood

#endif  // LRUISINGER_ROBINHOOD_INCLUDE_ROBINHOOD_ROBINHOOD_MAP_HPP_
