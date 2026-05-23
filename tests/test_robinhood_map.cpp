#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory_resource>
#include <ostream>
#include <stdexcept>

#include <robinhood/robinhood_map.hpp>

namespace {

using lsr::robinhood::RobinHoodMap;

struct CountingResource final : std::pmr::memory_resource {
    std::pmr::memory_resource* upstream = std::pmr::new_delete_resource();

    std::size_t allocations = 0;
    std::size_t deallocations = 0;
    std::size_t bytes_allocated = 0;
    std::size_t bytes_deallocated = 0;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocations;
        bytes_allocated += bytes;
        return upstream->allocate(bytes, alignment);
    }

    void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override {
        ++deallocations;
        bytes_deallocated += bytes;
        upstream->deallocate(ptr, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

void test_empty_map() {
    RobinHoodMap<int, int> map;

    assert(map.empty());
    assert(map.size() == 0);
    assert(!map.contains(1));
    assert(map.find(1) == nullptr);
}

void test_insert_find_contains() {
    RobinHoodMap<int, int> map;

    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);

    assert(!map.empty());
    assert(map.size() == 3);

    assert(map.contains(1));
    assert(map.contains(2));
    assert(map.contains(3));
    assert(!map.contains(4));

    assert(map.find(1) != nullptr);
    assert(*map.find(1) == 10);

    assert(map.find(2) != nullptr);
    assert(*map.find(2) == 20);

    assert(map.find(3) != nullptr);
    assert(*map.find(3) == 30);
}

void test_at() {
    RobinHoodMap<int, int> map;

    map.insert(7, 77);

    assert(map.at(7) == 77);

    bool threw = false;

    try {
        static_cast<void>(map.at(8));
    } catch (const std::out_of_range&) {
        threw = true;
    }

    assert(threw);
}

void test_get() {
    RobinHoodMap<int, int> map;

    map.insert(5, 50);

    const auto present = map.get(5);
    const auto missing = map.get(6);

    assert(present.has_value());
    assert(*present == 50);
    assert(!missing.has_value());
}

void test_erase() {
    RobinHoodMap<int, int> map;

    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);

    assert(map.erase(2));
    assert(!map.contains(2));
    assert(map.find(2) == nullptr);
    assert(map.size() == 2);

    assert(map.contains(1));
    assert(map.contains(3));

    assert(!map.erase(99));
    assert(map.size() == 2);
}

void test_remove_alias() {
    RobinHoodMap<int, int> map;

    map.insert(1, 10);

    assert(map.remove(1));
    assert(!map.contains(1));
    assert(!map.remove(1));
}

void test_insert_slot() {
    RobinHoodMap<int, int> map;

    auto* slot = map.insert_slot(42, 9001);

    assert(slot != nullptr);
    assert(slot->key == 42);
    assert(slot->value == 9001);

    auto* found = map.find_slot(42);

    assert(found != nullptr);
    assert(found->key == 42);
    assert(found->value == 9001);
}

void test_iteration() {
    RobinHoodMap<int, int> map;

    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);

    int count = 0;
    int value_sum = 0;

    for (const auto& slot : map) {
        ++count;
        value_sum += slot.value;
    }

    assert(count == 3);
    assert(value_sum == 60);
}

void test_const_iteration() {
    RobinHoodMap<int, int> map;

    map.insert(1, 10);
    map.insert(2, 20);

    const auto& const_map = map;

    int count = 0;

    for (const auto& slot : const_map) {
        assert(const_map.contains(slot.key));
        ++count;
    }

    assert(count == 2);
}

void test_reserve() {
    RobinHoodMap<int, int> map;

    map.reserve(1024);

    assert(map.capacity() >= 1024);

    for (int i = 0; i < 512; ++i) {
        map.insert(i, i * 10);
    }

    for (int i = 0; i < 512; ++i) {
        assert(map.contains(i));
        assert(*map.find(i) == i * 10);
    }
}

void test_shrink_to_fit() {
    RobinHoodMap<int, int> map;

    map.reserve(1024);

    const auto large_capacity = map.capacity();

    map.insert(1, 10);
    map.insert(2, 20);

    map.shrink_to_fit();

    assert(map.capacity() <= large_capacity);
    assert(map.contains(1));
    assert(map.contains(2));
}

void test_many_insertions() {
    RobinHoodMap<int, int> map;

    int n = 10'000;
    for (int i = 0; i < n; ++i) {
        map.insert(i, i + 1);
    }

    assert(map.size() == n);

    for (int i = 0; i < n; ++i) {
        auto* value = map.find(i);
        assert(value != nullptr);
        assert(*value == i + 1);
    }
}

void test_delete_and_reinsert() {
    RobinHoodMap<int, int> map;

    for (int i = 0; i < 1000; ++i) {
        map.insert(i, i);
    }

    for (int i = 0; i < 1000; i += 2) {
        assert(map.erase(i));
    }

    for (int i = 0; i < 1000; i += 2) {
        assert(!map.contains(i));
    }

    for (int i = 0; i < 1000; i += 2) {
        map.insert(i, i * 2);
    }

    for (int i = 0; i < 1000; ++i) {
        auto* value = map.find(i);
        assert(value != nullptr);

        if (i % 2 == 0) {
            assert(*value == i * 2);
        } else {
            assert(*value == i);
        }
    }
}

void test_move_constructor() {
    RobinHoodMap<int, int> original;

    original.insert(1, 10);
    original.insert(2, 20);

    RobinHoodMap<int, int> moved{std::move(original)};

    assert(moved.size() == 2);
    assert(moved.contains(1));
    assert(moved.contains(2));
    assert(*moved.find(1) == 10);
    assert(*moved.find(2) == 20);
}

void test_move_assignment() {
    RobinHoodMap<int, int> source;
    source.insert(1, 10);
    source.insert(2, 20);

    RobinHoodMap<int, int> target;
    target.insert(99, 99);

    target = std::move(source);

    assert(target.size() == 2);
    assert(target.contains(1));
    assert(target.contains(2));
    assert(!target.contains(99));
}

void test_pmr_resource_is_used() {
    CountingResource resource;

    {
        RobinHoodMap<int, int> map{&resource};

        map.insert(1, 10);
        map.insert(2, 20);
        map.reserve(128);

        assert(map.contains(1));
        assert(map.contains(2));
        assert(resource.allocations > 0);
    }

    assert(resource.deallocations > 0);
}

} // namespace

int main() {
    test_empty_map();
    test_insert_find_contains();
    test_at();
    test_get();
    test_erase();
    test_remove_alias();
    test_insert_slot();
    test_iteration();
    test_const_iteration();
    test_reserve();
    test_shrink_to_fit();
    test_many_insertions();
    test_delete_and_reinsert();
    test_move_constructor();
    test_move_assignment();
    test_pmr_resource_is_used();

    return 0;
}