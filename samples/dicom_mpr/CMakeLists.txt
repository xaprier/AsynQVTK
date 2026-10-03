cmake_minimum_required(VERSION 3.20)
project(
    AsynQtMprViewer
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "An open-source MPR DICOM viewer built with modern C++ architecture, Qt6, and VTK rendering."
    HOMEPAGE_URL "https://github.com/xaprier/AsynQVTK"
)

set(PROJECT_ORGANIZATION "xaprier")
set(PROJECT_DOCUMENTATION "https://xaprier.dev/AsynQVTK")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)

# MPI must be found before VTK so VTK::mpi can resolve MPI::MPI_C
find_package(MPI QUIET)

# VTK 8.2 does not support component-based find_package() calls, 
# so we need to use the old-style find_package() call for VTK 8.2 and earlier.
find_package(VTK REQUIRED)

find_package(DICOM QUIET)
if(NOT DICOM_FOUND)
    message(FATAL_ERROR "VTK-DICOM not found.")
endif()
if(DICOM_USE_FILE)
    include(${DICOM_USE_FILE})
endif()
set(VTK_DICOM_LIBRARIES vtkDICOM-${VTK_VERSION_MAJOR}.${VTK_VERSION_MINOR})
if(DEFINED VTK_DICOM_ROOT AND EXISTS "${VTK_DICOM_ROOT}/include/vtk-${VTK_VERSION_MAJOR}.${VTK_VERSION_MINOR}")
    list(APPEND VTK_DICOM_INCLUDE_DIRS "${VTK_DICOM_ROOT}/include/vtk-${VTK_VERSION_MAJOR}.${VTK_VERSION_MINOR}")
endif()
link_directories(/usr/local/lib)
if(DEFINED VTK_DICOM_ROOT AND EXISTS "${VTK_DICOM_ROOT}/lib")
    link_directories("${VTK_DICOM_ROOT}/lib")
endif()

add_subdirectory(lib)
add_subdirectory(src)

# compile_commands.json symlink to project root
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(link_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Linking compile_commands.json to project root"
    )
endif()
