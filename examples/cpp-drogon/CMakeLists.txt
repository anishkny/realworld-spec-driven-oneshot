cmake_minimum_required(VERSION 3.10)
project(realworld-api)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon CONFIG REQUIRED)

aux_source_directory(controllers CONTROLLER_SOURCES)
aux_source_directory(models MODEL_SOURCES)
aux_source_directory(utils UTIL_SOURCES)

add_executable(${PROJECT_NAME} 
    main.cc
    ${CONTROLLER_SOURCES}
    ${MODEL_SOURCES}
    ${UTIL_SOURCES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
