cmake_minimum_required(VERSION 3.5.0)
PROJECT(quantum_plus CXX C)
set (CMAKE_CXX_STANDARD 11)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake_modules/")
set(BIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/bin")
# set(RULES_INSTALL_DIR "/etc/udev/rules.d")
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall")

find_package(INDI REQUIRED)
find_package(Threads REQUIRED)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h )

include_directories( ${CMAKE_CURRENT_BINARY_DIR})
include_directories( ${CMAKE_SOURCE_DIR})

include_directories( ${INDI_INCLUDE_DIR})

set(indiallinone_SRCS
   ${CMAKE_SOURCE_DIR}/quantum_wheel.cpp
   )

add_executable(indi_quantum_plus ${indiallinone_SRCS})

target_link_libraries(indi_quantum_plus ${INDI_DRIVER_LIBRARIES})
target_link_libraries(indi_quantum_plus Threads::Threads)

install(TARGETS indi_quantum_plus RUNTIME DESTINATION bin )

install(FILES indi_quantum_plus.xml DESTINATION ${INDI_DATA_DIR})
# install(FILES 99-qsi.rules DESTINATION ${RULES_INSTALL_DIR})
