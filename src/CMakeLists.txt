set(GATEWAY_SOURCES
  gateway/router.cpp
  gateway/health_handler.cpp
)

add_executable(corvus-core
  main.cpp
  ${GATEWAY_SOURCES}
)

target_include_directories(corvus-core PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(corvus-core PRIVATE
  Drogon::Drogon
)

target_compile_features(corvus-core PRIVATE cxx_std_20)
