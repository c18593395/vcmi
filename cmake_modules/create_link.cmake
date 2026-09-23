#message(${CMAKE_ARGV0}) # cmake.exe
#message(${CMAKE_ARGV1}) # -P
#message(${CMAKE_ARGV2}) # thisfilename
#message(${CMAKE_ARGV3}) # existing
#message(${CMAKE_ARGV4}) # linkname
# FIXED: mklink /J fails on some Windows setups; use plain copy (robust for in-place execution)
file(REMOVE_RECURSE "${CMAKE_ARGV4}")
get_filename_component(_parent "${CMAKE_ARGV4}" DIRECTORY)
file(COPY "${CMAKE_ARGV3}" DESTINATION "${_parent}")
