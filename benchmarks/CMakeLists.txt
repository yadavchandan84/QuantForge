# Benchmarks use a tiny self-contained harness (no external deps) so they are
# easy to build and run in CI. Each benchmark reports measured numbers only.
add_executable(bench_event_queue bench_event_queue.cpp)
target_link_libraries(bench_event_queue PRIVATE quantforge::core qf_project_options)

add_executable(bench_engine bench_engine.cpp)
target_link_libraries(bench_engine PRIVATE quantforge::core qf_project_options)
