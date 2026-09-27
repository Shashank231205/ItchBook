add_executable(
  itchbook_tests
  test_book_builder.cpp
  test_feed_generator.cpp
  test_index_pool.cpp
  test_latency_histogram.cpp
  test_order_book.cpp
  test_order_id_map.cpp
  test_parser.cpp
  test_spsc_queue.cpp)

if(UNIX)
  target_sources(itchbook_tests PRIVATE test_mapped_file.cpp)
endif()

target_link_libraries(itchbook_tests PRIVATE itchbook::itchbook itchbook::warnings GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(itchbook_tests DISCOVERY_TIMEOUT 60)
