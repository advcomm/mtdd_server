cmake_minimum_required(VERSION 3.20)
project(mtdd_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MTDD_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/mtdd.proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_OUT_DIR}")

if(NOT _PROTOBUF_PROTOC)
  if(TARGET protobuf::protoc)
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
  else()
    set(_PROTOBUF_PROTOC "${_PROTOBUF_PROTOC}")
  endif()
endif()
if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
  if(TARGET gRPC::grpc_cpp_plugin)
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
  else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE "${GRPC_CPP_PLUGIN_EXECUTABLE}")
  endif()
else()
  set(_GRPC_CPP_PLUGIN_EXECUTABLE "${GRPC_CPP_PLUGIN_EXECUTABLE}")
endif()

set(PROTO_SRCS "${PROTO_OUT_DIR}/mtdd.pb.cc")
set(PROTO_HDRS "${PROTO_OUT_DIR}/mtdd.pb.h")
set(GRPC_SRCS "${PROTO_OUT_DIR}/mtdd.grpc.pb.cc")
set(GRPC_HDRS "${PROTO_OUT_DIR}/mtdd.grpc.pb.h")

add_custom_command(
  OUTPUT "${PROTO_SRCS}" "${PROTO_HDRS}" "${GRPC_SRCS}" "${GRPC_HDRS}"
  COMMAND ${_PROTOBUF_PROTOC}
  ARGS --grpc_out "${PROTO_OUT_DIR}"
       --cpp_out "${PROTO_OUT_DIR}"
       -I "${CMAKE_CURRENT_SOURCE_DIR}/proto"
       --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
       "${PROTO_FILE}"
  DEPENDS "${PROTO_FILE}"
  COMMENT "Generating gRPC sources from mtdd.proto"
)

add_library(mtdd_proto STATIC
  "${PROTO_SRCS}"
  "${GRPC_SRCS}"
)
target_include_directories(mtdd_proto PUBLIC "${PROTO_OUT_DIR}")
target_link_libraries(mtdd_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)
if(TARGET gRPC::grpc++_reflection)
  target_link_libraries(mtdd_proto PUBLIC gRPC::grpc++_reflection)
endif()

set(MTDD_SERVER_SOURCES
  src/main.cpp
  src/config.cpp
  src/codec/flex_meta.cpp
  src/codec/arrow_ipc_encoder.cpp
  src/pg/pq_conn.cpp
  src/pg/connection_manager.cpp
  src/pg/session_store.cpp
  src/pg/query_executor.cpp
  src/service/mtdd_shard_service.cpp
)

add_executable(mtdd_server ${MTDD_SERVER_SOURCES})
target_include_directories(mtdd_server PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${PROTO_OUT_DIR}"
)
if(TARGET Arrow::arrow_shared)
  set(MTDD_ARROW_LIB Arrow::arrow_shared)
elseif(TARGET Arrow::arrow_static)
  set(MTDD_ARROW_LIB Arrow::arrow_static)
elseif(TARGET Arrow::arrow)
  set(MTDD_ARROW_LIB Arrow::arrow)
else()
  message(FATAL_ERROR "Arrow library target not found")
endif()

if(TARGET PostgreSQL::PostgreSQL)
  set(MTDD_LIBPQ_LIB PostgreSQL::PostgreSQL)
elseif(TARGET unofficial::libpq::libpq)
  set(MTDD_LIBPQ_LIB unofficial::libpq::libpq)
else()
  message(FATAL_ERROR "libpq target not found")
endif()

target_link_libraries(mtdd_server PRIVATE
  mtdd_proto
  ${MTDD_LIBPQ_LIB}
  ${MTDD_ARROW_LIB}
  flatbuffers::flatbuffers
)

if(WIN32)
  target_compile_definitions(mtdd_server PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

install(TARGETS mtdd_server RUNTIME DESTINATION bin)
install(FILES deploy/nginx/mtdd-grpc.conf DESTINATION share/mtdd_server/nginx)
install(FILES deploy/systemd/mtdd-server.service DESTINATION share/mtdd_server/systemd)

if(MTDD_BUILD_TESTS AND (GTest_FOUND OR TARGET GTest::gtest))
  enable_testing()
  add_subdirectory(test)
endif()
