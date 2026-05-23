# Robinhood

Header-only Robin Hood hashmap.

## Features and Limitations

- Robin Hood open-addressing hashmap
- C backend API
- C++ wrapper
- Keys and values must be trivially copyable
- Iteration order is unspecified
- Pointers/references may be invalidated after insert/reserve/rehash
- Not thread-safe unless externally synchronized

## Installation

This library is header-only. Add the repository to your project and link against the CMake target.

```cmake
include(FetchContent)
FetchContent_Declare(
        robinhood
        GIT_REPOSITORY git@github.com:LuisRuisinger/robinhood.git
        GIT_TAG v0.1.0
)

FetchContent_MakeAvailable(robinhood)
target_link_libraries(your_target PRIVATE lsr::robinhood)
```

and then use it like

```c++
#include <robinhood/robinhood_map.hpp>

int main() {
    lsr::robinhood::RobinHoodMap<int, int> map;

    map.insert(1, 42);

    if (auto* value = map.find(1)) {
        return *value == 42 ? 0 : 1;
    }

    return 1;
}
```
