set(serverapp_SRCS
		StdInc.cpp
		EntryPoint.cpp
)

set(serverapp_HEADERS
		StdInc.h
)

assign_source_group(${serverapp_SRCS} ${serverapp_HEADERS})
add_executable(vcmiserver ${serverapp_SRCS} ${serverapp_HEADERS})
if(USING_CONAN)
	vcmi_copy_conan_runtime_deps(vcmiserver)
endif()

set(serverapp_LIBS vcmi)
if(CMAKE_SYSTEM_NAME MATCHES FreeBSD OR HAIKU)
	list(APPEND serverapp_LIBS execinfo)
endif()

if(ENABLE_ML)
	find_package(Python REQUIRED COMPONENTS Development)
	set(serverapp_LIBS mlclient mlserverplugin ${serverapp_LIBS})
	# Force Python lib at the end of link line (after libvcmi.so)
	# Using INTERFACE library to prevent CMake reordering
	add_library(python_fix INTERFACE)
	target_link_libraries(python_fix INTERFACE Python::Python)
endif()

# 09-10: 重复追加 vcmi 到行尾 — MinGW ld 从左到右解析, servercommon 新增的 lib 符号引用
# 需要 import lib 位于 vcmiservercommon.a 之后 (Windows 链接序修复)
target_link_libraries(vcmiserver PRIVATE ${serverapp_LIBS} minizip::minizip vcmiservercommon vcmi)

if(ENABLE_ML)
	# Force Python to end of link line to resolve PyGILState from libvcmi.so
	target_link_libraries(vcmiserver PRIVATE python_fix)
endif()

target_include_directories(vcmiserver
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
	set(VCMI_FILE_DESCRIPTION "VCMI Server")
	set(VCMI_ORIGINAL_FILENAME "VCMI_server.exe")
	set(VCMI_ICON_RESOURCE "")
	set(server_VERSIONINFO_RC "${CMAKE_CURRENT_BINARY_DIR}/VCMI_server_versioninfo.rc")
	configure_file("${CMAKE_SOURCE_DIR}/win/WindowsFileInfo.rc.in" "${server_VERSIONINFO_RC}" @ONLY)
	target_sources(vcmiserver PRIVATE "${server_VERSIONINFO_RC}")
	set_target_properties(vcmiserver
		PROPERTIES
			OUTPUT_NAME "VCMI_server"
			PROJECT_LABEL "VCMI_server"
	)
endif()

vcmi_set_output_dir(vcmiserver "")
enable_pch(vcmiserver)

install(TARGETS vcmiserver DESTINATION ${BIN_DIR})
