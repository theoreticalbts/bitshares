
add_executable( json_relaxed_test json_relaxed_test.cpp )
target_link_libraries( json_relaxed_test fc )
target_include_directories( json_relaxed_test
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../.." )
