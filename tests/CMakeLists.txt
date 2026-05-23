add_executable(robinhood_tests
        test_robinhood_map.cpp
)

target_link_libraries(robinhood_tests
        PRIVATE
        lsr::robinhood
)

target_compile_features(robinhood_tests
        PRIVATE
        cxx_std_17
)

if(ROBINHOOD_ENABLE_CLANG_TIDY)
    set_target_properties(robinhood_tests PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
    )
endif()

add_test(NAME robinhood_tests
        COMMAND robinhood_tests
)