TEMPLATE = subdirs

SUBDIRS += rtpmanager videomaxrate speexdsp
windows:SUBDIRS += directsound winks
mac:SUBDIRS += osxaudio osxvideo
