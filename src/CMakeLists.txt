#-----------------------------------------------------------------------------
#
#  CMake Config
#
#-----------------------------------------------------------------------------

add_executable(linestringssplitter linestringssplitter.cpp output.cpp)
target_link_libraries(linestringssplitter ${GDAL_LIBRARIES})
install(TARGETS linestringssplitter DESTINATION bin)

