# Core backtesting library: quantforge_core
add_library(quantforge_core
    event.cpp
    data_handler.cpp
    strategies.cpp
)

target_include_directories(quantforge_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

target_link_libraries(quantforge_core
    PUBLIC qf_project_options
)

find_package(Threads REQUIRED)
target_link_libraries(quantforge_core PUBLIC Threads::Threads)

add_library(quantforge::core ALIAS quantforge_core)
