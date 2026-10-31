find_package(GTest REQUIRED)

# Unit Tests Executable
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

# Integration & System Tests Executable
add_executable(integration_tests
    integration/test_reverse_proxy.cpp
    integration/test_health_checker.cpp
    integration/test_server_concurrency.cpp
)
target_link_libraries(integration_tests PRIVATE load_balancer_core GTest::gtest GTest::gtest_main pthread)
add_test(NAME IntegrationTests COMMAND integration_tests)

# Regression & Negative Tests Executable
add_executable(regression_tests
    regression/test_edge_cases.cpp
)
target_link_libraries(regression_tests PRIVATE load_balancer_core GTest::gtest GTest::gtest_main pthread)
add_test(NAME RegressionTests COMMAND regression_tests)

# Fuzz Testing Target
add_executable(fuzz_http_parser
    fuzz/fuzz_http_parser.cpp
)
target_link_libraries(fuzz_http_parser PRIVATE load_balancer_core pthread)
add_test(NAME FuzzHttpParser COMMAND fuzz_http_parser)
