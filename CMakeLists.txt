cmake_minimum_required(VERSION 3.23)
project(osc-app-installer CXX C)

include_directories(${LIBOGCDIR}/include)
include_directories(${DEVKITPRO}/portlibs/wii/include)
include_directories(${DEVKITPRO}/portlibs/ppc/include)
link_directories(${DEVKITPRO}/portlibs/wii/lib)
link_directories(${DEVKITPRO}/portlibs/ppc/lib)
link_directories(${DEVKITPRO}/libogc/lib/wii)

set(MACHDEP "-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ASM_FLAGS "-x assembler-with-cpp")
set(CMAKE_C_FLAGS "-Wall -Wextra -Wno-unused-function -O2 ${CMAKE_CXX_FLAGS} ${MACHDEP}")
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-function -O2 ${CMAKE_CXX_FLAGS} ${MACHDEP} -fdiagnostics-color")
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

add_executable(osc-app-installer miniz.c miniz.h main.cpp)
target_link_libraries(osc-app-installer wiiuse bte fat ogc m patcher)

add_custom_command(
        TARGET osc-app-installer
        POST_BUILD
        COMMAND /opt/devkitpro/tools/bin/elf2dol
        ARGS osc-app-installer.elf osc-app-installer.dol
)