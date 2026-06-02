# Supports vcpkg (CONFIG) and Debian/Ubuntu system packages (MODULE/pkg-config).

find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()

find_package(gRPC CONFIG QUIET)
if(NOT TARGET gRPC::grpc++)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GRPCPP REQUIRED grpc++)
  pkg_check_modules(GRPC REQUIRED grpc)

  find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)

  add_library(gRPC::grpc++ INTERFACE IMPORTED)
  target_include_directories(gRPC::grpc++ INTERFACE ${GRPCPP_INCLUDE_DIRS})
  target_link_libraries(gRPC::grpc++ INTERFACE ${GRPCPP_LIBRARIES})
  target_link_directories(gRPC::grpc++ INTERFACE ${GRPCPP_LIBRARY_DIRS})
endif()

if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
  if(TARGET gRPC::grpc_cpp_plugin)
    set(GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
  else()
    find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)
  endif()
endif()

if(NOT _PROTOBUF_PROTOC)
  if(TARGET protobuf::protoc)
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
  else()
    find_program(_PROTOBUF_PROTOC protoc REQUIRED)
  endif()
endif()

find_package(PostgreSQL REQUIRED)

find_package(flatbuffers CONFIG QUIET)
if(NOT TARGET flatbuffers::flatbuffers)
  find_path(FLATBUFFERS_INCLUDE_DIR NAMES flatbuffers/flexbuffers.h)
  find_library(FLATBUFFERS_LIBRARY NAMES flatbuffers)
  if(NOT FLATBUFFERS_INCLUDE_DIR OR NOT FLATBUFFERS_LIBRARY)
    message(FATAL_ERROR "flatbuffers development files not found")
  endif()
  add_library(flatbuffers::flatbuffers UNKNOWN IMPORTED)
  set_target_properties(flatbuffers::flatbuffers PROPERTIES
    IMPORTED_LOCATION "${FLATBUFFERS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FLATBUFFERS_INCLUDE_DIR}"
  )
endif()

find_package(GTest QUIET CONFIG)
if(NOT GTest_FOUND)
  find_package(GTest QUIET)
endif()
