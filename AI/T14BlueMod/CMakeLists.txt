cmake_minimum_required(VERSION 3.18 FATAL_ERROR)

set(T14BlueMod_SRCS
	T14Nullkiller.cpp
	T14BlueGateway.cpp
)

set(T14BlueMod_HEADERS
	StdInc.h
	T14Nullkiller.h
	T14BlueGateway.h
)

assign_source_group(${T14BlueMod_SRCS} ${T14BlueMod_HEADERS})

add_library(T14BlueMod OBJECT ${T14BlueMod_SRCS} ${T14BlueMod_HEADERS})
target_compile_definitions(T14BlueMod PRIVATE VCMI_DLL=1)

target_include_directories(T14BlueMod PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(T14BlueMod PUBLIC vcmiMain TBB::tbb)

vcmi_set_output_dir(T14BlueMod "AI")
enable_pch(T14BlueMod)
