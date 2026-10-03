set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)
find_package(VTK REQUIRED)

add_executable(sphere_grid main.cpp)

target_include_directories(sphere_grid PRIVATE ${VTK_INCLUDE_DIRS})

target_link_libraries(sphere_grid PRIVATE
    AsynQVTK::AsynQVTK
    Qt5::Widgets
    ${VTK_LIBRARIES}
    -lpthread
)
