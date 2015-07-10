cmake_minimum_required(VERSION 2.8.11)

project(light_client)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)
# Instruct CMake to run rcc automatically when needed.
set(CMAKE_AUTORCC ON)

find_package(Qt5Core)
find_package(Qt5Widgets)
find_package(Qt5Quick)

file(GLOB QML qml/*)
add_executable(light_client ClientDataModel.cpp ClientDataModel.hpp main.cpp ${QML})

target_link_libraries(light_client PRIVATE Qt5::Core Qt5::Widgets Qt5::Quick graphene_chain graphene_utilities fc)
