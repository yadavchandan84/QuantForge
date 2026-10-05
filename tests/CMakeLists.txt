include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)
qf_fetch_googletest()

include(GoogleTest)

add_executable(quantforge_tests
    test_event_queue.cpp
    test_data_handler.cpp
    test_strategies.cpp
    test_execution.cpp
    test_portfolio.cpp
    test_analytics.cpp
    test_engine_sweep.cpp
    test_determinism.cpp
)

target_link_libraries(quantforge_tests
    PRIVATE
        quantforge::core
        GTest::gtest_main
)

# Keep test code strict but do not fail on third-party warnings.
target_link_libraries(quantforge_tests PRIVATE qf_project_options)

gtest_discover_tests(quantforge_tests)
