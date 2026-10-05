cmake_minimum_required(VERSION 3.16)

project(metro-lcd
    VERSION 2.0
    DESCRIPTION "Metro door-top LCD display simulation (4:1 strip, Qt Widgets)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt6 优先，找不到退回 Qt5
find_package(Qt6 COMPONENTS Widgets QUIET)
if (NOT Qt6_FOUND)
    find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets)
endif()

add_executable(metro-lcd WIN32
    src/main.cpp
    src/TrainScreen.cpp
    src/TrainScreen.h
    src/SoundCue.cpp
    src/SoundCue.h
)

target_include_directories(metro-lcd PRIVATE src)

if (Qt6_FOUND)
    target_link_libraries(metro-lcd PRIVATE Qt6::Widgets)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
else()
    target_link_libraries(metro-lcd PRIVATE Qt5::Widgets)
    get_target_property(_qmake_exe Qt5::qmake IMPORTED_LOCATION)
endif()

# 提示音播放用 Windows 自带的 winmm（PlaySound），不需要额外的 Qt 模块
if (WIN32)
    target_link_libraries(metro-lcd PRIVATE winmm)
endif()

# 编译完自动拷 Qt 运行库 —— 这样在 Visual Studio 里按 F5 能直接跑起来
if (WIN32)
    get_filename_component(_qt_bin "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin}")
    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET metro-lcd POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:metro-lcd>"
            COMMENT "windeployqt: 拷贝 Qt 运行库（保证 F5 直接能跑）")
    else()
        message(WARNING "没找到 windeployqt，F5 运行时可能需要手动把 ${_qt_bin} 加进 PATH")
    endif()

    # 把音频文件拷到 exe 旁边（程序运行时从 <exe目录>/assets/voice 找）
    add_custom_command(TARGET metro-lcd POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:metro-lcd>/assets"
        COMMENT "拷贝提示音 assets/ 到输出目录")
endif()
