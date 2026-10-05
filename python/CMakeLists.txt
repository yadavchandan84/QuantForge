include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)
qf_fetch_pybind11()

pybind11_add_module(_core MODULE bindings.cpp)

target_link_libraries(_core PRIVATE quantforge::core)

# The core library is built with aggressive warnings; the bindings include
# pybind headers that may trip -Wconversion, so compile bindings with the
# project standard but without inheriting the strict warning interface.
target_compile_features(_core PRIVATE cxx_std_20)

# On MinGW/GCC, statically link the C++/GCC runtimes into the extension so the
# compiled .pyd is self-contained and does not depend on libstdc++-6.dll etc.
# being on PATH at import time (which Python's DLL loader ignores by default).
if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND WIN32))
    # -static pulls in libgcc, libstdc++ and libwinpthread statically, making
    # the .pyd fully self-contained (only system UCRT + python3xx.dll remain).
    target_link_options(_core PRIVATE
        -static -static-libgcc -static-libstdc++)
endif()

# Install destination used by scikit-build-core (see pyproject.toml). When built
# standalone (QF_BUILD_PYTHON=ON via plain CMake) this is harmless.
if(DEFINED SKBUILD)
    install(TARGETS _core LIBRARY DESTINATION quantforge)
endif()
