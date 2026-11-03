# Compiler warning configuration for targets of this project.
#
# radiobrowser_set_warnings(<target> STRICT|RELAXED)
#   STRICT  - full warning set, used for the library and examples
#   RELAXED - common warnings, used for tests (gtest macros trigger -Wconversion)
# Warnings become errors with RADIOBROWSER_WARNINGS_AS_ERRORS. These flags are
# private to the targets and never leak to consumers.

function(radiobrowser_set_warnings target level)
  if(MSVC)
    set(flags /W4 /permissive- /utf-8 /Zc:__cplusplus)
    if(RADIOBROWSER_WARNINGS_AS_ERRORS)
      list(APPEND flags /WX)
    endif()
  else()
    set(flags -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
              -Wcast-align -Wformat=2 -Wimplicit-fallthrough)
    if(level STREQUAL "STRICT")
      list(APPEND flags -Wconversion -Wsign-conversion -Wold-style-cast -Wdouble-promotion
           -Wundef)
    endif()
    if(RADIOBROWSER_WARNINGS_AS_ERRORS)
      list(APPEND flags -Werror)
    endif()
  endif()
  target_compile_options(${target} PRIVATE ${flags})
endfunction()
