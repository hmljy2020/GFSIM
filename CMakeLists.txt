cmake_minimum_required(VERSION 3.10)
project(GFSIM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

add_library(gfsim STATIC src/Reporter.cpp src/Logger.cpp src/SimSystem.cpp
                         src/SimAssert.cpp src/SimMemory.cpp src/SimTestbench.cpp
                         src/SimTrace.cpp src/SimGraph.cpp src/SimModule.cpp
                         src/SimExecutor.cpp)
target_link_libraries(gfsim PUBLIC Threads::Threads)
target_include_directories(gfsim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gfsim PUBLIC cxx_std_11)

add_library(counter_example_model STATIC examples/counter/CounterSystem.cpp
            examples/counter/Counter.cpp examples/counter/Client.cpp)
target_link_libraries(counter_example_model PUBLIC gfsim)
target_include_directories(counter_example_model PUBLIC examples/counter)
add_executable(counter_example examples/counter/main.cpp)
target_link_libraries(counter_example PRIVATE counter_example_model)

add_library(mesh_example_model STATIC examples/mesh/MeshBench.cpp
            examples/mesh/MeshNode.cpp)
target_link_libraries(mesh_example_model PUBLIC gfsim)
target_include_directories(mesh_example_model PUBLIC examples/mesh)
add_executable(mesh_example examples/mesh/main.cpp)
target_link_libraries(mesh_example PRIVATE mesh_example_model)
add_executable(mesh_heavy_benchmark examples/mesh/heavy_benchmark.cpp)
target_link_libraries(mesh_heavy_benchmark PRIVATE mesh_example_model)

include(CTest)
add_library(method_example_model STATIC examples/methods/MethodTarget.cpp
            examples/methods/MethodCaller.cpp examples/methods/MethodBench.cpp)
target_link_libraries(method_example_model PUBLIC gfsim)
target_include_directories(method_example_model PUBLIC examples/methods)
add_executable(method_example examples/methods/main.cpp)
target_link_libraries(method_example PRIVATE method_example_model)

if(BUILD_TESTING)
    add_executable(delayed_consume_test tests/queue/test_delayed_consume.cpp)
    target_link_libraries(delayed_consume_test PRIVATE gfsim)
    add_test(NAME delayed_consume COMMAND delayed_consume_test)
    add_executable(queue_method_visibility_test tests/queue/test_queue_methods.cpp
                   tests/queue/QueueView.cpp tests/queue/QueueBench.cpp)
    target_link_libraries(queue_method_visibility_test PRIVATE gfsim)
    add_test(NAME queue_method_visibility COMMAND queue_method_visibility_test
             ${CMAKE_CURRENT_BINARY_DIR}/queue-methods.dot)
    set_tests_properties(queue_method_visibility PROPERTIES TIMEOUT 60)
    add_executable(bind_module_test tests/methods/test_bind_module.cpp
                   tests/methods/ReferenceNode.cpp)
    target_include_directories(bind_module_test PRIVATE src)
    target_link_libraries(bind_module_test PRIVATE gfsim)
    add_test(NAME bind_module COMMAND bind_module_test ${CMAKE_CURRENT_BINARY_DIR}/module-references.dot)
    add_executable(method_interaction_test tests/methods/test_methods.cpp)
    target_include_directories(method_interaction_test PRIVATE src)
    target_link_libraries(method_interaction_test PRIVATE method_example_model)
    add_test(NAME method_interaction COMMAND method_interaction_test ${CMAKE_CURRENT_BINARY_DIR}/methods.dot)
    add_test(NAME method_example COMMAND method_example 4 ${CMAKE_CURRENT_BINARY_DIR}/method-example.dot)
    set_tests_properties(bind_module method_interaction method_example PROPERTIES TIMEOUT 60)
    find_program(GFSIM_DOT dot)
    if(GFSIM_DOT)
        add_test(NAME queue_methods_graph_render COMMAND ${GFSIM_DOT} -Tsvg
                 ${CMAKE_CURRENT_BINARY_DIR}/queue-methods.dot
                 -o ${CMAKE_CURRENT_BINARY_DIR}/queue-methods.svg)
        set_tests_properties(queue_methods_graph_render PROPERTIES DEPENDS queue_method_visibility)
        add_test(NAME method_graph_render COMMAND ${GFSIM_DOT} -Tsvg
                 ${CMAKE_CURRENT_BINARY_DIR}/methods.dot -o ${CMAKE_CURRENT_BINARY_DIR}/methods.svg)
        set_tests_properties(method_graph_render PROPERTIES DEPENDS method_interaction)
        add_test(NAME module_references_render COMMAND ${GFSIM_DOT} -Tsvg
                 ${CMAKE_CURRENT_BINARY_DIR}/module-references.dot
                 -o ${CMAKE_CURRENT_BINARY_DIR}/module-references.svg)
        set_tests_properties(module_references_render PROPERTIES DEPENDS bind_module)
    endif()
    add_executable(parallel_subtree_test tests/parallel/test_parallel.cpp
                   tests/parallel/ProbeModule.cpp tests/parallel/RingStage.cpp)
    target_include_directories(parallel_subtree_test PRIVATE src)
    target_link_libraries(parallel_subtree_test PRIVATE gfsim)
    add_test(NAME parallel_subtree COMMAND parallel_subtree_test
             ${CMAKE_CURRENT_BINARY_DIR}/parallel-phases.json
             ${CMAKE_CURRENT_BINARY_DIR}/parallel-ring.json
             ${CMAKE_CURRENT_BINARY_DIR}/parallel-ring.dot)
    set_tests_properties(parallel_subtree PROPERTIES TIMEOUT 60)
    find_program(MESH_PYTHON3 python3)
    if(MESH_PYTHON3)
        add_test(NAME heavy_mesh_summary COMMAND ${MESH_PYTHON3}
                 ${CMAKE_CURRENT_SOURCE_DIR}/examples/mesh/summarize_heavy.py
                 ${CMAKE_CURRENT_BINARY_DIR}/heavy-mesh-smoke.csv)
        set_tests_properties(heavy_mesh_summary PROPERTIES DEPENDS heavy_mesh_benchmark)
        add_test(NAME parallel_trace_json COMMAND ${MESH_PYTHON3}
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/parallel/check_trace.py
                 ${CMAKE_CURRENT_BINARY_DIR}/parallel-phases.json
                 ${CMAKE_CURRENT_BINARY_DIR}/parallel-ring.json)
        set_tests_properties(parallel_trace_json PROPERTIES DEPENDS parallel_subtree)
        add_test(NAME mesh_codegen COMMAND ${MESH_PYTHON3}
                 ${CMAKE_CURRENT_SOURCE_DIR}/examples/mesh/generate_mesh.py --check)
    endif()
    add_executable(mesh_example_test tests/examples/test_mesh.cpp)
    target_link_libraries(mesh_example_test PRIVATE mesh_example_model)
    add_test(NAME mesh_example COMMAND mesh_example_test ${CMAKE_CURRENT_BINARY_DIR}/mesh.dot)

    add_executable(heavy_mesh_test tests/examples/test_heavy_mesh.cpp)
    target_link_libraries(heavy_mesh_test PRIVATE mesh_example_model)
    add_test(NAME heavy_mesh COMMAND heavy_mesh_test)
    add_test(NAME heavy_mesh_benchmark COMMAND mesh_heavy_benchmark
             --iterations 0,19 --workers 0,1,2,4 --batches 2 --repeats 1 --warmup 0
             --output ${CMAKE_CURRENT_BINARY_DIR}/heavy-mesh-smoke.csv)

    add_executable(counter_exchange_test tests/examples/test_counter_exchange.cpp)
    target_link_libraries(counter_exchange_test PRIVATE counter_example_model)
    add_test(NAME counter_exchange COMMAND counter_exchange_test)

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
    foreach(extension h cpp md)
        configure_file(templates/SimModule.${extension}.in
                       ${CMAKE_CURRENT_BINARY_DIR}/generated/${MODULE_NAME}.${extension} @ONLY)
    endforeach()
    add_library(template_model STATIC
                ${CMAKE_CURRENT_BINARY_DIR}/generated/TemplateSmoke.cpp)
    target_include_directories(template_model PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(template_model PUBLIC gfsim)
    add_executable(template_smoke_test
                   tests/templates/template_smoke.cpp)
    target_link_libraries(template_smoke_test PRIVATE template_model)
    add_test(NAME template_smoke COMMAND template_smoke_test)

    add_executable(simstate_test tests/state/test_state.cpp
                   tests/state/StateOwner.cpp tests/state/StateAccess.cpp)
    target_link_libraries(simstate_test PRIVATE gfsim)
    add_test(NAME simstate COMMAND simstate_test ${CMAKE_CURRENT_BINARY_DIR}/simstate.dot)
    set_tests_properties(simstate PROPERTIES TIMEOUT 60)
    add_executable(state_api_example tests/state/state_api_example.cpp)
    target_link_libraries(state_api_example PRIVATE gfsim)
    add_test(NAME state_api_example COMMAND state_api_example)

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
