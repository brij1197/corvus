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
)

target_include_directories(corvus-gateway PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corvus-gateway PUBLIC
  corvus-api
  Drogon::Drogon
)

target_compile_features(corvus-gateway PUBLIC cxx_std_20)

find_package(jwt-cpp CONFIG REQUIRED)

add_library(corvus-auth STATIC
  auth/jwt_validator.cpp
  auth/middleware.cpp
)

target_include_directories(corvus-auth PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corvus-auth PUBLIC
  corvus-api
  Drogon::Drogon  
  jwt-cpp::jwt-cpp
)

target_compile_features(corvus-auth PUBLIC cxx_std_20)

add_executable(corvus-core
  main.cpp
)

target_link_libraries(corvus-core PRIVATE
  corvus-gateway
  corvus-auth
)

target_compile_features(corvus-core PRIVATE cxx_std_20)
