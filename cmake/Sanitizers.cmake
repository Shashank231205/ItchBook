# Applied globally so third-party code (GoogleTest) is instrumented too; ThreadSanitizer in
# particular reports false positives when only part of the program is instrumented.
if(ITCHBOOK_SANITIZERS)
  message(STATUS "ItchBook: sanitizers enabled: ${ITCHBOOK_SANITIZERS}")
  add_compile_options(-fsanitize=${ITCHBOOK_SANITIZERS} -fno-omit-frame-pointer
                      -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${ITCHBOOK_SANITIZERS})
endif()
