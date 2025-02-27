file(GLOB _src *.cpp *.hpp *.c *.h)
set(_src ${_src})
add_executable(task4 ${_src})

target_include_directories(task4 PRIVATE . ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(task4 SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

target_link_libraries(task4 ${LLVM_LIBS})
