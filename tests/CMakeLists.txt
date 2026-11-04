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
  add_test(NAME esp32_image COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_esp32_image.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME electrical_feedback COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_electrical_feedback.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME tools_unittests COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools -p "test_*.py"
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME catalog_sdk COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_sdk.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME importers COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_importers.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME record_replay COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_replay.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # serial.data: what a serial probe receives, streamed to the client (SPEC-008 §6)
  add_test(NAME serial_stream COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_serial_stream.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME organization COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_organization.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the bench's layout and a breadboard's strips (SPEC-007 §2, SPEC-005 §8.1)
  add_test(NAME bench COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_bench.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the world: fields sensors read, bodies, set_world (ADR-026)
  add_test(NAME pir COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_pir.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME logic_analyzer COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_logic_analyzer.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME power_profiler COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_power_profiler.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME oscilloscope COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_oscilloscope.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME world COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_world.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # a board's serial port on the host, paced (ADR-031, ADR-032)
  add_test(NAME host_serial COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_host_serial.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the Pico programmed with BOOTSEL and a UF2 file on its USB drive (ADR-031)
  add_test(NAME uf2 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_uf2.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the NUCLEO's ST-LINK through stlink-server's protocol, and OpenOCD programming a blank board
  add_test(NAME stlink COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_stlink.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the ST-LINK's own USB device: a .bin copied onto its NODE_F446RE drive programs the board
  add_test(NAME stlink_drive COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_stlink_drive.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the bench's USB devices on the computer's own USB stack, through USB/IP
  add_test(NAME usbip COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_usbip.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # every diagnostic format the engine raises has its French (core/util/diag_fr.c)
  add_test(NAME diagnostics_fr COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/i18n/diagnostics.py
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the workbench's copy of the catalog's taxonomy is current
  add_test(NAME taxonomy_ui COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/catalog/gen_taxonomy.py --check
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the part families' catalog entries are what their families generate
  add_test(NAME catalog_families COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/catalog/families.py check
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # every catalog entry's text in French, made from its English as it now stands
  add_test(NAME catalog_fr COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/i18n/catalog_text.py check
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # a Raspberry Pi Debug Probe on a Pico: CMSIS-DAP v2 over USB/IP on the RP2040's multi-drop wire
  add_test(NAME debug_probe COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_debug_probe.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # embsim ide's VS Code launch configurations, carried out as VS Code carries them out
  add_test(NAME ide COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_ide.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the fault mode and damage (ADR-034)
  add_test(NAME faults COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_faults.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME analysis COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_analysis.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME waveform_measurements COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_waveform_measurements.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME waveform_streaming COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_waveform_streaming.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME run_target COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_target.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # real parts' captures (sigrok's) replayed into their models (tests/captures)
  add_test(NAME real_captures COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify/replay_captures.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # stale images, register dumps, the circuit profile, export svg, plan
  add_test(NAME diagnose COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_diagnose.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the Cortex-M4's DSP instructions against QEMU's (tests/firmware/dsp)
  add_test(NAME dsp_differential COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_dsp.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the Cortex-M4F's MPU, faults, privilege and FP stacking against QEMU's (tests/firmware/cm4-core)
  add_test(NAME cm4_core_differential COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_cm4_core.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the Python client (python/embsim) against this build
  add_test(NAME python_client COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/python/tests/test_session.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # the MCP server (python/embsim/mcp.py, ADR-035): an assistant's session against this build
  add_test(NAME mcp_server COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/python/tests/test_mcp.py
           --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME docs_integrity COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/docs/check_docs.py)
  add_test(NAME coverage_tables COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/catalog/coverage.py --check)
  add_test(NAME banned_constructs COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/repository/check_banned.py)
  # four shards, so CTest runs the catalog's thousands of fixtures side by
  # side and no one test nears its timeout as the catalog grows
  foreach(shard RANGE 3)
    add_test(NAME catalog_fixture_assertions_${shard} COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/catalog/run_fixtures.py
             --embsim $<TARGET_FILE:embsim_cli> --shard ${shard}/4 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endforeach()
  if(EXISTS ${CMAKE_SOURCE_DIR}/examples/button-led/project.json)
    add_test(NAME http_assets COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/protocol/test_http_assets.py
             --embsim $<TARGET_FILE:embsim_cli> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
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
