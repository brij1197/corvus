find_package(hiredis CONFIG REQUIRED)

add_executable(corvus-unit-tests
  test_version.cpp
  test_health_handler.cpp
  test_router.cpp
  test_envelope.cpp
  test_jwt_validator.cpp
  test_api_key_validator.cpp
  test_rate_limiter.cpp
)

target_include_directories(corvus-unit-tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corvus-unit-tests PRIVATE
  corvus-gateway
  corvus-api
  corvus-auth
  GTest::gtest
  GTest::gtest_main
  GTest::gmock
  Drogon::Drogon
  hiredis::hiredis
)

target_compile_features(corvus-unit-tests PRIVATE cxx_std_20)

include(GoogleTest)

gtest_add_tests(
  TARGET      corvus-unit-tests
  SOURCES     test_version.cpp
              test_health_handler.cpp
              test_router.cpp
              test_envelope.cpp
              test_jwt_validator.cpp
              test_api_key_validator.cpp
)

