TEMPLATE = subdirs

SUBDIRS += rtpmanager videomaxrate liveadder speexdsp
windows:SUBDIRS += directsound winks
mac:SUBDIRS += osxaudio osxvideo
