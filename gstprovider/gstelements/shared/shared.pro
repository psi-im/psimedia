TEMPLATE = subdirs

SUBDIRS += rtpmanager
windows:SUBDIRS += directsound winks
mac:SUBDIRS += osxaudio osxvideo
