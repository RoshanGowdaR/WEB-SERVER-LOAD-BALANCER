find_package(GTest REQUIRED)

# 1. Unit Tests
add_executable(unit_tests
    unit/test_socket.cpp
    unit/test_http_parser.cpp
    unit/test_thread_pool.cpp
    unit/test_load_balancers.cpp
    unit/test_config.cpp
    unit/test_metrics.cpp
)
target_link_libraries(unit_tests PRIVATE load_balancer_core GTest::gtest GTest::gtest_main pthread)
add_test(NAME UnitTests COMMAND unit_tests)

# 2. Integration Tests
add_executable(integration_tests
    integration/test_reverse_proxy.cpp
    integration/test_health_checker.cpp
    integration/test_server_concurrency.cpp
)
target_link_libraries(integration_tests PRIVATE load_balancer_core GTest::gtest GTest::gtest_main pthread)
add_test(NAME IntegrationTests COMMAND integration_tests)

# 3. Security Tests
add_executable(security_tests
    security/test_http_smuggling.cpp
)
target_link_libraries(security_tests PRIVATE load_balancer_core GTest::gtest GTest::gtest_main pthread)
add_test(NAME SecurityTests COMMAND security_tests)

# 4. Concurrency Tests
add_executable(concurrency_tests
    concurrency/test_concurrent_clients.cpp
)
target_link_libraries(concurrency_tests PRIVATE load_balancer_core GTest::gtest GTest::gtest_main pthread)
add_test(NAME ConcurrencyTests COMMAND concurrency_tests)

# 5. Regression Tests
add_executable(regression_tests
    regression/test_edge_cases.cpp
)
target_link_libraries(regression_tests PRIVATE load_balancer_core GTest::gtest GTest::gtest_main pthread)
add_test(NAME RegressionTests COMMAND regression_tests)

# 6. Randomized Smoke Fuzzer (Standalone portable test)
add_executable(fuzz_http_parser
    fuzz/fuzz_http_parser.cpp
)
target_link_libraries(fuzz_http_parser PRIVATE load_balancer_core pthread)
add_test(NAME FuzzHttpParserSmoke COMMAND fuzz_http_parser)

# 7. Coverage-Guided libFuzzer Targets (Enabled when compiling with Clang and ENABLE_LIBFUZZER)
if(ENABLE_LIBFUZZER)
    message(STATUS "Configuring LLVM libFuzzer targets with -fsanitize=fuzzer")
    add_executable(fuzz_request_parser fuzz/fuzz_request_parser.cpp)
    target_compile_options(fuzz_request_parser PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_request_parser PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_libraries(fuzz_request_parser PRIVATE load_balancer_core)

    add_executable(fuzz_response_parser fuzz/fuzz_response_parser.cpp)
    target_compile_options(fuzz_response_parser PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_response_parser PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_libraries(fuzz_response_parser PRIVATE load_balancer_core)
endif()
