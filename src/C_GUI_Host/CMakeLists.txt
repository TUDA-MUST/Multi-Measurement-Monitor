cmake_minimum_required(VERSION 3.16)
project(MultiMeasurementMonitor C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED gtk+-3.0 json-glib-1.0)

add_executable(MultiMeasurementMonitor
    C_GUI_Host_main.c
    gui.c
    client.c
    network.c
    settings.c
)

target_include_directories(MultiMeasurementMonitor PRIVATE
    ${GTK_INCLUDE_DIRS}
)

target_compile_options(MultiMeasurementMonitor PRIVATE
    -O3
    -ftree-vectorize
    ${GTK_CFLAGS_OTHER}
)

target_link_libraries(MultiMeasurementMonitor PRIVATE
    ${GTK_LIBRARIES}
)

if(WIN32)
    target_compile_definitions(MultiMeasurementMonitor PRIVATE
        _WIN32
        DEBUG
    )

    target_link_libraries(MultiMeasurementMonitor PRIVATE
        ws2_32
        iphlpapi
        dbghelp
    )

    set_target_properties(MultiMeasurementMonitor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/WinApp/bin"
        OUTPUT_NAME "MultiMeasurementMonitor"
        SUFFIX ".exe"
    )

elseif(UNIX)
    target_compile_definitions(MultiMeasurementMonitor PRIVATE
        LINUX
    )

    target_link_libraries(MultiMeasurementMonitor PRIVATE
        m
    )
endif()
