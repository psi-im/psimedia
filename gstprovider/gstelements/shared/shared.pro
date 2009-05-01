TEMPLATE = subdirs

SUBDIRS += rtpmanager videomaxrate liveadder audioresample speexdsp
windows:SUBDIRS += directsound winks
mac:SUBDIRS += osxaudio osxvideo
