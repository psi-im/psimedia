include(rtpmanager.pri)

windows {
	include(directsound.pri)
	include(winks.pri)
}

mac {
	include(osxaudio.pri)
	include(osxvideo.pri)
}
