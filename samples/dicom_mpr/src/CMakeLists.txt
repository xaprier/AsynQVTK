cmake_minimum_required(VERSION 3.20)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/app/include/app/Version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app/include/app/Version.hpp"
    @ONLY
)


file (GLOB_RECURSE CPP_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/adapters/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/controllers/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/overlays/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/render/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/src/*.cpp"
)

file (GLOB_RECURSE HPP_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/adapters/include/adapters/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/include/app/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/controllers/include/controllers/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/overlays/include/overlays/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/render/include/render/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/include/ui/*.hpp"
)

file (GLOB_RECURSE QRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/resources/*.qrc"
)

add_executable(${PROJECT_NAME}
    ${CPP_FILES}
    ${HPP_FILES}
    ${QRC_FILES}
)

if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/assets/icon.rc")
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/adapters/include
        ${CMAKE_CURRENT_SOURCE_DIR}/app/include
        ${CMAKE_CURRENT_BINARY_DIR}/app/include
        ${CMAKE_CURRENT_SOURCE_DIR}/controllers/include
        ${CMAKE_CURRENT_SOURCE_DIR}/overlays/include
        ${CMAKE_CURRENT_SOURCE_DIR}/render/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/include
        ${VTK_DICOM_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        XQVtkViewport
        AsynQVTK
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        ${VTK_DICOM_LIBRARIES}
        ${VTK_LIBRARIES}
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/${PROJECT_NAME}.desktop"
        DESTINATION share/applications
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/${PROJECT_NAME}.png"
        DESTINATION share/icons/hicolor/1024x1024/apps
    )
endif()
