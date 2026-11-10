function(eden_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor
      -Woverloaded-virtual -Wnull-dereference -Wformat=2)
  endif()
endfunction()
