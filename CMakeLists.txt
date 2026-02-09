cmake_minimum_required(VERSION 3.14)
project(xbox_control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# libevdev (evdev interface for gamepads)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)

# yaml-cpp for configuration files
find_package(yaml-cpp REQUIRED)

# Controller config library
add_library(controller_config
  src/controller_config.cpp
)
target_include_directories(controller_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(controller_config PRIVATE yaml-cpp)

# Controller base library
add_library(controller_base
  src/controller_base.cpp
)
target_include_directories(controller_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(controller_base PRIVATE
  ${LIBEVDEV_LIBRARIES}
  controller_config
)
target_compile_options(controller_base PRIVATE ${LIBEVDEV_CFLAGS_OTHER})
target_include_directories(controller_base PRIVATE ${LIBEVDEV_INCLUDE_DIRS})

# UDP library
add_library(udp_comm
  src/udp_publisher.cpp
  src/udp_receiver.cpp
)
target_include_directories(udp_comm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Main joystick application
add_executable(joystick
  src/joystick.cpp
)
target_include_directories(joystick PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(joystick PRIVATE
  ${LIBEVDEV_LIBRARIES}
  controller_base
  udp_comm
)
target_compile_options(joystick PRIVATE ${LIBEVDEV_CFLAGS_OTHER})
target_include_directories(joystick PRIVATE ${LIBEVDEV_INCLUDE_DIRS})

# Test receiver: receives UDP packets and prints input
add_executable(udp_receiver_test
  src/udp_receiver_test.cpp
)
target_include_directories(udp_receiver_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(udp_receiver_test PRIVATE controller_config)

# Vibration sender: sends vibration commands to controllers
add_executable(vibration_sender
  src/vibration_sender.cpp
)
target_include_directories(vibration_sender PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Install config files
install(DIRECTORY config/ DESTINATION share/xbox_control/config)

install(TARGETS joystick udp_receiver_test controller_config controller_base udp_comm
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

# Note: vibration_sender is no longer needed - vibration commands are sent via UDP
# and handled by the joystick application
