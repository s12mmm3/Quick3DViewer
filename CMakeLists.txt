cmake_minimum_required(VERSION 3.21)
project(Q3DViewer VERSION 1.0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_NO_MISSING_CATALOG_LANGUAGE_WARNING ON)

find_package(Qt6 6.8 COMPONENTS Core Gui Quick QuickControls2 Network Concurrent Qml LinguistTools Quick3D REQUIRED)

if (WIN32)
    if (MSVC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi /EHa")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DEBUG")
        set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
    endif()
    set(VERSIONINFO_RC "${CMAKE_BINARY_DIR}/VersionInfo.rc")
    configure_file("${CMAKE_SOURCE_DIR}/VersionInfo.rc.in" "${VERSIONINFO_RC}")
elseif (APPLE)
    if (IOS)
        # Building for iOS
    elseif (UNIX)
        # Building for macOS
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
    else()
    endif()
elseif (EMSCRIPTEN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread -s USE_PTHREADS=1 -s SHARED_MEMORY=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -s USE_PTHREADS=1 -s SHARED_MEMORY=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread -s USE_PTHREADS=1 -s SHARED_MEMORY=1")
endif()

qt_standard_project_setup(REQUIRES 6.8)

FILE(GLOB SOURCES "./*.h" "./*.cpp" "./*.qrc" "manager/*" "task/*" "app/*" "mesh/*")
qt_add_executable(${PROJECT_NAME} WIN32 MACOSX_BUNDLE
    ${SOURCES}
    "logo.icns"
)

if(EMSCRIPTEN)
    target_link_options(${PROJECT_NAME} PUBLIC -sASYNCIFY -Os)
endif()


file(GLOB_RECURSE QML_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "qmls/*" "qqr.js/*")
set(QML_FILES ${QML_FILES} "Main.qml")

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
    set(FONTS
        "fonts/HarmonyOS_Sans_SC_Medium.ttf"
    )
else()
    set(FONTS)
endif()
file(GLOB_RECURSE ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "icons/*")
qt_add_qml_module(${PROJECT_NAME}
    URI main
    NO_RESOURCE_TARGET_PATH
    VERSION 1.0
    QML_FILES ${QML_FILES}#[[ ${QML_SINGLETONS}]]
    RESOURCES ${ICONS} "qtquickcontrols2.conf" ${FONTS}
)

if (ANDROID)
    # set(QT_ANDROID_BUILD_ALL_ABIS ON)
    set_target_properties(${PROJECT_NAME}
        PROPERTIES
        QT_ANDROID_PACKAGE_NAME com.overwriter.${PROJECT_NAME}
        # QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/android"
        # QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
    )

    # include(FetchContent)
    # FetchContent_Declare(
    #     android_openssl
    #     DOWNLOAD_EXTRACT_TIMESTAMP true
    #     URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip
    #     )
    # FetchContent_MakeAvailable(android_openssl)
    # include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)

    # add_android_openssl_libraries(${PROJECT_NAME})
else()

endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROJECT_NAME="${PROJECT_NAME}"
    QT_MESSAGELOGCONTEXT
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Qt6::Quick
    Qt6::Quick3D
    Qt6::Gui
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Concurrent
    Qt6::Qml
)

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/logo.icns
    PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
)

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/logo.icns
    PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.overwriter.${PROJECT_NAME}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE_ICON_FILE "logo.icns"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION  .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET ${PROJECT_NAME}
    OUTPUT_SCRIPT deploy_script
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
install(SCRIPT ${deploy_script})
