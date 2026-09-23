cmake_minimum_required(VERSION 3.10)
project(GFSIM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gfsim STATIC src/Reporter.cpp src/Logger.cpp src/SimSystem.cpp
                         src/SimAssert.cpp src/SimMemory.cpp src/SimTestbench.cpp
                         src/SimTrace.cpp src/SimGraph.cpp)
target_include_directories(gfsim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gfsim PUBLIC cxx_std_11)

include(CTest)
if(BUILD_TESTING)
    add_executable(simobj_simsys_test tests/simobj/test_simobj_simsys.cpp)
    target_link_libraries(simobj_simsys_test PRIVATE gfsim)
    add_test(NAME simobj_simsys COMMAND simobj_simsys_test)

    add_executable(simmodule_test tests/simmodule/test_simmodule.cpp)
    target_link_libraries(simmodule_test PRIVATE gfsim)
    add_test(NAME simmodule COMMAND simmodule_test)

    add_executable(report_logging_test tests/platform/test_report_logging.cpp)
    target_link_libraries(report_logging_test PRIVATE gfsim)
    add_test(NAME report_logging COMMAND report_logging_test)

    set(MODULE_NAME TemplateSmoke)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)
    configure_file(templates/SimModule.h.in
                   ${CMAKE_CURRENT_BINARY_DIR}/generated/TemplateSmoke.h @ONLY)
    configure_file(templates/SimModule.cpp.in
                   ${CMAKE_CURRENT_BINARY_DIR}/generated/TemplateSmoke.cpp @ONLY)
    add_executable(template_smoke_test
                   tests/templates/template_smoke.cpp
                   ${CMAKE_CURRENT_BINARY_DIR}/generated/TemplateSmoke.cpp)
    target_include_directories(template_smoke_test PRIVATE
                               ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(template_smoke_test PRIVATE gfsim)
    add_test(NAME template_smoke COMMAND template_smoke_test)

    add_executable(three_stage_pipeline_test
                   tests/pipeline/test_three_stage_pipeline.cpp)
    target_link_libraries(three_stage_pipeline_test PRIVATE gfsim)
    add_test(NAME three_stage_pipeline COMMAND three_stage_pipeline_test)

    add_executable(simmemory_test tests/platform/test_simmemory.cpp)
    target_link_libraries(simmemory_test PRIVATE gfsim)
    add_test(NAME simmemory COMMAND simmemory_test ${CMAKE_CURRENT_BINARY_DIR})

    add_executable(simtestbench_test tests/platform/test_simtestbench.cpp)
    target_link_libraries(simtestbench_test PRIVATE gfsim)
    add_test(NAME simtestbench COMMAND simtestbench_test)

    add_executable(simtrace_test tests/trace/test_simtrace.cpp)
    target_link_libraries(simtrace_test PRIVATE gfsim)
    add_test(NAME simtrace COMMAND simtrace_test
             ${CMAKE_CURRENT_BINARY_DIR}/simtrace-test.json)

    add_executable(simgraph_test tests/graph/test_simgraph.cpp)
    target_link_libraries(simgraph_test PRIVATE gfsim)
    add_test(NAME simgraph COMMAND simgraph_test
             ${CMAKE_CURRENT_BINARY_DIR}/simgraph-test.dot)

    add_executable(topology_contract_test tests/graph/test_topology_contract.cpp)
    target_link_libraries(topology_contract_test PRIVATE gfsim)
    add_test(NAME topology_contract COMMAND topology_contract_test)

    add_executable(pure_transaction_test tests/pure/test_pure_transaction.cpp)
    target_link_libraries(pure_transaction_test PRIVATE gfsim)
    add_test(NAME pure_transaction COMMAND pure_transaction_test)

    add_executable(flush_rid_test tests/queue/test_flush_rid.cpp)
    target_link_libraries(flush_rid_test PRIVATE gfsim)
    add_test(NAME flush_rid COMMAND flush_rid_test)
endif()
