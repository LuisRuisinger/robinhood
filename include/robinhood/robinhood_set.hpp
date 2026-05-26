#ifndef LRUISINGER_ROBINHOOD_INCLUDE_ROBINHOOD_ROBINHOOD_SET_HPP_
#define LRUISINGER_ROBINHOOD_INCLUDE_ROBINHOOD_ROBINHOOD_SET_HPP_

#include <cassert>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory_resource>
#include <stdexcept>
#include <type_traits>

#include "robinhood.h"

namespace lsr::robinhood {

template <typename K, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
class RobinHoodSet {
    static_assert(std::is_trivially_copyable_v<K>,
                  "RobinHoodSet currently requires trivially copyable keys");

    // =============================================================================================
    // Declare
    // =============================================================================================

    ROBINHOOD_SET_DECLARE(rh_set, K, Hash{}(*key__), KeyEqual{}(*a__, *b__));

    using inner_t = rh_set_t;
    using slot_t = rh_set_slot_t;

    inner_t m_set;

    // =============================================================================================
    // Iterator
    // =============================================================================================

    class iterator {
        friend class const_iterator;

        inner_t *m_set = nullptr;
        usize    m_idx = 0;

        void skip_empty() {
            while (m_set != nullptr && m_idx < m_set->cap &&
                   m_set->metadata[m_idx] == ROBINHOOD_EMPTY_SENTINEL) {
                ++m_idx;
            }
        }

       public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = K;
        using difference_type = std::ptrdiff_t;
        using pointer = K *;
        using reference = K &;

        iterator() = default;

        iterator(inner_t *set, usize idx) : m_set{set}, m_idx{idx} { skip_empty(); }

        reference operator*() const { return m_set->slots[m_idx].key; }

        pointer operator->() const { return &m_set->slots[m_idx].key; }

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
            return a.m_set == b.m_set && a.m_idx == b.m_idx;
        }

        friend bool operator!=(const iterator &a, const iterator &b) { return !(a == b); }
    };

    class const_iterator {
        const inner_t *m_set = nullptr;
        usize          m_idx = 0;

        void skip_empty() {
            while (m_set != nullptr && m_idx < m_set->cap &&
                   m_set->metadata[m_idx] == ROBINHOOD_EMPTY_SENTINEL) {
                ++m_idx;
            }
        }

       public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const K;
        using difference_type = std::ptrdiff_t;
        using pointer = const K *;
        using reference = const K &;

        const_iterator() = default;

        const_iterator(const inner_t *set, usize idx) : m_set{set}, m_idx{idx} { skip_empty(); }

        const_iterator(const iterator &it) : m_set{it.m_set}, m_idx{it.m_idx} {}

        reference operator*() const { return m_set->slots[m_idx].key; }

        pointer operator->() const { return &m_set->slots[m_idx].key; }

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
            return a.m_set == b.m_set && a.m_idx == b.m_idx;
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
    using value_type = K;
    using slot_type = slot_t;
    using size_type = usize;
    using hasher = Hash;
    using key_equal = KeyEqual;

    // =============================================================================================
    // Constructors
    // =============================================================================================

    explicit RobinHoodSet(usize                      expected_size = ROBINHOOD_DEFAULT_CAPACITY,
                          std::pmr::memory_resource *resource = std::pmr::get_default_resource()) {
        ROBINHOOD_SET_INIT_EX(m_set, expected_size, pmr_alloc, pmr_free, resource);
    }

    explicit RobinHoodSet(std::pmr::memory_resource *resource)
        : RobinHoodSet(ROBINHOOD_DEFAULT_CAPACITY, resource) {}

    RobinHoodSet(RobinHoodSet &&o) noexcept : m_set{o.m_set} {
        std::memset(&o.m_set, 0, sizeof(o.m_set));
    }

    RobinHoodSet(const RobinHoodSet &) = delete;

    // =============================================================================================
    // Destructor
    // =============================================================================================

    ~RobinHoodSet() {
        if (m_set.metadata != nullptr) {
            ROBINHOOD_SET_DESTROY(m_set);
        }
    }

    // =============================================================================================
    // Operators
    // =============================================================================================

    RobinHoodSet &operator=(RobinHoodSet &&o) noexcept {
        if (this != &o) {
            if (m_set.metadata != nullptr) {
                ROBINHOOD_SET_DESTROY(m_set);
            }

            m_set = o.m_set;
            std::memset(&o.m_set, 0, sizeof(o.m_set));
        }

        return *this;
    }

    RobinHoodSet &operator=(const RobinHoodSet &) = delete;

    // =============================================================================================
    // API
    // =============================================================================================

    void insert(const K &k) {
        slot_type *slot = nullptr;
        ROBINHOOD_SET_INSERT(rh_set, m_set, k, &slot);
    }

    K *insert_key(const K &k) {
        slot_type *slot = nullptr;
        ROBINHOOD_SET_INSERT(rh_set, m_set, k, &slot);
        return slot ? &slot->key : nullptr;
    }

    slot_type *insert_slot(const K &k) {
        slot_type *slot = nullptr;
        ROBINHOOD_SET_INSERT(rh_set, m_set, k, &slot);
        return slot;
    }

    bool erase(const K &k) {
        bool r = false;
        ROBINHOOD_SET_DELETE(rh_set, m_set, k, &r);
        return r;
    }

    bool remove(const K &k) { return erase(k); }

    [[nodiscard]] bool contains(const K &k) const {
        bool r = false;
        ROBINHOOD_SET_CONTAINS(rh_set, m_set, k, &r);
        return r;
    }

    [[nodiscard]] K *find(const K &k) {
        slot_t *slot = nullptr;
        bool    found = false;

        ROBINHOOD_SET_FIND(rh_set, m_set, k, &slot, &found);
        return found ? &slot->key : nullptr;
    }

    [[nodiscard]] const K *find(const K &k) const {
        slot_t *slot = nullptr;
        bool    found = false;

        ROBINHOOD_SET_FIND(rh_set, m_set, k, &slot, &found);
        return found ? &slot->key : nullptr;
    }

    [[nodiscard]] slot_t *find_slot(const K &k) {
        slot_t *slot = nullptr;
        bool    found = false;

        ROBINHOOD_SET_FIND(rh_set, m_set, k, &slot, &found);
        return found ? slot : nullptr;
    }

    [[nodiscard]] const slot_t *find_slot(const K &k) const {
        slot_t *slot = nullptr;
        bool    found = false;

        ROBINHOOD_SET_FIND(rh_set, m_set, k, &slot, &found);
        return found ? slot : nullptr;
    }

    const K &at(const K &k) const {
        const K *found = find(k);
        if (!found) {
            throw std::out_of_range("RobinHoodSet::at: key not found");
        }

        return *found;
    }

    void reserve(usize expected_size) { ROBINHOOD_SET_RESERVE(rh_set, m_set, expected_size); }

    void shrink_to_fit() { ROBINHOOD_SET_SHRINK_TO_FIT(rh_set, m_set); }

    // =============================================================================================
    // Iterator
    // =============================================================================================

    iterator begin() { return iterator(&m_set, 0); }

    iterator end() { return iterator(&m_set, m_set.cap); }

    const_iterator begin() const { return const_iterator(&m_set, 0); }

    const_iterator end() const { return const_iterator(&m_set, m_set.cap); }

    const_iterator cbegin() const { return const_iterator(&m_set, 0); }

    const_iterator cend() const { return const_iterator(&m_set, m_set.cap); }

    // =============================================================================================
    // Misc
    // =============================================================================================

    [[nodiscard]] bool empty() const { return m_set.len == 0; }

    [[nodiscard]] usize size() const { return m_set.len; }

    [[nodiscard]] usize capacity() const { return m_set.cap; }

    [[nodiscard]] usize load() const { return ROBINHOOD_SET_LOAD(m_set); }

    [[nodiscard]] usize max_psl() const { return m_set.max_psl; }

    inner_t &raw() { return m_set; }

    const inner_t &raw() const { return m_set; }
};

}  // namespace lsr::robinhood

#endif  // LRUISINGER_ROBINHOOD_INCLUDE_ROBINHOOD_ROBINHOOD_SET_HPP_
