enable_testing()

# 添加测试
function(add_auto_test test_source)
    get_filename_component(test_name ${test_source} NAME_WE)
    get_filename_component(abs_path ${test_source} ABSOLUTE)
    add_executable(${test_name} ${abs_path})
    target_link_libraries(${test_name} ${LIBRARY_NAME})
    add_test(NAME ${test_name} COMMAND ${test_name})
    add_dependencies(build_all_tests ${test_name})
endfunction()

add_custom_target(build_all_tests COMMENT "Building all tests")

# 自动查找并添加
file(GLOB TEST_SOURCES  
    "*_test.cc"
    "*_test.cpp" 
    "test_*.cc"
    "test_*.cpp")
foreach(source ${TEST_SOURCES})
    add_auto_test(${source})
endforeach()
