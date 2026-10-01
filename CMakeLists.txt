cmake_minimum_required(VERSION 3.5)
project(GisCupBonn)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Enable/Disable output of compile commands during generation

if(NOT DEFINED LIBRARY_DIR)
    set(LIBRARY_DIR "${CMAKE_SOURCE_DIR}/external/libraries")
endif()

message(STATUS "LIBRARY_DIR= ${LIBRARY_DIR}")
set(CMAKE_PREFIX_PATH ${LIBRARY_DIR} ${CMAKE_PREFIX_PATH})


find_package(CGAL 6.0.1 REQUIRED)
find_package(Boost 1.83 REQUIRED )
find_package(GDAL REQUIRED)
find_package(OpenMP REQUIRED)

message(STATUS "CGAL version: ${CGAL_VERSION}")
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "GDAL_INCLUDE_DIRS = ${GDAL_INCLUDE_DIRS}")
message(STATUS "GDAL_LIBRARIES    = ${GDAL_LIBRARIES}")

# Collect sources of different subprojects
file(GLOB_RECURSE GEOMETRY_SOURCES "src/geometry/*.cpp")
file(GLOB_RECURSE DISCRETE_SOURCES "src/discrete/*.cpp")
file(GLOB_RECURSE UTILITY_SOURCES "src/utility/*.cpp")


add_library(geometry_code
        ${GEOMETRY_SOURCES}
)

target_include_directories(geometry_code
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include/geometry
        ${PROJECT_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
        ${GDAL_INCLUDE_DIRS}
)

target_link_libraries(geometry_code
        PUBLIC
        CGAL::CGAL
        GDAL::GDAL
        Boost::headers
        OpenMP::OpenMP_CXX
        ${GDAL_LIBRARIES}
)


add_library(utility_code
        ${UTILITY_SOURCES}
)

target_include_directories(utility_code
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include/
        ${PROJECT_SOURCE_DIR}/external/libraries/toml++/
)

target_link_libraries(utility_code
        geometry_code
)


add_library(discrete_code
        ${DISCRETE_SOURCES}
)

target_include_directories(discrete_code
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include/discrete
)

target_link_libraries(discrete_code
        geometry_code
        utility_code
)

add_library(GisCupBonn_lib INTERFACE)

target_include_directories(GisCupBonn_lib INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(GisCupBonn_lib INTERFACE
      geometry_code
      discrete_code
      utility_code
)


add_executable(GisCupBonn_RunCrossproduct src/mains/main_run_crossproduct.cpp)
target_link_libraries(GisCupBonn_RunCrossproduct PRIVATE GisCupBonn_lib)

add_executable(GisCupBonn_RunCrossproductParallel src/mains/main_run_crossproduct_parallel.cpp)
target_link_libraries(GisCupBonn_RunCrossproductParallel PRIVATE GisCupBonn_lib)

add_executable(GisCupBonn_RunCrossproductSeededParallel src/mains/main_run_crossproduct_seeded_parallel.cpp)
target_link_libraries(GisCupBonn_RunCrossproductSeededParallel PRIVATE GisCupBonn_lib)

add_executable(GisCupBonn_RunOne src/mains/main_run_one.cpp)
target_link_libraries(GisCupBonn_RunOne PRIVATE GisCupBonn_lib)

add_executable(GisCupBonn_RunOneSeededParallel src/mains/main_run_one_seeded_parallel.cpp)
target_link_libraries(GisCupBonn_RunOneSeededParallel PRIVATE GisCupBonn_lib)

add_executable(GisCupBonn_VisualizeSolutions src/mains/main_visualize_solutions.cpp)
target_link_libraries(GisCupBonn_VisualizeSolutions PRIVATE GisCupBonn_lib)

add_executable(GisCupBonn_FinalSeededCrossproduct src/mains/main_final_crossproduct_seeded_parallel.cpp)
target_link_libraries(GisCupBonn_FinalSeededCrossproduct PRIVATE GisCupBonn_lib)


# gtest
option(TESTS "Build the tests" ON)
if (TESTS)
    enable_testing()
    include(GoogleTest)
    add_subdirectory("${PROJECT_SOURCE_DIR}/external/libraries/googletest")

    macro(package_add_test TESTNAME)
        # create an executable in which the tests will be stored
        add_executable(${TESTNAME} ${ARGN})
        # link the Google test infrastructure, mocking library, and a default main function to
        # the test executable.  Remove g_test_main if writing your own main function.
        target_include_directories(${TESTNAME} PRIVATE ${PROJECT_SOURCE_DIR}/include)
        target_link_libraries(${TESTNAME}
                PRIVATE
                geometry_code
                discrete_code
                utility_code
                gtest
                gmock
                gtest_main
        )

        # gtest_discover_tests replaces gtest_add_tests,
        # see https://cmake.org/cmake/help/v3.10/module/GoogleTest.html for more options to pass to it
        gtest_discover_tests(${TESTNAME}
                # set a working directory so your project root so that you can find test data via paths relative to the project root
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_DIR}"
        )
        set_target_properties(${TESTNAME} PROPERTIES FOLDER tests)
    endmacro()

    # ADD TEST DIRECTORIES HERE
    add_subdirectory(${PROJECT_SOURCE_DIR}/test)
endif ()
