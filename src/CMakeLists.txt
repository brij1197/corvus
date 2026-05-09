add_library(corvus-gateway STATIC
  gateway/router.cpp
  gateway/health_handler.cpp
)

target_include_directories(corvus-gateway PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)
 
target_link_libraries(corvus-gateway PUBLIC
  Drogon::Drogon
)
 
target_compile_features(corvus-gateway PUBLIC cxx_std_20)

add_executable(corvus-core
  main.cpp
)

target_link_libraries(corvus-core PRIVATE
  corvus-gateway
)

target_compile_features(corvus-core PRIVATE cxx_std_20)
