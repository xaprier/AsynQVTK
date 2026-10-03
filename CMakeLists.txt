cmake_minimum_required(VERSION 3.5...3.10)

project(AsynQVTK)

set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Ofast")
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_WIN32_THREADS_INIT 0)
set(CMAKE_USE_PTHREADS_INIT 1)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
# NOTE: this sample was built and verified against VTK 8.2.0. Newer VTK
# versions changed the OpenGL render window / interactor APIs used here
# (vtkGenericOpenGLRenderWindow, QVTKInteractor), so later versions may
# require adjustments to AsyncRenderWorker.
find_package(VTK REQUIRED)

include_directories(
    ${VTK_INCLUDE_DIRS}
    "./"
)

file(GLOB HPP_FILES *.hpp)
file(GLOB CXX_FILES *.cpp)

add_executable(${PROJECT_NAME} ${HPP_FILES} ${CXX_FILES})

target_link_libraries(${PROJECT_NAME}
    Qt5::Core
    Qt5::Widgets
    ${VTK_LIBRARIES}
    -lpthread
)
