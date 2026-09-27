add_executable(itchbook-gen itchbook_gen.cpp)
target_link_libraries(itchbook-gen PRIVATE itchbook::itchbook itchbook::warnings)

if(UNIX)
  add_executable(itchbook-replay itchbook_replay.cpp)
  target_link_libraries(itchbook-replay PRIVATE itchbook::itchbook itchbook::warnings)
endif()
