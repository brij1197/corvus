enable_testing()

add_executable(corvus-unit-tests
  test_version.cpp
  test_health_handler.cpp
)

target_include_directories(corvus-unit-tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corvus-unit-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  Drogon::Drogon
)

target_compile_features(corvus-unit-tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(corvus-unit-tests)
