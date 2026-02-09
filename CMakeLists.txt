cmake_minimum_required(VERSION 3.14)
project(xbox_control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# libevdev (evdev interface for gamepads)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)

# Publisher: reads Xbox controllers via evdev, sends over UDP
add_executable(xbox_udp_publisher
  src/xbox_udp_publisher.cpp
)
target_include_directories(xbox_udp_publisher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(xbox_udp_publisher PRIVATE
  ${LIBEVDEV_LIBRARIES}
)
target_compile_options(xbox_udp_publisher PRIVATE ${LIBEVDEV_CFLAGS_OTHER})
target_include_directories(xbox_udp_publisher PRIVATE ${LIBEVDEV_INCLUDE_DIRS})

# Test receiver: receives UDP packets and prints input
add_executable(udp_receiver_test
  src/udp_receiver_test.cpp
)
target_include_directories(udp_receiver_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

install(TARGETS xbox_udp_publisher udp_receiver_test
  RUNTIME DESTINATION bin
)
