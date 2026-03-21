cmake_minimum_required(VERSION 3.21)

project(StatusServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STATUSSERVER_COPY_RUNTIME_FILES "Copy config.ini and MySQL runtime DLLs after build on Windows" ON)

set(MYSQL_CONNECTOR_ROOT "" CACHE PATH "MySQL Connector/C++ root directory")
set(MYSQL_CONNECTOR_INCLUDE_DIR "" CACHE PATH "MySQL Connector/C++ include directory")
set(MYSQL_CONNECTOR_LIBRARY_DIR "" CACHE PATH "MySQL Connector/C++ library directory")

if(MYSQL_CONNECTOR_ROOT)
    if(NOT MYSQL_CONNECTOR_INCLUDE_DIR)
        set(MYSQL_CONNECTOR_INCLUDE_DIR "${MYSQL_CONNECTOR_ROOT}/include")
    endif()
    if(NOT MYSQL_CONNECTOR_LIBRARY_DIR)
        if(WIN32)
            set(MYSQL_CONNECTOR_LIBRARY_DIR "${MYSQL_CONNECTOR_ROOT}/lib64/debug/vs14")
        else()
            if(EXISTS "${MYSQL_CONNECTOR_ROOT}/lib64")
                set(MYSQL_CONNECTOR_LIBRARY_DIR "${MYSQL_CONNECTOR_ROOT}/lib64")
            else()
                set(MYSQL_CONNECTOR_LIBRARY_DIR "${MYSQL_CONNECTOR_ROOT}/lib")
            endif()
        endif()
    endif()
endif()

find_package(Boost REQUIRED COMPONENTS filesystem system)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(Threads REQUIRED)

if(MYSQL_CONNECTOR_INCLUDE_DIR)
    set(_mysql_include_hints "${MYSQL_CONNECTOR_INCLUDE_DIR}")
else()
    set(_mysql_include_hints)
endif()

if(MYSQL_CONNECTOR_LIBRARY_DIR)
    set(_mysql_lib_hints "${MYSQL_CONNECTOR_LIBRARY_DIR}")
else()
    set(_mysql_lib_hints)
endif()

find_path(MYSQLCPPCONN_INCLUDE_PATH
    NAMES jdbc/mysql_driver.h
    HINTS ${_mysql_include_hints}
)

find_library(MYSQLCPPCONN_LIBRARY
    NAMES mysqlcppconn
    HINTS ${_mysql_lib_hints}
)

find_library(MYSQLCPPCONNX_LIBRARY
    NAMES mysqlcppconnx
    HINTS ${_mysql_lib_hints}
)

if(NOT MYSQLCPPCONN_INCLUDE_PATH)
    message(FATAL_ERROR "未找到 MySQL Connector/C++ 头文件。请设置 MYSQL_CONNECTOR_ROOT 或 MYSQL_CONNECTOR_INCLUDE_DIR。")
endif()

if(NOT MYSQLCPPCONN_LIBRARY)
    message(FATAL_ERROR "未找到 mysqlcppconn 库。请设置 MYSQL_CONNECTOR_ROOT 或 MYSQL_CONNECTOR_LIBRARY_DIR。")
endif()

set(STATUSSERVER_SOURCES
    AsioIOServicePool.cpp
    ConfigManager.cpp
    message.grpc.pb.cc
    message.pb.cc
    MySqlDao.cpp
    MySqlMgr.cpp
    RedisConPool.cpp
    RedisManager.cpp
    StatusServer.cpp
    StatusServiceImpl.cpp
)

set(STATUSSERVER_HEADERS
    AsioIOServicePool.h
    ConfigManager.h
    Defer.h
    global.h
    message.grpc.pb.h
    message.pb.h
    MySqlDao.h
    MySqlMgr.h
    RedisConPool.h
    RedisManager.h
    Singleton.h
    StatusServiceImpl.h
)

add_executable(StatusServer
    ${STATUSSERVER_SOURCES}
    ${STATUSSERVER_HEADERS}
)

target_include_directories(StatusServer PRIVATE
    ${MYSQLCPPCONN_INCLUDE_PATH}
)

target_link_libraries(StatusServer PRIVATE
    Boost::filesystem
    Boost::system
    protobuf::libprotobuf
    gRPC::grpc++
    jsoncpp_lib
    hiredis::hiredis
    Threads::Threads
    ${MYSQLCPPCONN_LIBRARY}
)

if(MYSQLCPPCONNX_LIBRARY)
    target_link_libraries(StatusServer PRIVATE ${MYSQLCPPCONNX_LIBRARY})
endif()

if(WIN32)
    target_compile_definitions(StatusServer PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(StatusServer PRIVATE ws2_32)
endif()

if(WIN32 AND STATUSSERVER_COPY_RUNTIME_FILES)
    add_custom_command(TARGET StatusServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
                "$<TARGET_FILE_DIR:StatusServer>/config.ini"
        COMMENT "Copying config.ini"
    )

    file(GLOB MYSQL_RUNTIME_DLLS
        "${CMAKE_CURRENT_SOURCE_DIR}/mysqlcppconn*.dll"
    )

    foreach(mysql_runtime_dll IN LISTS MYSQL_RUNTIME_DLLS)
        add_custom_command(TARGET StatusServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${mysql_runtime_dll}"
                    "$<TARGET_FILE_DIR:StatusServer>/"
            COMMENT "Copying ${mysql_runtime_dll}"
        )
    endforeach()
endif()
