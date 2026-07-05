find_package(hiredis CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBPQXX REQUIRED libpqxx)


add_executable(corvus-unit-tests
  test_version.cpp
  test_health_handler.cpp
  test_router.cpp
  test_envelope.cpp
  test_jwt_validator.cpp
  test_api_key_validator.cpp
  test_rate_limiter.cpp
  test_request_size_limit.cpp
  test_request_id.cpp
  test_pg_pool.cpp
  test_migration_runner.cpp
  test_redis_connection.cpp
  test_cache_aside.cpp
  test_resource_model.cpp
)

target_include_directories(corvus-unit-tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corvus-unit-tests PRIVATE
  corvus-gateway
  corvus-api
  corvus-auth
  corvus-db
  GTest::gtest
  GTest::gtest_main
  GTest::gmock
  Drogon::Drogon
  hiredis::hiredis
  ${LIBPQXX_LIBRARIES}
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
              test_request_size_limit.cpp
              test_request_id.cpp
              test_pg_pool.cpp
              test_migration_runner.cpp
              test_redis_connection.cpp
              test_cache_aside.cpp
              test_resource_model.cpp
)

