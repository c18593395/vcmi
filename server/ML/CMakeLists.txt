cmake_minimum_required(VERSION 3.18 FATAL_ERROR)

# Conan target is "sqlite3", linux apt target is "SQLite3"
find_package(sqlite3 CONFIG QUIET)

if(TARGET sqlite3::sqlite3)
    set(SQLITE3_TARGET sqlite3::sqlite3)
else()
    find_package(SQLite3 QUIET)
    if(SQLite3_FOUND)
        set(SQLITE3_TARGET SQLite::SQLite3)
    else()
        set(SQLITE3_TARGET "")
    endif()
endif()

add_library(mlserverplugin SHARED
  Config.h
  ServerPlugin.cpp
  ServerPlugin.h
  Stats.cpp
  Stats.h

  StdInc.h
)

add_dependencies(mlserverplugin vcmi)
if(SQLITE3_TARGET)
    target_link_libraries(mlserverplugin PRIVATE vcmi ${SQLITE3_TARGET})
else()
    target_link_libraries(mlserverplugin PRIVATE vcmi)
    target_compile_definitions(mlserverplugin PRIVATE WITHOUT_SQLITE3)
endif()
vcmi_set_output_dir(mlserverplugin "")
enable_pch(mlserverplugin)
