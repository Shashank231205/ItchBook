add_library(
  itchbook
  book/order_book.cpp
  perf/latency_histogram.cpp
  perf/tsc_clock.cpp
  platform/cpu.cpp
  sim/feed_generator.cpp)

if(UNIX)
  target_sources(itchbook PRIVATE io/mapped_file.cpp)
endif()

add_library(itchbook::itchbook ALIAS itchbook)

target_include_directories(itchbook PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_compile_features(itchbook PUBLIC cxx_std_20)
target_link_libraries(itchbook PRIVATE itchbook::warnings)
if(TARGET Threads::Threads)
  target_link_libraries(itchbook PUBLIC Threads::Threads)
endif()

if(ITCHBOOK_NATIVE)
  target_compile_options(itchbook PUBLIC -march=native)
endif()
