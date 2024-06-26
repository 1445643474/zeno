SET(Boost_ADDITIONAL_VERSIONS "1.82.0" "1.82")

FIND_PACKAGE(Eigen3 REQUIRED NO_MODULE)
FIND_PACKAGE(OpenMP REQUIRED)
FIND_PACKAGE(Boost COMPONENTS graph)
#find_package(TBB CONFIG REQUIRED COMPONENTS tbb tbbmalloc)

FILE(GLOB_RECURSE SOURCE_FILES src/*.cpp src/*.h src/thirdparty/parson.c src/thirdparty/tinyspline.c src/thirdparty/tinysplinecxx.cxx)

ADD_LIBRARY(Roads STATIC ${SOURCE_FILES})

TARGET_INCLUDE_DIRECTORIES(Roads PUBLIC include)
TARGET_LINK_LIBRARIES(Roads PRIVATE Eigen3::Eigen)

ADD_EXECUTABLE(RoadsTest test/main.cpp)
TARGET_LINK_LIBRARIES(RoadsTest PUBLIC Roads)

IF (OPENMP_FOUND)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" ${OpenMP_EXE_LINKER_FLAGS})
    TARGET_LINK_LIBRARIES(Roads PRIVATE OpenMP::OpenMP_CXX)
    if (MSVC)
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("/openmp:experimental" MSVC_SUPPORT_OPENMP_EXPERIMENTAL)
        if (MSVC_SUPPORT_OPENMP_EXPERIMENTAL)
            target_compile_options(zeno PRIVATE $<IF:$<COMPILE_LANGUAGE:CXX>,/openmp:experimental,>)
        endif()
    endif()
ELSE()
    MESSAGE(FATAL_ERROR "Roads Module is depending on OpenMP. You have to install it first.")
ENDIF()

IF (TARGET Boost::graph)
    TARGET_LINK_LIBRARIES(Roads PRIVATE Boost::graph)
ELSE()
    MESSAGE(FATAL_ERROR "Roads Module is depending on Boost::graph. You have to install it first.")
ENDIF()

#TARGET_LINK_LIBRARIES(Roads PRIVATE TBB::tbb TBB::tbbmalloc)

ADD_COMPILE_DEFINITIONS(TINYSPLINE_EXPORT=1)
ADD_COMPILE_DEFINITIONS(TINYSPLINE_FLOAT_PRECISION=1)
