TEMPLATE = subdirs

SUBDIRS += videomaxrate liveadder speexdsp
windows:SUBDIRS += directsound winks
mac:SUBDIRS += osxaudio osxvideo
