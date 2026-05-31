add_library(corvus-api STATIC
  api/envelope.cpp
  api/response.cpp
  api/request_id.cpp
)

target_include_directories(corvus-api PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corvus-api PUBLIC
  Drogon::Drogon
)

target_compile_features(corvus-api PUBLIC cxx_std_20)

add_library(corvus-gateway STATIC
  gateway/router.cpp
  gateway/health_handler.cpp
  gateway/not_found_handler.cpp
  gateway/rate_limiter.cpp
  gateway/request_size_limit.cpp
  gateway/request_id_middleware.cpp
)

target_include_directories(corvus-gateway PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corvus-gateway PUBLIC
  corvus-api
  Drogon::Drogon
)

target_compile_features(corvus-gateway PUBLIC cxx_std_20)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED libpqxx)

add_library(corvus-db STATIC
  db/pg_pool.cpp
)

target_include_directories(corvus-db PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${LIBPQXX_INCLUDE_DIRS}
)

target_link_libraries(corvus-db PUBLIC
  ${LIBPQXX_LIBRARIES}
)

target_compile_features(corvus-db PUBLIC cxx_std_20)

find_package(jwt-cpp CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(corvus-auth STATIC
  auth/jwt_validator.cpp
  auth/middleware.cpp
  auth/api_key_validator.cpp
  auth/api_key_middleware.cpp
)

target_include_directories(corvus-auth PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corvus-auth PUBLIC
  corvus-api
  Drogon::Drogon  
  jwt-cpp::jwt-cpp
  hiredis::hiredis
  OpenSSL::Crypto
)

target_compile_features(corvus-auth PUBLIC cxx_std_20)

add_executable(corvus-core
  main.cpp
)

target_link_libraries(corvus-core PRIVATE
  corvus-gateway
  corvus-auth
  corvus-db
)

target_compile_features(corvus-core PRIVATE cxx_std_20)
