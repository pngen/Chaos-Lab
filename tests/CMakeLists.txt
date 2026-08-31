
# Dependency-free deterministic test harness. No timeouts / watchdogs.
set(CHAOS_UNIT_TESTS
  test_identities
  test_campaign
  test_scheduler
  test_evidence
  test_authority
  test_fuzz
  test_assertions
  test_safety
  test_persistence
  test_resource
  test_replay
  test_digest
)

foreach(t ${CHAOS_UNIT_TESTS})
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE chaoslab chaoslab_os)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Integration tests (need tcp/process/transport + built target binaries).
set(CHAOS_INTEGRATION_TESTS test_transport test_process)
foreach(t ${CHAOS_INTEGRATION_TESTS})
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE chaoslab chaoslab_os)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Real multiprocess closure proof (spawns the built chaos.exe and targets).
if(CHAOSLAB_BUILD_CLI AND CHAOSLAB_BUILD_TARGETS)
  add_executable(test_multiprocess test_multiprocess.cpp)
  target_link_libraries(test_multiprocess PRIVATE chaoslab chaoslab_os)
  add_test(NAME test_multiprocess COMMAND test_multiprocess)
  set_tests_properties(test_multiprocess PROPERTIES
    ENVIRONMENT "CHAOSLAB_BIN=$<TARGET_FILE:chaos_cli>"
    WORKING_DIRECTORY $<TARGET_FILE_DIR:chaos_cli>)
endif()
