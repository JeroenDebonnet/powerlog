cmake_minimum_required(VERSION 3.20)

project(powerlog)

set(CMAKE_CXX_STANDARD 20)
find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

add_executable(powerlog
    src/main.cpp
    src/p1_logger.cpp
    src/freenove_lcd.cpp
    src/terminal_display.cpp
)

target_include_directories(powerlog PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(powerlog PRIVATE ${CURSES_LIBRARIES} Threads::Threads)

configure_file(plot_powerlog.py plot_powerlog.py COPYONLY)
