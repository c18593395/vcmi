# Minimum required version greatly affect CMake behavior
# So cmake_minimum_required must be called before the project()
# 3.16.0 is used since it's used by our currently oldest supported system: Ubuntu-20.04
cmake_minimum_required(VERSION 3.16.0)

project(VCMI)
# TODO
# - Cleanup remove_directory copy_directory if performance will be a problem.
# We can use some macro over copy_if_different since it's only work for single file.
# - Find a way to move add_custom_command for assets deploy out of "lib/CMakeLists.txt"
# - Consider to remove M_DATA_DIR, DM_BIN_DIR, DM_LIB_DIR and not use them in code as well
# - Try to get rid of FOLDER override with define_property
# It's used currently to make sure that 3rd-party dependencies in git submodules get proper FOLDER property

if(APPLE AND NOT IOS)
	set(MACOS 1)
endif()

if(POLICY CMP0142)
  cmake_policy(SET CMP0142 NEW)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
if(POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()

############################################
#        User-provided options             #
############################################

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
		"Choose the type of build, options are: Debug Release RelWithDebInfo. Default is RelWithDebInfo."
		FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

# Platform-independent options

option(ENABLE_CLIENT "Enable compilation of game client" ON)
option(ENABLE_ERM "Enable compilation of ERM scripting module" OFF)
option(ENABLE_LUA "Enable compilation of LUA scripting module" OFF)
option(ENABLE_VIDEO "Enable video support using ffmpeg" OFF)
option(ENABLE_TRANSLATIONS "Enable generation of translations for launcher and editor" ON)
option(ENABLE_MINIMAL_LIB "Build only core parts of vcmi library that are required for game lobby" OFF)
option(ENABLE_GOLDMASTER "Build in public release mode in which some debug routines are disabled" OFF)

# AI libraries. Empty AI is always on

option(ENABLE_NULLKILLER2_AI "Enable compilation of Nullkiller2AI library" ON)
option(ENABLE_STUPID_AI "Enable compilation of StupidAI library" ON)
option(ENABLE_BATTLE_AI "Enable compilation of BattleAI library" ON)
option(ENABLE_MMAI "Enable compilation of MMAI AI library" ON)

# Compilation options

option(ENABLE_PCH "Enable compilation using precompiled headers" ON)
option(ENABLE_DEBUG_CONSOLE "Enable debug console for Windows builds" ON)
option(ENABLE_STRICT_COMPILATION "Treat all compiler warnings as errors" OFF)
option(ENABLE_MULTI_PROCESS_BUILDS "Enable /MP flag for MSVS solution" ON)
option(ENABLE_COLORIZED_COMPILER_OUTPUT "Colorize compilation output (Clang/GNU)." ON)
option(ENABLE_CCACHE "Speed up recompilation by caching previous compilations" OFF)

# Platform-specific options

if(ANDROID)
	set(ANDROID_TARGET_SDK_VERSION "35" CACHE STRING "Android target SDK version")
	set(ANDROIDDEPLOYQT_OPTIONS "" CACHE STRING "Additional androiddeployqt options separated by semi-colon")
	set(ANDROID_GRADLE_PROPERTIES "" CACHE STRING "Additional Gradle properties separated by semi-colon")

	set(ENABLE_STATIC_LIBS ON)
	set(ENABLE_LAUNCHER ON)
else()
	option(ENABLE_STATIC_LIBS "Build library and all components such as AI statically" OFF)
	option(ENABLE_LAUNCHER "Enable compilation of launcher" ON)
endif()

if(IOS)
	set(BUNDLE_IDENTIFIER_PREFIX "" CACHE STRING "Bundle identifier prefix")
	set(APP_DISPLAY_NAME "VCMI" CACHE STRING "App name on the home screen")
endif()

if(IOS OR ANDROID)
	set(ENABLE_MONOLITHIC_INSTALL OFF)
	set(ENABLE_SINGLE_APP_BUILD ON)
	set(ENABLE_EDITOR OFF)
	set(ENABLE_TEST OFF)
	set(ENABLE_LOBBY OFF)
	set(ENABLE_SERVER OFF)
	set(COPY_CONFIG_ON_BUILD OFF)
	set(ENABLE_ML OFF)
	set(ENABLE_DISCORD OFF)
else()
	option(ENABLE_MONOLITHIC_INSTALL "Install everything in single directory on Linux and Mac" OFF) # Used for Snap packages and also useful for debugging
	option(COPY_CONFIG_ON_BUILD "Copies config folder into output directory at building phase" ON)
	option(ENABLE_SERVER "Enable compilation of dedicated server" ON)
	option(ENABLE_EDITOR "Enable compilation of map editor" ON)
	option(ENABLE_SINGLE_APP_BUILD "Builds client and launcher as single executable" OFF)
	option(ENABLE_TEST "Enable compilation of unit tests" OFF)
	option(ENABLE_LOBBY "Enable compilation of lobby server" OFF)
	option(ENABLE_ML "Enable compilation of extensions for MMAI training" ON)

	include(CheckCXXCompilerFlag)
	if(MSVC)
		check_cxx_compiler_flag("/std:c++23" COMPILER_SUPPORTS_CXX23)
		if(NOT COMPILER_SUPPORTS_CXX23)
			check_cxx_compiler_flag("/std:c++23preview" COMPILER_SUPPORTS_CXX23_PREVIEW)
			set(COMPILER_SUPPORTS_CXX23 ${COMPILER_SUPPORTS_CXX23_PREVIEW})
		endif()
	else()
		check_cxx_compiler_flag("-std=c++23" COMPILER_SUPPORTS_CXX23)
	endif()

	option(ENABLE_DISCORD "Enable compilation of discord integration" ON)

	# Determine if Glaze can be used
	set(DISCORD_RPC_NO_GLAZE OFF)
	if(
		(MACOS AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "10.15")
		OR
		(IOS AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "13.0")
		OR
		(NOT COMPILER_SUPPORTS_CXX23)
	)
		set(DISCORD_RPC_NO_GLAZE ON)
	endif()
endif()

# ERM depends on LUA implicitly
if(ENABLE_ERM AND NOT ENABLE_LUA)
	set(ENABLE_LUA ON)
endif()

include(CMakeDependentOption)
cmake_dependent_option(ENABLE_INNOEXTRACT "Enable innoextract for GOG file extraction in launcher" ON "ENABLE_LAUNCHER" OFF)
cmake_dependent_option(ENABLE_GITVERSION "Enable Version.cpp with Git commit hash" ON "NOT ENABLE_GOLDMASTER" OFF)
cmake_dependent_option(ENABLE_TEMPLATE_EDITOR "Enable template editor inside map editor" ON "ENABLE_EDITOR" OFF)

option(VCMI_PORTMASTER "PortMaster build" OFF)

############################################
#        Miscellaneous options             #
############################################

if (ENABLE_STATIC_LIBS AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(ENABLE_COLORIZED_COMPILER_OUTPUT)
	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		add_compile_options(-fcolor-diagnostics)
	elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		add_compile_options(-fdiagnostics-color=always)
	endif()
endif()

# Prefer sccache on Windows/MSVC; fall back to ccache elsewhere
if(ENABLE_CCACHE)
    if(MSVC)
        find_program(SCCACHE sccache)
        if(SCCACHE)
            message(STATUS "Enabling compiler launcher: sccache")
            set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE}")
            set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE}")
        else()
            # Optional fallback (only if ccache is present)
            find_program(CCACHE ccache REQUIRED)
            if(CCACHE)
                message(STATUS "Enabling compiler launcher: ccache (fallback)")
                set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE}")
                set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE}")
            endif()
        endif()
    else()
        find_program(CCACHE ccache REQUIRED)
        if(CCACHE)
            message(STATUS "Enabling compiler launcher: ccache")
            set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE}")
            set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE}")
        endif()
    endif()
endif()

if(ENABLE_CCACHE AND XCODE)
	set(compilerLauncherPermissions FILE_PERMISSIONS
		OWNER_READ OWNER_WRITE OWNER_EXECUTE
		GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE
	)
	set(compilerLauncherTemplate "xcode/compiler-launcher.in")
	set(compilerLauncherC "${CMAKE_BINARY_DIR}/xcode/launch-c")
	set(compilerLauncherCXX "${CMAKE_BINARY_DIR}/xcode/launch-cxx")

	set(xcodeCompiler "clang")
	configure_file(${compilerLauncherTemplate} ${compilerLauncherC} ${compilerLauncherPermissions})

	set(xcodeCompiler "clang++")
	configure_file(${compilerLauncherTemplate} ${compilerLauncherCXX} ${compilerLauncherPermissions})

	if(XCODE_VERSION VERSION_GREATER_EQUAL 26)
		set(compilerLauncherXcode26 "${CMAKE_BINARY_DIR}/xcode/launch-xc26")

		set(xcodeCompiler)
		configure_file(${compilerLauncherTemplate} ${compilerLauncherXcode26} ${compilerLauncherPermissions})

		set(CMAKE_XCODE_ATTRIBUTE_C_COMPILER_LAUNCHER   ${compilerLauncherXcode26})
		set(CMAKE_XCODE_ATTRIBUTE_CXX_COMPILER_LAUNCHER ${compilerLauncherXcode26})
	else()
		set(CMAKE_XCODE_ATTRIBUTE_CC  ${compilerLauncherC})
		set(CMAKE_XCODE_ATTRIBUTE_CXX ${compilerLauncherCXX})
	endif()
	set(CMAKE_XCODE_ATTRIBUTE_LD         ${compilerLauncherC})
	set(CMAKE_XCODE_ATTRIBUTE_LDPLUSPLUS ${compilerLauncherCXX})
endif()

# Allow to pass package name from Travis CI
set(PACKAGE_NAME_SUFFIX "" CACHE STRING "Suffix for CPack package name")
set(PACKAGE_FILE_NAME "" CACHE STRING "Override for CPack package filename")

set(CMAKE_MODULE_PATH ${CMAKE_HOME_DIRECTORY}/cmake_modules ${PROJECT_SOURCE_DIR}/CI)
# Contains custom functions and macros, but don't altering any options

include(VCMIUtils)
include(VersionDefinition)

vcmi_print_important_variables()

# Options to enable folders in CMake generated projects for Visual Studio, Xcode, etc
# Very useful to put 3rd-party libraries such as GoogleTest in their own folders
set_property(GLOBAL PROPERTY USE_FOLDERS TRUE)
# Make FOLDER property inheritable
# So when we set FOLDER property on AI directory all and targets inside will inherit it
#
# Important! This trick depend on undefined behavior since we override CMake own property.
# In same time define_property documentation states it's function for custom properties.
define_property(
	TARGET
	PROPERTY FOLDER
	INHERITED
	BRIEF_DOCS "Set the folder name."
	FULL_DOCS  "Use to organize targets in an IDE."
)

# Generate Version.cpp
if(ENABLE_GITVERSION)
	add_custom_target(update_version ALL
		BYPRODUCTS "Version.cpp"
		COMMAND ${CMAKE_COMMAND} -DGIT_SHA1="${GIT_SHA1}" -P "${PROJECT_SOURCE_DIR}/cmake_modules/Version.cmake"
	)
else()
	add_definitions(-DVCMI_NO_EXTRA_VERSION)
endif(ENABLE_GITVERSION)

if(ENABLE_PCH)
	macro(enable_pch name)
		target_precompile_headers(${name} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:<StdInc.h$<ANGLE-R>>)
	endmacro(enable_pch)
else()
	macro(enable_pch ignore)
	endmacro(enable_pch)
endif()

############################################
#        Documentation section             #
############################################

include(UseDoxygen OPTIONAL)

############################################
#        Compile and linking options       #
############################################

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

#General visibility options
set(CMAKE_CXX_VISIBILITY_PRESET default)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 0)

#Global fallback mapping
# RelWithDebInfo falls back to Release, then MinSizeRel, and then to None (tbb in 22.04 requires it)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel None "")
# MinSizeRel falls back to Release, then RelWithDebInfo, and then to None (tbb in 22.04 requires it)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel Release RelWithDebInfo None "")
# Release falls back to RelWithDebInfo, then MinSizeRel, and then to None (tbb in 22.04 requires it)
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release RelWithDebInfo MinSizeRel None "")

set(CMAKE_XCODE_ATTRIBUTE_APP_DISPLAY_NAME ${APP_DISPLAY_NAME})
set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=Debug] dwarf)
set(CMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE NO)
set(CMAKE_XCODE_ATTRIBUTE_ENABLE_NS_ASSERTIONS NO)
set(CMAKE_XCODE_ATTRIBUTE_ENABLE_NS_ASSERTIONS[variant=Debug] YES)
set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_64_TO_32_BIT_CONVERSION NO)
set(CMAKE_XCODE_ATTRIBUTE_MARKETING_VERSION ${APP_SHORT_VERSION})
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO)
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] YES)

#Check for endian
if(${CMAKE_VERSION} VERSION_LESS "3.20.0") 
	include(TestBigEndian)
	test_big_endian(VCMI_ENDIAN_BIG)
	if(VCMI_ENDIAN_BIG)
		add_definitions(-DVCMI_ENDIAN_BIG)
	endif()
elseif(${CMAKE_CXX_BYTE_ORDER} EQUAL "BIG_ENDIAN")
	add_definitions(-DVCMI_ENDIAN_BIG)
endif()

if(ENABLE_LAUNCHER)
	add_definitions(-DENABLE_LAUNCHER)
endif()

if(ENABLE_EDITOR)
	add_definitions(-DENABLE_EDITOR)
endif()

if(ENABLE_TEMPLATE_EDITOR)
	add_definitions(-DENABLE_TEMPLATE_EDITOR)
endif()

if(ENABLE_SINGLE_APP_BUILD)
	add_definitions(-DENABLE_SINGLE_APP_BUILD)
endif()

if(ENABLE_MINIMAL_LIB)
	add_definitions(-DENABLE_MINIMAL_LIB)
endif()

if(ENABLE_GOLDMASTER)
	add_definitions(-DENABLE_GOLDMASTER)
endif()

if(ENABLE_ML)
	add_definitions(-DENABLE_ML)
	set(ENABLE_MMAI ON)
endif()

if(ENABLE_NULLKILLER2_AI)
	add_definitions(-DENABLE_NULLKILLER2_AI)
endif()
if(ENABLE_STUPID_AI)
	add_definitions(-DENABLE_STUPID_AI)
endif()
if(ENABLE_BATTLE_AI)
	add_definitions(-DENABLE_BATTLE_AI)
endif()
if(ENABLE_MMAI)
	add_definitions(-DENABLE_MMAI)
endif()


if(IOS)
	set(CMAKE_MACOSX_RPATH 1)

	if(NOT USING_CONAN)
		list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_PREFIX_PATH}") # required for Boost
		set(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH FALSE)
		set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
	endif()

	set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
	set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED_FOR_APPS YES)
	set(CMAKE_XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${BUNDLE_IDENTIFIER_PREFIX}.$(PRODUCT_NAME)")
	set(CMAKE_XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
endif()

if(MACOS)
	# Not supported by standard library of our current minimal target - macOS 10.14 or newer required
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-aligned-allocation")
endif()

if(MINGW OR MSVC)
	#delete lib prefix for dlls (libvcmi -> vcmi)
	set(CMAKE_SHARED_LIBRARY_PREFIX "")

	if(MSVC)
		# Use ccache with MSVC + Visual Studio generator by shimming cl.exe.
		if(ENABLE_CCACHE AND (CMAKE_GENERATOR MATCHES "Visual Studio"))
			# Wrapper only for VS generator
			if(CCACHE)
				set(_shim "${CMAKE_BINARY_DIR}/cl.exe")
				# Prefer a symlink; if it fails (e.g. Windows without Developer Mode),
				# CMake will fallback to copying the file.
				file(CREATE_LINK "${CCACHE}" "${_shim}" SYMBOLIC COPY_ON_ERROR)

				set(CMAKE_VS_GLOBALS
					"CLToolExe=cl.exe"
					"CLToolPath=${CMAKE_BINARY_DIR}"
					"TrackFileAccess=false"
					"UseMultiToolTask=true"
				)
			endif()
		endif()

		add_definitions(-DBOOST_ALL_NO_LIB)
		add_definitions(-DBOOST_ALL_DYN_LINK)
		set(Boost_USE_STATIC_LIBS OFF)

		# Don't link with SDLMain
		if(ENABLE_DEBUG_CONSOLE)
			set(SDL2_BUILDING_LIBRARY ON)
			add_definitions(-DVCMI_WITH_DEBUG_CONSOLE)
		endif()

		# Suppress warnings
		add_definitions(-D_CRT_SECURE_NO_WARNINGS)
		add_definitions(-D_SCL_SECURE_NO_WARNINGS)
		add_definitions(-D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING) # Fix gtest and gmock build

		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4250") # 4250: 'class1' : inherits 'class2::member' via dominance
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251") # 4251: class 'xxx' needs to have dll-interface to be used by clients of class 'yyy'
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244") # 4244: conversion from 'xxx' to 'yyy', possible loss of data
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4267") # 4267: conversion from 'xxx' to 'yyy', possible loss of data
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4275") # 4275: non dll-interface class 'xxx' used as base for dll-interface class

		if(ENABLE_STRICT_COMPILATION)
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX") # Treats all compiler warnings as errors
		endif()

		if(ENABLE_MULTI_PROCESS_BUILDS)
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
		endif()

		# Workaround: Visual Studio has issues with exports of classes that inherit templates
		# https://stackoverflow.com/questions/44960760/msvc-dll-exporting-class-that-inherits-from-template-cause-lnk2005-already-defin
		# Reported to Microsoft here:
		# https://developercommunity.visualstudio.com/content/problem/224597/linker-failing-because-of-multiple-definitions-of.html
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /FORCE:MULTIPLE")
	endif(MSVC)
endif(MINGW OR MSVC)

if(ANDROID)
	if(ANDROID_NDK_MAJOR LESS 23 AND ANDROID_ABI MATCHES "^armeabi")
		# libunwind must come before other shared libs:
		# https://android.googlesource.com/platform/ndk/+/master/docs/BuildSystemMaintainers.md#Unwinding
		list(APPEND SYSTEM_LIBS unwind)
	endif()
	list(APPEND SYSTEM_LIBS log)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR NOT WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpointer-arith")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wuninitialized")

	if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 10.0 OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang" )
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wmismatched-tags")
	endif()

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")   # low chance of valid reports, a lot of emitted warnings
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch")             # large number of false-positives, disabled
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-reorder")            # large number of noise, low chance of any significant issues
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-compare")       # low chance of any significant issues

	if(ENABLE_STRICT_COMPILATION)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=array-bounds") # false positives in boost::multiarray during release build, keep as warning-only
	endif()

	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT WIN32)
			# For gcc 14+ we can use -fhardened instead
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_ASSERTIONS -fstack-protector-strong -fstack-clash-protection")
			if (CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
				set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcf-protection=full")
			endif()
		endif()
	endif()

	# Fix string inspection with lldb
	# https://stackoverflow.com/questions/58578615/cannot-inspect-a-stdstring-variable-in-lldb
	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fstandalone-debug")
	endif()

	if(UNIX)
		set(SYSTEM_LIBS ${SYSTEM_LIBS} ${CMAKE_DL_LIBS})
	endif()
endif()

# Check if some platform-specific libraries are needed for linking
if(HAIKU)
	set(SYSTEM_LIBS ${SYSTEM_LIBS} network)
endif()

if(ENABLE_LUA)
	add_definitions(-DSCRIPTING_ENABLED=1)
endif()

############################################
#        Finding packages                  #
############################################

set(BOOST_COMPONENTS date_time filesystem locale program_options)
if(ENABLE_INNOEXTRACT)
	list(APPEND BOOST_COMPONENTS iostreams)
endif()
if(NOT USING_CONAN)
	find_package(Boost 1.74.0 REQUIRED COMPONENTS ${BOOST_COMPONENTS})
	if(Boost_MAJOR_VERSION EQUAL 1 AND Boost_MINOR_VERSION LESS 69)
		list(APPEND BOOST_COMPONENTS system)
		find_package(Boost 1.74.0 REQUIRED COMPONENTS ${BOOST_COMPONENTS})
	endif()

	find_package(ZLIB REQUIRED)
	# Conan compatibility
	if(TARGET zlib::zlib)
		add_library(ZLIB::ZLIB ALIAS zlib::zlib)
	endif()
endif()

if(NOT USING_CONAN)
	find_package(minizip)
	if(NOT minizip_FOUND)
		find_package(minizip-ng REQUIRED)
		add_library(minizip::minizip ALIAS MINIZIP::minizip-ng)
	endif()
else()
	# Load minizip from Conan-generated config
	if(EXISTS "${CMAKE_SOURCE_DIR}/minizip-config.cmake")
		include(${CMAKE_SOURCE_DIR}/cmakedeps_macros.cmake)
		include(${CMAKE_SOURCE_DIR}/minizipTargets.cmake)
	endif()
	# Load ZLIB from Conan-generated config
	if(EXISTS "${CMAKE_SOURCE_DIR}/ZLIBConfig.cmake")
		include(${CMAKE_SOURCE_DIR}/ZLIBTargets.cmake)
	endif()
	# Load Boost from Conan-generated config
	if(EXISTS "${CMAKE_SOURCE_DIR}/BoostConfig.cmake")
		include(${CMAKE_SOURCE_DIR}/BoostTargets.cmake)
	endif()
	# Load BZip2 from Conan-generated config
	if(EXISTS "${CMAKE_SOURCE_DIR}/BZip2Config.cmake")
		include(${CMAKE_SOURCE_DIR}/BZip2Targets.cmake)
	endif()
	# Load TBB from Conan-generated config
	if(EXISTS "${CMAKE_SOURCE_DIR}/TBBConfig.cmake")
		include(${CMAKE_SOURCE_DIR}/TBBTargets.cmake)
	endif()
	# Load fmt from Conan-generated config
	if(EXISTS "${CMAKE_SOURCE_DIR}/fmtConfig.cmake")
		include(${CMAKE_SOURCE_DIR}/fmtTargets.cmake)
	endif()
endif()

if (ENABLE_CLIENT)
	if (ENABLE_VIDEO)
		find_package(ffmpeg REQUIRED COMPONENTS avutil swscale avformat avcodec swresample)
	endif()

	if(NOT USING_CONAN)
		find_package(SDL2 REQUIRED)
		if(TARGET SDL2::SDL2)
			add_library(SDL2::SDL2 ALIAS SDL2::SDL2)
		endif()
		if(TARGET SDL2::SDL2-static)
			add_library(SDL2::SDL2 ALIAS SDL2::SDL2-static)
		endif()
		find_package(SDL2_image REQUIRED)
		if(TARGET SDL2_image::SDL2_image)
			add_library(SDL2::Image ALIAS SDL2_image::SDL2_image)
		endif()
		if(TARGET SDL2_image::SDL2_image-static)
			add_library(SDL2::Image ALIAS SDL2_image::SDL2_image-static)
		endif()
		find_package(SDL2_mixer REQUIRED)
		if(TARGET SDL2_mixer::SDL2_mixer)
			add_library(SDL2::Mixer ALIAS SDL2_mixer::SDL2_mixer)
		endif()
		if(TARGET SDL2_mixer::SDL2_mixer-static)
			add_library(SDL2::Mixer ALIAS SDL2_mixer::SDL2_mixer-static)
		endif()
		find_package(SDL2_ttf REQUIRED)
		if(TARGET SDL2_ttf::SDL2_ttf)
			add_library(SDL2::TTF ALIAS SDL2_ttf::SDL2_ttf)
		endif()
		if(TARGET SDL2_ttf::SDL2_ttf-static)
			add_library(SDL2::TTF ALIAS SDL2_ttf::SDL2_ttf-static)
		endif()

		# Handle libsquish
		message(STATUS "Checking for libsquish...")
		find_package(libsquish QUIET)
		message(STATUS "libsquish_FOUND: ${libsquish_FOUND}")
		if(NOT TARGET libsquish::libsquish)
			# Manually create libsquish target if not found
			message(STATUS "Creating libsquish target manually...")
			file(GLOB CONAN_LIBSQUISH_DIRS "C:/Users/Administrator/.conan2/p/libsq*")
			message(STATUS "Found libsquish dirs: ${CONAN_LIBSQUISH_DIRS}")
			foreach(CONAN_LIBSQUISH_DIR ${CONAN_LIBSQUISH_DIRS})
				message(STATUS "Checking: ${CONAN_LIBSQUISH_DIR}")
				if(IS_DIRECTORY "${CONAN_LIBSQUISH_DIR}/p/lib")
					set(LIBSQUISH_INCLUDE_DIR "${CONAN_LIBSQUISH_DIR}/p/include")
					set(LIBSQUISH_LIBRARY "${CONAN_LIBSQUISH_DIR}/p/lib/squish.lib")
					message(STATUS "Looking for squish.h at: ${LIBSQUISH_INCLUDE_DIR}/squish.h")
					message(STATUS "Looking for squish.lib at: ${LIBSQUISH_LIBRARY}")
					if(EXISTS "${LIBSQUISH_INCLUDE_DIR}/squish.h" AND EXISTS "${LIBSQUISH_LIBRARY}")
						message(STATUS "Found libsquish at: ${CONAN_LIBSQUISH_DIR}")
						if(NOT TARGET libsquish::libsquish)
							add_library(libsquish::libsquish STATIC IMPORTED)
							set_target_properties(libsquish::libsquish PROPERTIES
								IMPORTED_LOCATION "${LIBSQUISH_LIBRARY}"
								INTERFACE_INCLUDE_DIRECTORIES "${LIBSQUISH_INCLUDE_DIR}"
							)
							message(STATUS "libsquish target created successfully")
						endif()
						break()
				 endif()
				endif()
			endforeach()
		endif()
		if(NOT TARGET libsquish::libsquish)
			message(FATAL_ERROR "libsquish::libsquish target still not found after manual creation")
		endif()
	else()
		# First load base dependencies (order matters!)
		# Load ZLIB from Conan-generated config
		if(EXISTS "${CMAKE_SOURCE_DIR}/ZLIBConfig.cmake")
			include(${CMAKE_SOURCE_DIR}/ZLIBTargets.cmake)
		endif()
		# Load BZip2 from Conan-generated config
		if(EXISTS "${CMAKE_SOURCE_DIR}/BZip2Config.cmake")
			include(${CMAKE_SOURCE_DIR}/BZip2Targets.cmake)
		endif()
		# Load LibLZMA from Conan-generated config
		if(EXISTS "${CMAKE_SOURCE_DIR}/LibLZMAConfig.cmake")
			include(${CMAKE_SOURCE_DIR}/LibLZMATargets.cmake)
		endif()
		# Load JPEG from Conan-generated config
		if(EXISTS "${CMAKE_SOURCE_DIR}/JPEGConfig.cmake")
			include(${CMAKE_SOURCE_DIR}/JPEGTargets.cmake)
			if(TARGET JPEG::JPEG)
				add_library(JPEG::JPEG ALIAS JPEG::JPEG)
			endif()
		endif()
		# Load TIFF from Conan-generated config (depends on LibLZMA)
		if(EXISTS "${CMAKE_SOURCE_DIR}/TIFFConfig.cmake")
			include(${CMAKE_SOURCE_DIR}/TIFFTargets.cmake)
			if(TARGET TIFF::TIFF)
				add_library(TIFF::TIFF ALIAS TIFF::TIFF)
			endif()
		endif()
		# Load Freetype from Conan-generated config
		if(EXISTS "${CMAKE_SOURCE_DIR}/freetype-config.cmake")
			include(${CMAKE_SOURCE_DIR}/freetypeTargets.cmake)
			if(TARGET Freetype::Freetype)
				add_library(Freetype::Freetype ALIAS Freetype::Freetype)
			endif()
		endif()
		# Load PNG from Conan-generated config
		if(EXISTS "${CMAKE_SOURCE_DIR}/PNGConfig.cmake")
			include(${CMAKE_SOURCE_DIR}/PNGTargets.cmake)
			if(TARGET PNG::PNG)
				add_library(PNG::PNG ALIAS PNG::PNG)
			endif()
		endif()
		# Load SDL2 from Conan-generated config
		if(EXISTS "${CMAKE_SOURCE_DIR}/SDL2Config.cmake")
			include(${CMAKE_SOURCE_DIR}/SDL2Targets.cmake)
			if(TARGET SDL2::SDL2)
				add_library(SDL2::SDL2 ALIAS SDL2::SDL2)
			endif()
		endif()
		# Load SDL2_image from Conan-generated config (depends on JPEG, TIFF, PNG)
		if(EXISTS "${CMAKE_SOURCE_DIR}/SDL2_imageConfig.cmake")
			include(${CMAKE_SOURCE_DIR}/SDL2_imageTargets.cmake)
			if(TARGET SDL2_image::SDL2_image)
				add_library(SDL2::Image ALIAS SDL2_image::SDL2_image)
			endif()
		endif()
		# Load SDL2_mixer from Conan-generated config
		if(EXISTS "${CMAKE_SOURCE_DIR}/SDL2_mixerConfig.cmake")
			include(${CMAKE_SOURCE_DIR}/SDL2_mixerTargets.cmake)
			if(TARGET SDL2_mixer::SDL2_mixer)
				add_library(SDL2::Mixer ALIAS SDL2_mixer::SDL2_mixer)
			endif()
		else()
			# Fallback: manually create SDL2::Mixer target from SDL_mixer Conan package
			# Find SDL_mixer package directory
			file(GLOB CONAN_SDLMIXER_DIRS "C:/Users/Administrator/.conan2/p/sdl_m*")
			foreach(CONAN_SDLMIXER_DIR ${CONAN_SDLMIXER_DIRS})
				if(IS_DIRECTORY "${CONAN_SDLMIXER_DIR}/p/lib")
				 set(SDL2_MIXER_INCLUDE_DIR "${CONAN_SDLMIXER_DIR}/p/include/SDL2")
				 set(SDL2_MIXER_LIBRARY "${CONAN_SDLMIXER_DIR}/p/lib/SDL2_mixer-static.lib")
				 if(EXISTS "${SDL2_MIXER_INCLUDE_DIR}/SDL_mixer.h" AND EXISTS "${SDL2_MIXER_LIBRARY}")
					 if(NOT TARGET SDL2::Mixer)
						 add_library(SDL2::Mixer STATIC IMPORTED)
						 set_target_properties(SDL2::Mixer PROPERTIES
							 IMPORTED_LOCATION "${SDL2_MIXER_LIBRARY}"
							 INTERFACE_INCLUDE_DIRECTORIES "${CONAN_SDLMIXER_DIR}/p/include;${CONAN_SDLMIXER_DIR}/p/include/SDL2"
						 )
						 if(TARGET SDL2::SDL2)
							 target_link_libraries(SDL2::Mixer INTERFACE SDL2::SDL2)
						 endif()
						 # Add mpg123 dependency
						 if(EXISTS "C:/Users/Administrator/.conan2/p/mpg129d8cc1b75d6ea/p/lib/mpg123.lib")
							 target_link_libraries(SDL2::Mixer INTERFACE "C:/Users/Administrator/.conan2/p/mpg129d8cc1b75d6ea/p/lib/mpg123.lib")
						 endif()
						 # Add opus dependency
						 if(EXISTS "C:/Users/Administrator/.conan2/p/opusf86a7e2c79fc97/p/lib/opusfile.lib")
							 target_link_libraries(SDL2::Mixer INTERFACE "C:/Users/Administrator/.conan2/p/opusf86a7e2c79fc97/p/lib/opusfile.lib")
						 endif()
						 # Add opus base library
						 if(EXISTS "C:/Users/Administrator/.conan2/p/opus30299ba642ac9/p/lib/opus.lib")
							 target_link_libraries(SDL2::Mixer INTERFACE "C:/Users/Administrator/.conan2/p/opus30299ba642ac9/p/lib/opus.lib")
						 endif()
						 # Add ogg library
						 if(EXISTS "C:/Users/Administrator/.conan2/p/ogg95fbc54750358/p/lib/ogg.lib")
							 target_link_libraries(SDL2::Mixer INTERFACE "C:/Users/Administrator/.conan2/p/ogg95fbc54750358/p/lib/ogg.lib")
						 endif()
						 # Add modplug library
						 if(EXISTS "C:/Users/Administrator/.conan2/p/libmoa2738ee70bf43/p/lib/modplug.lib")
							 target_link_libraries(SDL2::Mixer INTERFACE "C:/Users/Administrator/.conan2/p/libmoa2738ee70bf43/p/lib/modplug.lib")
						 endif()
						 # Add Windows system libraries for mpg123
						 target_link_libraries(SDL2::Mixer INTERFACE shlwapi.lib shell32.lib)
						 message(STATUS "SDL2_mixer found manually: ${SDL2_MIXER_LIBRARY}")
					 endif()
				 endif()
			 endif()
			endforeach()
		endif()
		# Load SDL2_ttf from Conan-generated config (depends on Freetype)
		if(EXISTS "${CMAKE_SOURCE_DIR}/SDL2_ttfConfig.cmake")
			include(${CMAKE_SOURCE_DIR}/SDL2_ttfTargets.cmake)
			if(TARGET SDL2_ttf::SDL2_ttf)
				add_library(SDL2::TTF ALIAS SDL2_ttf::SDL2_ttf)
			endif()
		endif()
	endif()
endif()

if(ENABLE_LOBBY)
	find_package(SQLite3 REQUIRED)
endif()

if(ENABLE_LAUNCHER OR ENABLE_EDITOR)
	# Widgets finds its own dependencies (QtGui and QtCore).
	find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
	find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

	if(ENABLE_TEMPLATE_EDITOR)
		find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Svg Xml)
		find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Svg Xml)

		if(QT_VERSION_MAJOR EQUAL 6)
			find_package(QT NAMES Qt6 REQUIRED COMPONENTS SvgWidgets)
			find_package(Qt6 REQUIRED COMPONENTS SvgWidgets)
		endif()
	endif()

	if(ENABLE_TRANSLATIONS)
		find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS LinguistTools)
		add_definitions(-DENABLE_QT_TRANSLATIONS)
	endif()
endif()

# Handle libsquish for both USING_CONAN and NOT USING_CONAN
message(STATUS "Checking for libsquish (global)...")
if(NOT TARGET libsquish::libsquish)
	# Try Conan first
	message(STATUS "Looking for Conan libsquish config...")
	if(EXISTS "${CMAKE_SOURCE_DIR}/libsquishConfig.cmake")
		include("${CMAKE_SOURCE_DIR}/libsquishConfig.cmake")
		message(STATUS "Found libsquish Conan config")
	endif()
	# If not found, try manual creation from Conan cache
	if(NOT TARGET libsquish::libsquish)
		message(STATUS "Creating libsquish target manually from Conan cache...")
		file(GLOB CONAN_LIBSQUISH_DIRS "C:/Users/Administrator/.conan2/p/libsq*")
		message(STATUS "Found libsquish dirs: ${CONAN_LIBSQUISH_DIRS}")
		foreach(CONAN_LIBSQUISH_DIR ${CONAN_LIBSQUISH_DIRS})
			message(STATUS "Checking: ${CONAN_LIBSQUISH_DIR}")
			if(IS_DIRECTORY "${CONAN_LIBSQUISH_DIR}/p/lib")
				set(LIBSQUISH_INCLUDE_DIR "${CONAN_LIBSQUISH_DIR}/p/include")
				set(LIBSQUISH_LIBRARY "${CONAN_LIBSQUISH_DIR}/p/lib/squish.lib")
				message(STATUS "Looking for squish.h at: ${LIBSQUISH_INCLUDE_DIR}/squish.h")
				message(STATUS "Looking for squish.lib at: ${LIBSQUISH_LIBRARY}")
				if(EXISTS "${LIBSQUISH_INCLUDE_DIR}/squish.h" AND EXISTS "${LIBSQUISH_LIBRARY}")
					message(STATUS "Found libsquish at: ${CONAN_LIBSQUISH_DIR}")
					if(NOT TARGET libsquish::libsquish)
						add_library(libsquish::libsquish STATIC IMPORTED)
						set_target_properties(libsquish::libsquish PROPERTIES
							IMPORTED_LOCATION "${LIBSQUISH_LIBRARY}"
							INTERFACE_INCLUDE_DIRECTORIES "${LIBSQUISH_INCLUDE_DIR}"
						)
						message(STATUS "libsquish target created successfully")
					endif()
					break()
				endif()
			endif()
		endforeach()
	endif()
endif()
# Final check for libsquish
if(NOT TARGET libsquish::libsquish)
	message(FATAL_ERROR "libsquish::libsquish target still not found after all attempts")
endif()

# Handle sqlite3
message(STATUS "Checking for sqlite3...")
if(NOT TARGET sqlite3::sqlite3)
	# Try Conan first
	message(STATUS "Looking for Conan sqlite3 config...")
	if(EXISTS "${CMAKE_SOURCE_DIR}/sqlite3Config.cmake")
		include("${CMAKE_SOURCE_DIR}/sqlite3Config.cmake")
		message(STATUS "Found sqlite3 Conan config")
	endif()
	# If not found, try manual creation from Conan cache
	if(NOT TARGET sqlite3::sqlite3)
		message(STATUS "Creating sqlite3 target manually from Conan cache...")
		file(GLOB CONAN_SQLITE3_DIRS "C:/Users/Administrator/.conan2/p/sqlitc*")
		message(STATUS "Found sqlite3 dirs: ${CONAN_SQLITE3_DIRS}")
		foreach(CONAN_SQLITE3_DIR ${CONAN_SQLITE3_DIRS})
			message(STATUS "Checking: ${CONAN_SQLITE3_DIR}")
			if(IS_DIRECTORY "${CONAN_SQLITE3_DIR}/p/lib")
				set(SQLITE3_INCLUDE_DIR "${CONAN_SQLITE3_DIR}/p/include")
				set(SQLITE3_LIBRARY "${CONAN_SQLITE3_DIR}/p/lib/sqlite3.lib")
				message(STATUS "Looking for sqlite3.h at: ${SQLITE3_INCLUDE_DIR}/sqlite3.h")
				message(STATUS "Looking for sqlite3.lib at: ${SQLITE3_LIBRARY}")
				if(EXISTS "${SQLITE3_INCLUDE_DIR}/sqlite3.h" AND EXISTS "${SQLITE3_LIBRARY}")
					message(STATUS "Found sqlite3 at: ${CONAN_SQLITE3_DIR}")
					if(NOT TARGET sqlite3::sqlite3)
						add_library(sqlite3::sqlite3 STATIC IMPORTED)
						set_target_properties(sqlite3::sqlite3 PROPERTIES
							IMPORTED_LOCATION "${SQLITE3_LIBRARY}"
							INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_INCLUDE_DIR}"
						)
						message(STATUS "sqlite3 target created successfully")
					endif()
					break()
				endif()
			endif()
		endforeach()
	endif()
endif()
# sqlite3 target — create stub if not found from Conan
if(NOT TARGET sqlite3::sqlite3)
  add_library(sqlite3 SHARED IMPORTED)
  set_target_properties(sqlite3 PROPERTIES IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libsqlite3.so)
  add_library(sqlite3::sqlite3 ALIAS sqlite3)
endif()

if(NOT ENABLE_MINIMAL_LIB)
	if(NOT USING_CONAN)
		find_package(TBB REQUIRED)
	else()
		# Conan provides onetbb::onetbb target, but TBBConfig.cmake might not be available
		# We'll let it fail naturally if not found since USING_CONAN implies dependencies are handled
	endif()
endif()

if(ENABLE_LUA)
	find_package(luajit)

	if(TARGET luajit::luajit)
		message(STATUS "Using LuaJIT provided by system")
	else()
		message(STATUS "Cannot find LuaJIT! Fallback to using usual Lua.")
		find_package(Lua REQUIRED)
		if(Lua_FOUND)
			add_library(luajit::luajit UNKNOWN IMPORTED)
			set_target_properties(luajit::luajit PROPERTIES
				INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}")
			set_target_properties(luajit::luajit PROPERTIES
				IMPORTED_LOCATION "${LUA_LIBRARIES}")
		endif()
	endif()
endif()

############################################
#        Output directories                #
############################################

if(WIN32) # on Win everything goes into H3 root directory
	set(BIN_DIR "." CACHE STRING "Where to install binaries")
	set(LIB_DIR "." CACHE STRING "Where to install main library")
	set(DATA_DIR "." CACHE STRING "Where to install data files")
elseif(APPLE)
	# includes lib path which determines where to install shared libraries (either /lib or /lib64)
	include(GNUInstallDirs)

	set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
	if(ENABLE_MONOLITHIC_INSTALL)
		set(BIN_DIR "." CACHE STRING "Where to install binaries")
		set(LIB_DIR "." CACHE STRING "Where to install main library")
		set(DATA_DIR "." CACHE STRING "Where to install data files")
	else()
		if(MACOS)
			set(APP_BUNDLE_DIR "${CMAKE_PROJECT_NAME}.app")
			set(APP_BUNDLE_CONTENTS_DIR "${APP_BUNDLE_DIR}/Contents")
			set(APP_BUNDLE_BINARY_DIR "${APP_BUNDLE_CONTENTS_DIR}/MacOS")
			set(APP_BUNDLE_RESOURCES_DIR "${APP_BUNDLE_CONTENTS_DIR}/Resources")

			set(BIN_DIR "${APP_BUNDLE_BINARY_DIR}" CACHE STRING "Where to install binaries")
			set(LIB_DIR "${APP_BUNDLE_CONTENTS_DIR}/Frameworks" CACHE STRING "Where to install main library")
			set(DATA_DIR "${APP_BUNDLE_RESOURCES_DIR}/Data" CACHE STRING "Where to install data files")
		else()
			set(LIB_DIR "Frameworks")
			set(DATA_DIR ".")
		endif()
	endif()
elseif(ANDROID)
	include(GNUInstallDirs)
	set(LIB_DIR "libs/${ANDROID_ABI}")

	file(READ "${CMAKE_ANDROID_NDK}/meta/abis.json" ndkAbiInfo)
	string(JSON ANDROID_SYSROOT_LIB_SUBDIR GET "${ndkAbiInfo}" "${ANDROID_ABI}" "triple")

	# required by Qt
	set(androidPackageSourceDir "${CMAKE_SOURCE_DIR}/android")
	set(androidQtBuildDir "${CMAKE_BINARY_DIR}/android-build")
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${androidQtBuildDir}/${LIB_DIR}")
else()
	# includes lib path which determines where to install shared libraries (either /lib or /lib64)
	include(GNUInstallDirs)

	if(ENABLE_MONOLITHIC_INSTALL)
		set(BIN_DIR "." CACHE STRING "Where to install binaries")
		set(LIB_DIR "." CACHE STRING "Where to install main library")
		set(DATA_DIR "." CACHE STRING "Where to install data files")

		# following constants only used for platforms using XDG (Linux, BSD, etc)
		add_definitions(-DM_DATA_DIR="${DATA_DIR}")
		add_definitions(-DM_BIN_DIR="${BIN_DIR}")
		add_definitions(-DM_LIB_DIR="${LIB_DIR}")

		set(CMAKE_INSTALL_RPATH "$ORIGIN/")
	else()
		if(NOT BIN_DIR)
			set(BIN_DIR "${CMAKE_INSTALL_BINDIR}" CACHE STRING "Where to install binaries")
		endif()
		if(NOT LIB_DIR)
			set(LIB_DIR "${CMAKE_INSTALL_LIBDIR}/vcmi" CACHE STRING "Where to install main library")
		endif()
		if(NOT DATA_DIR)
			set(DATA_DIR "${CMAKE_INSTALL_DATAROOTDIR}/vcmi" CACHE STRING "Where to install data files")
		endif()

		# following constants only used for platforms using XDG (Linux, BSD, etc)
		add_definitions(-DM_DATA_DIR="${CMAKE_INSTALL_PREFIX}/${DATA_DIR}")
		add_definitions(-DM_BIN_DIR="${CMAKE_INSTALL_PREFIX}/${BIN_DIR}")
		add_definitions(-DM_LIB_DIR="${CMAKE_INSTALL_PREFIX}/${LIB_DIR}")

		set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${LIB_DIR}")
	endif()
endif()

# iOS/Android have flat libs directory structure
if(IOS OR ANDROID)
	set(AI_LIB_DIR "${LIB_DIR}")
	set(SCRIPTING_LIB_DIR "${LIB_DIR}")
else()
	set(AI_LIB_DIR "${LIB_DIR}/AI")
	set(SCRIPTING_LIB_DIR "${LIB_DIR}/scripting")
endif()

# common Qt paths
if(ENABLE_LAUNCHER OR ENABLE_EDITOR)
	get_target_property(qmakePath Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
	get_filename_component(qtDir "${qmakePath}/../../" ABSOLUTE)
	set(qtBinDir "${qtDir}/bin")
endif()

#######################################
#        Add subdirectories           #
#######################################

if(IOS)
	add_subdirectory(ios)
endif()

if (ENABLE_CLIENT)
	add_subdirectory_with_folder("AI" AI)
endif()

add_subdirectory(lib)

if (ENABLE_CLIENT OR ENABLE_SERVER)
	add_subdirectory(server)
endif()

if(ENABLE_ERM)
	add_subdirectory(scripting/erm)
endif()

if(ENABLE_LUA)
	add_subdirectory(scripting/lua)
endif()

if(ENABLE_LAUNCHER OR ENABLE_EDITOR)
	add_subdirectory(vcmiqt)
endif()

if(ENABLE_LAUNCHER)
	add_subdirectory(launcher)
endif()

if(ENABLE_EDITOR)
	add_subdirectory(mapeditor)
endif()

if(ENABLE_LOBBY)
	add_subdirectory(lobby)
endif()

if (ENABLE_CLIENT)
	add_subdirectory(client)
	add_subdirectory(clientapp)
endif()

if(ENABLE_SERVER)
	add_subdirectory(serverapp)
endif()

if(ENABLE_TEST)
	enable_testing()
	add_subdirectory(test)
endif()

if(ENABLE_ML)
   add_subdirectory(ML)
endif()

#######################################
#        Installation section         #
#######################################

if(DEFINED CONAN_RUNTIME_LIBS_FILE AND EXISTS "${CONAN_RUNTIME_LIBS_FILE}")
	vcmi_install_conan_deps()
endif()

if(ANDROID)
	string(REPLACE ";" "\n" ANDROID_GRADLE_PROPERTIES_MULTILINE "${ANDROID_GRADLE_PROPERTIES}")
	file(WRITE "${androidPackageSourceDir}/vcmi-app/gradle.properties" "signingRoot=${CMAKE_SOURCE_DIR}/CI/android\n${ANDROID_GRADLE_PROPERTIES_MULTILINE}")

	if(ANDROID_STL MATCHES "_shared$")
		set(stlLibName "${CMAKE_SHARED_LIBRARY_PREFIX}${ANDROID_STL}${CMAKE_SHARED_LIBRARY_SUFFIX}")
		vcmi_install_libs_symlink("${CMAKE_SYSROOT}/usr/lib/${ANDROID_SYSROOT_LIB_SUBDIR}/${stlLibName}")
	endif()
else()
	install(DIRECTORY config DESTINATION ${DATA_DIR})
	if (ENABLE_CLIENT OR ENABLE_SERVER)
		install(DIRECTORY Mods DESTINATION ${DATA_DIR})
	endif()
endif()
if(ENABLE_LUA)
	install(DIRECTORY scripts DESTINATION ${DATA_DIR})
endif()

# that script is useless for Windows / iOS / Android
if(NOT WIN32 AND NOT IOS AND NOT ANDROID AND NOT ENABLE_MINIMAL_LIB)
	install(FILES vcmibuilder DESTINATION ${BIN_DIR} PERMISSIONS
		OWNER_WRITE OWNER_READ OWNER_EXECUTE
					GROUP_READ GROUP_EXECUTE
					WORLD_READ WORLD_EXECUTE)
endif()

#######################################
#       Packaging section             #
#######################################

# This needs to be removed in future when successfully set OpenSign CI step for binaries signing to avoid resigning MS Redist / UCRT
if(MSVC)
	SET(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${BIN_DIR})
	Include(InstallRequiredSystemLibraries)
endif()

set(CPACK_PACKAGE_VERSION_MAJOR ${VCMI_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VCMI_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VCMI_VERSION_PATCH})

# vcmi does not have "patch version" in version string
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}")
#TODO: remove version from Global.h and use this one as define?

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)

if("${PACKAGE_NAME_SUFFIX}" STREQUAL "")
	set(CPACK_PACKAGE_NAME "VCMI")
else()
	set(CPACK_PACKAGE_NAME "VCMI ${PACKAGE_NAME_SUFFIX}")
endif()
if("${PACKAGE_FILE_NAME}" STREQUAL "")
	set(CPACK_PACKAGE_FILE_NAME "vcmi-${CPACK_PACKAGE_VERSION}")
else()
	set(CPACK_PACKAGE_FILE_NAME "${PACKAGE_FILE_NAME}")
endif()
set(CPACK_PACKAGE_VENDOR "VCMI team")

add_definitions(-DVCMI_BIN_DIR="${CMAKE_BINARY_DIR}/bin")

if(WIN32)
	# This needs to be removed in future as we already migrated to Inno Setup and artifacts in CI are always packed to ZIP
	# Note: due to NSI script generation process all of the backward slashes here are useful
	set(CPACK_GENERATOR ZIP)
	set(CPACK_MONOLITHIC_INSTALL 1)
	set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/license.txt")
	set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}")
	if("${PACKAGE_NAME_SUFFIX}" STREQUAL "")
		set(CPACK_NSIS_PACKAGE_NAME "VCMI ${CPACK_PACKAGE_VERSION}")
	else()
		set(CPACK_NSIS_PACKAGE_NAME "VCMI ${CPACK_PACKAGE_VERSION} ${PACKAGE_NAME_SUFFIX} ")
	endif()
	if(CMAKE_SYSTEM_PROCESSOR MATCHES ".*64")
		set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
	else()
		set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
	endif()

	if(ENABLE_LAUNCHER)
		set(VCMI_MAIN_EXECUTABLE "VCMI_launcher")
	else()
		set(VCMI_MAIN_EXECUTABLE "VCMI_client")
	endif()
	set(CPACK_PACKAGE_EXECUTABLES "${VCMI_MAIN_EXECUTABLE};VCMI")
	set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
		CreateShortCut \\\"$DESKTOP\\\\VCMI.lnk\\\" \\\"$INSTDIR\\\\${VCMI_MAIN_EXECUTABLE}.exe\\\"
		ExecShell '' 'netsh' 'advfirewall firewall add rule name=vcmi_server dir=in action=allow program=\\\"$INSTDIR\\\\vcmi_server.exe\\\" enable=yes profile=public,private' SW_HIDE
		ExecShell '' 'netsh' 'advfirewall firewall add rule name=vcmi_client dir=in action=allow program=\\\"$INSTDIR\\\\vcmi_client.exe\\\" enable=yes profile=public,private' SW_HIDE
	")
	set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
		Delete \\\"$DESKTOP\\\\VCMI.lnk\\\"
		ExecShell '' 'netsh' 'advfirewall firewall delete rule name=vcmi_server' SW_HIDE
		ExecShell '' 'netsh' 'advfirewall firewall delete rule name=vcmi_client' SW_HIDE
	")

	# set the install/unistall icon used for the installer itself
	# There is a bug in NSI that does not handle full unix paths properly.
	set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/clientapp/icons\\\\vcmi.ico")
	set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/clientapp/icons\\\\vcmi.ico")
	# set the package header icon for MUI
	set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/clientapp/icons\\\\vcmi.ico")

	set(CPACK_NSIS_MENU_LINKS "http://vcmi.eu/" "VCMI Web Site")

	set(CPACK_NSIS_INSTALLED_ICON_NAME "VCMI_client.exe")
	set(CPACK_NSIS_COMPRESSOR "/SOLID lzma")
	set(CPACK_NSIS_DISPLAY_NAME "${CPACK_NSIS_PACKAGE_NAME}, open-source engine for Heroes of Might and Magic III ")
	set(CPACK_NSIS_HELP_LINK "http://vcmi.eu/")
	set(CPACK_NSIS_URL_INFO_ABOUT "http://vcmi.eu/")
	set(CPACK_NSIS_CONTACT @CPACK_PACKAGE_CONTACT@)
	set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")

	if(NOT (${CMAKE_CROSSCOMPILING}))
		add_subdirectory(win)
	endif()
elseif(MACOS AND NOT ENABLE_MONOLITHIC_INSTALL)
	set(CPACK_MONOLITHIC_INSTALL 1)
	set(CPACK_GENERATOR "DragNDrop")
	set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_SOURCE_DIR}/osx/dmg_background.png")
	# Workaround for this issue:
	# CPack Error: Error executing: /usr/bin/hdiutil detach "/Volumes/VCMI"
	# https://github.com/actions/runner-images/issues/7522#issuecomment-1564467252
	if(DEFINED ENV{GITHUB_RUN_ID})
		set(CPACK_COMMAND_HDIUTIL "/usr/bin/sudo /usr/bin/hdiutil")
	endif()
	# CMake code for CPACK_DMG_DS_STORE executed before DS_STORE_SETUP_SCRIPT
	# So we can keep both enabled and this shouldn't hurt
	# set(CPACK_DMG_DS_STORE "${CMAKE_SOURCE_DIR}/osx/dmg_DS_Store")
	set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_SOURCE_DIR}/osx/DS_Store_Setup.scpt")

	# Always use "VCMI" as volume name so full path will be /Volumes/VCMI/
	# Otherwise DMG background image wouldn't work
	# Pre-generated DS_Store use absolute path to background image
	set(CPACK_DMG_VOLUME_NAME "${CMAKE_PROJECT_NAME}")

	include(GetGitRevisionDescription)
	get_git_head_revision(GIT_REFSPEC GIT_SHA1)
	string(TIMESTAMP CURRENT_YEAR "%Y")

	set(MACOSX_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
	set(MACOSX_BUNDLE_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
	set(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2007-${CURRENT_YEAR} VCMI team")
	set(MACOSX_BUNDLE_GUI_IDENTIFIER "eu.vcmi.vcmi")
	set(MACOSX_BUNDLE_BUNDLE_VERSION ${GIT_SHA1})
	set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${APP_SHORT_VERSION})
	if(ENABLE_LAUNCHER)
		set(MACOSX_BUNDLE_EXECUTABLE_NAME "vcmilauncher")
	else()
		set(MACOSX_BUNDLE_EXECUTABLE_NAME "vcmiclient")
	endif()
	set(MACOSX_BUNDLE_ICON_FILE "vcmi.icns")

	install(FILES "${CMAKE_SOURCE_DIR}/osx/vcmi.icns" DESTINATION ${APP_BUNDLE_RESOURCES_DIR})

	configure_file("${CMAKE_SOURCE_DIR}/osx/Info.plist.in" "${CMAKE_BINARY_DIR}/Info.plist")
	install(FILES "${CMAKE_BINARY_DIR}/Info.plist" DESTINATION ${APP_BUNDLE_CONTENTS_DIR})

	# Bundle fixing code must be in separate directory to be executed after all other install code
	add_subdirectory(osx)
elseif(IOS)
	set(CPACK_GENERATOR ZIP)
	set(CPACK_ARCHIVE_FILE_EXTENSION ipa)
	set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
	set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_CURRENT_BINARY_DIR};${CMAKE_PROJECT_NAME};app;/")
else()
	set(CPACK_GENERATOR TGZ)
endif()

include(CPack)
