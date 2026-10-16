# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
file(GLOB EMBSIM_UNIT_TESTS CONFIGURE_DEPENDS unit/test_*.c)
foreach(src ${EMBSIM_UNIT_TESTS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE embsim)
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/core ${CMAKE_SOURCE_DIR}/tests)
  target_compile_options(${name} PRIVATE ${EMBSIM_C_FLAGS})
  target_compile_definitions(${name} PRIVATE EMBSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME ${name} COMMAND ${name})
endforeach()

# every catalog item instantiates under its declared policies and passes its
# fixtures (SPEC-014 §3)
add_test(NAME catalog_fixtures COMMAND embsim_cli catalog validate ${CMAKE_SOURCE_DIR}/catalog)

# Python-based checks run under CTest when an interpreter is available: the
# tools' unit tests, documentation integrity, repository policy checks, and the
# protocol conformance suite over the stdio and WebSocket transports.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME tools_unittests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools -p "test_*.py"
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME catalog_sdk COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_sdk.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME importers COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_importers.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME docs_integrity COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/docs/check_docs.py)
  add_test(NAME banned_constructs COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/repository/check_banned.py)
  if(EXISTS ${CMAKE_SOURCE_DIR}/examples/button-led/project.json)
    add_test(NAME protocol_conformance COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/run_conformance.py
             --embsim $<TARGET_FILE:embsim_cli> --project ${CMAKE_SOURCE_DIR}/examples/button-led/project.json --port 18765
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME gdb_server COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_gdb.py
             --embsim $<TARGET_FILE:embsim_cli> --project ${CMAKE_SOURCE_DIR}/examples/bluepill-button-led/project.json
             --elf ${CMAKE_SOURCE_DIR}/examples/bluepill-button-led/firmware/button-led.elf
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME firmware_build COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_build.py
             --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    # two boards, two cores, one serial line, run twice to the same result (M4)
    add_test(NAME multiboard COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_multiboard.py
             --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME examples_headless COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ci/run_examples.py --build ${CMAKE_BINARY_DIR}
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
endif()
