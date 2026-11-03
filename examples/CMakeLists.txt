# The examples deliberately use only C++17, like many consumers.

function(radiobrowser_add_example target source output_name)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE radiobrowser::radiobrowser)
  set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
                                             CXX_EXTENSIONS OFF OUTPUT_NAME ${output_name})
  radiobrowser_set_warnings(${target} STRICT)
  if(MSVC)
    # std::getenv is fine here; silence MSVC's deprecation warning (C4996).
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endfunction()

radiobrowser_add_example(radiobrowser_cli cli/main.cpp radiobrowser-cli)
radiobrowser_add_example(radiobrowser_caching_transport caching_transport/main.cpp
                         radiobrowser-caching-transport)
