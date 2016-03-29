project(psimedia-demo)

add_definitions(-DDEBUG_POSTFIX=\"\")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(APPLE)
    add_definitions(-DDEBUG_POSTFIX=\"_debug\")
  elseif(WIN32)
    add_definitions(-DDEBUG_POSTFIX=\"d\")
  endif()
endif()

if(NOT QT4_BUILD)
  find_package(Qt5Core QUIET)
  find_package(Qt5Gui QUIET)
  find_package(Qt5Widgets QUIET)
  find_package(Qt5Network QUIET)
endif()

if(Qt5Core_FOUND)
  include_directories(${Qt5Core_INCLUDE_DIRS})
  include_directories(${Qt5Gui_INCLUDE_DIRS})
  include_directories(${Qt5Widgets_INCLUDE_DIRS})
  include_directories(${Qt5Network_INCLUDE_DIRS})
  add_definitions(${Qt5Core_DEFINITIONS})
  add_definitions(${Qt5Gui_DEFINITIONS})
  add_definitions(${Qt5Widgets_DEFINITIONS})
  add_definitions(${Qt5Network_DEFINITIONS})
else()
  # Find Qt4
  find_package(Qt4 REQUIRED QtCore QtGui QtNetwork)

  # Include the cmake file needed to use qt4
  include(${QT_USE_FILE})
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../psimedia)

set(HEADERS
    main.h
    ../psimedia/psimedia.h
    ../psimedia/psimedia_p.h
    ../psimedia/psimediaprovider.h
)

set(SOURCES main.cpp ../psimedia/psimedia.cpp)
set(FORMS config.ui mainwin.ui)

if(Qt5Core_FOUND)
    qt5_wrap_cpp(MOC_SOURCES ${HEADERS})
    qt5_wrap_ui(UI_SOURCES ${FORMS})
else()
    qt4_wrap_cpp(MOC_SOURCES ${HEADERS})
    qt4_wrap_ui(UI_SOURCES ${FORMS})
endif()

add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${UI_SOURCES} ${MOC_SOURCES})

if(Qt5Core_FOUND)
  target_link_libraries(${PROJECT_NAME} ${Qt5Core_LIBRARIES} ${Qt5Gui_LIBRARIES} ${Qt5Widgets_LIBRARIES} ${Qt5Network_LIBRARIES})
else()
  target_link_libraries(${PROJECT_NAME} ${QT_LIBRARIES})
endif()

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${BIN_INSTALL_DIR})
