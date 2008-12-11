TEMPLATE = subdirs

SUBDIRS += rtpmanager videomaxrate
windows:SUBDIRS += directsound winks
mac:SUBDIRS += osxaudio osxvideo
