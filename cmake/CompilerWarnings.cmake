add_library(itchbook_warnings INTERFACE)
add_library(itchbook::warnings ALIAS itchbook_warnings)

target_compile_options(
  itchbook_warnings
  INTERFACE -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wold-style-cast
            -Wcast-align
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
            -Wno-missing-field-initializers
            $<$<BOOL:${ITCHBOOK_WARNINGS_AS_ERRORS}>:-Werror>)
