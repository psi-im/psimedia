
#windows: {
#	SOURCES += $$PWD/deviceenum_win.cpp
#}
unix:!mac: {
	SOURCES += $$PWD/deviceenum_unix.cpp
}
#mac: {
#	SOURCES += $$PWD/deviceenum_mac.cpp
#	LIBS += -framework CoreAudio
#}
