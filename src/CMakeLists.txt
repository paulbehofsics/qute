file(GLOB SOURCES *.cc)

add_executable(qute ${SOURCES})
set_target_properties(qute PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
target_include_directories(qute PUBLIC "/home/fs/docopt.cpp")

target_link_libraries(qute ${Boost_LIBRARIES})
target_link_libraries(qute ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(qute docopt)
