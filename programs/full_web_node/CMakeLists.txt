cmake_minimum_required(VERSION 2.8.11)

project(full_web_node)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

find_package(Qt5Core)
find_package(Qt5Quick)
find_package(Qt5WebEngine)

file(GLOB QML qml/*)
qt5_add_resources(QML_QRC qml/qml.qrc)

add_executable(full_web_node BlockChain.cpp main.cpp ${QML_QRC} ${QML})

target_link_libraries(full_web_node PRIVATE Qt5::Core Qt5::Quick Qt5::WebEngine
   graphene_chain graphene_egenesis_full graphene_utilities fc graphene_account_history graphene_market_history graphene_app )

install( TARGETS
   full_web_node
   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
