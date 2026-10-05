# Centralized third-party dependency management via FetchContent.
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# GoogleTest -----------------------------------------------------------------
function(qf_fetch_googletest)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    # Prevent GoogleTest from overriding our compiler/linker options on MSVC.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endfunction()

# pybind11 -------------------------------------------------------------------
function(qf_fetch_pybind11)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.6
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(pybind11)
endfunction()
