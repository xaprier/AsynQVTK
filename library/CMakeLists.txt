set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
# NOTE: this library was built and verified against VTK 8.2.0. Newer VTK
# versions changed the OpenGL render window / interactor APIs used here
# (vtkGenericOpenGLRenderWindow, QVTKInteractor), so later versions may
# require adjustments to AsyncRenderWorker.
find_package(VTK REQUIRED)

add_library(AsynQVTK STATIC
    src/AsyncRenderView.cpp
    src/AsyncRenderWorker.cpp
    include/AsynQVTK/AsyncRenderView.hpp
    include/AsynQVTK/AsyncRenderWorker.hpp
)
add_library(AsynQVTK::AsynQVTK ALIAS AsynQVTK)

target_include_directories(AsynQVTK PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(AsynQVTK PRIVATE
    ${VTK_INCLUDE_DIRS}
)

target_link_libraries(AsynQVTK PUBLIC
    Qt5::Core
    Qt5::Widgets
)
target_link_libraries(AsynQVTK PRIVATE
    ${VTK_LIBRARIES}
)
