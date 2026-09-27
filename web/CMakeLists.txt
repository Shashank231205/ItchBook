add_executable(itchbook_wasm wasm_api.cpp)
target_link_libraries(itchbook_wasm PRIVATE itchbook::itchbook itchbook::warnings)
set_target_properties(itchbook_wasm PROPERTIES OUTPUT_NAME itchbook SUFFIX ".js")
target_link_options(
  itchbook_wasm
  PRIVATE
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createItchBook
  -sENVIRONMENT=web,node
  -sALLOW_MEMORY_GROWTH=1
  -sINITIAL_MEMORY=64MB
  -sEXPORTED_RUNTIME_METHODS=HEAPF64,UTF8ToString)
