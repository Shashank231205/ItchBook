add_executable(itchbook_benchmarks bench_order_book.cpp bench_order_id_map.cpp bench_pipeline.cpp)
target_link_libraries(itchbook_benchmarks PRIVATE itchbook::itchbook itchbook::warnings
                                                  benchmark::benchmark_main)
