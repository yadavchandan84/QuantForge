# Core backtesting library: quantforge_core
add_library(quantforge_core
    event.cpp
    data_handler.cpp
    strategies.cpp
    execution_handler.cpp
    portfolio.cpp
    analytics.cpp
    engine.cpp
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

# The static core is linked into the Python extension (a shared object), so it
# must be position-independent on Linux/macOS. Harmless for executables.
set_target_properties(quantforge_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(quantforge::core ALIAS quantforge_core)
