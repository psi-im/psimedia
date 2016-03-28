if(NOT QT4_BUILD)
  find_package(Qt5Core QUIET)
  find_package(Qt5Widgets QUIET)
endif(NOT QT4_BUILD)

if(Qt5Core_FOUND)
  message("Qt5 found")

  include_directories(${Qt5Core_INCLUDE_DIRS})
  include_directories(${Qt5Widgets_INCLUDE_DIRS})
  add_definitions(${Qt5Core_DEFINITIONS})
  add_definitions(${Qt5Widgets_DEFINITIONS})
else()
  message("Qt5 not found, searching for Qt4")
  # Find Qt4
  find_package(Qt4 REQUIRED QtCore QtGui)

  # Include the cmake file needed to use qt4
  include(${QT_USE_FILE})
endif()

# Tell CMake to run moc when necessary:
set(CMAKE_AUTOMOC ON)
# As moc files are generated in the binary dir, tell CMake
# to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)

pkg_search_module(video REQUIRED gstreamer-video-0.10)
pkg_search_module(interfaces REQUIRED gstreamer-interfaces-0.10)

set(SOURCES
	gstelements/static/gstelements.c
	gstcustomelements/gstcustomelements.c
	gstcustomelements/appvideosink.c
	gstcustomelements/apprtpsrc.c
	gstcustomelements/apprtpsink.c
	devices.cpp
	modes.cpp
	payloadinfo.cpp
	pipeline.cpp
	bins.cpp
	rtpworker.cpp
	gstthread.cpp
	rwcontrol.cpp
	gstprovider.cpp)

if(WIN32)
  list(APPEND SOURCES deviceenum/deviceenum_win.cpp)
endif()

if(APPLE)
  list(APPEND SOURCES deviceenum/deviceenum_mac.cpp)
endif()

if(UNIX AND NOT APPLE)
  list(APPEND SOURCES deviceenum/deviceenum_unix.cpp)
endif()

include_directories(${video_INCLUDE_DIRS} ${interfaces_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/psimedia)

add_library(gstprovider MODULE ${PARTS} ${SOURCES})
target_link_libraries(${PROJECT_NAME} ${video_LDFLAGS} ${interfaces_LDFLAGS} ${PARTS_LDFLAGS})

if(APPLE)
  target_link_libraries(${PROJECT_NAME} "-framework CoreAudio")
  set_property(TARGET ${PROJECT_NAME}  PROPERTY SUFFIX ".dylib")
endif()

if(Qt5Core_FOUND)
  target_link_libraries(${PROJECT_NAME} ${Qt5Core_LIBRARIES} ${Qt5Gui_LIBRARIES} ${Qt5Widgets_LIBRARIES})
else()
  target_link_libraries(${PROJECT_NAME} ${QT_LIBRARIES})
endif()
