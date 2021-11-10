zeno_glob_recurse(source zeno *.h *.cpp)
target_sources(zeno PRIVATE ${source})
target_include_directories(zeno PUBLIC zeno)

if (ZENO_WITH_SYSTEM_TBB)
    message("-- Building Zeno with System TBB")
    find_package(TBB REQUIRED)
    target_link_libraries(zeno PUBLIC TBB::tbb)
    target_compile_definitions(zeno PUBLIC -DZENO_WITH_SYSTEM_TBB)
else()
    message("-- Building Zeno with Bundled TBB")
    add_subdirectory(depends/tbb)
    target_link_libraries(zeno PUBLIC tbb)
endif()

if (ZENO_WITH_SYCL)
    message("-- Building Zeno with hipSYCL targets: [${HIPSYCL_TARGETS}]")
    find_package(hipSYCL CONFIG REQUIRED)
    add_sycl_to_target(TARGET zeno)
    target_compile_definitions(zeno PUBLIC -DZENO_WITH_SYCL)
endif()

if (ZENO_WITH_LEGACY)
    message("-- Building Zeno with Legacy Nodes")
    zeno_glob_recurse(source legacy *.h *.cpp)
    target_include_directories(zeno PUBLIC legacy)
    target_sources(zeno PRIVATE ${source})
endif()

if (ZENO_WITH_BACKWARD)
    message("-- Building Zeno with Stack Traceback")
    add_subdirectory(depends/backward-cpp)
    target_sources(zeno PRIVATE ${BACKWARD_ENABLE})
    add_backward(zeno)
endif()
