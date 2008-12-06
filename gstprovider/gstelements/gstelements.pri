windows {
	# directsound
	HEADERS += \
		$$PWD/directsound/gstdirectsound.h \
		$$PWD/directsound/gstdirectsoundsrc.h \
		$$PWD/directsound/gstdirectsoundsink.h

	SOURCES += \
		$$PWD/directsound/gstdirectsound.c \
		$$PWD/directsound/gstdirectsoundsrc.c \
		$$PWD/directsound/gstdirectsoundsink.c \
		$$PWD/directsound/gstdirectsoundplugin.c

	LIBS *= \
		-lgstinterfaces-0.10 \
		-lgstaudio-0.10

	# winks
	HEADERS += \
		$$PWD/winks/kshelpers.h \
		$$PWD/winks/ksvideohelpers.h \
		$$PWD/winks/gstksclock.h \
		$$PWD/winks/gstksvideodevice.h \
		$$PWD/winks/gstksvideosrc.h

	SOURCES += \
		$$PWD/winks/kshelpers.c \
		$$PWD/winks/ksvideohelpers.c \
		$$PWD/winks/gstksclock.c \
		$$PWD/winks/gstksvideodevice.c \
		$$PWD/winks/gstksvideosrc.c
}
