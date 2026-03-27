add_executable(provider_unit_tests
    bus_executor_test.cpp
    ezo_i2c_bridge_test.cpp
    main.cpp
    framed_stdio_test.cpp
    handlers_test.cpp
    provider_config_test.cpp
)

target_include_directories(provider_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(provider_unit_tests PRIVATE
    anolis_provider_ezo_core
    GTest::gtest
)

anolis_provider_ezo_apply_warnings(provider_unit_tests)

if(WIN32)
    target_compile_definitions(provider_unit_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(GoogleTest)
gtest_discover_tests(provider_unit_tests
    TEST_PREFIX provider.unit.
    DISCOVERY_TIMEOUT 30
    PROPERTIES
        LABELS "unit"
        TIMEOUT 30
)
