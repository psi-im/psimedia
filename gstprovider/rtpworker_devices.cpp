/*
 * Copyright (C) 2026  Psi IM team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "rtpworker.h"

#include <QMutexLocker>

namespace PsiMedia {

void RtpWorker::setInputDevices(const QString &audioInput, const QString &videoInput, const QString &fileName,
                                const QByteArray &fileData, bool loop)
{
    const bool liveCaptureChanged = infile.isEmpty() && indata.isEmpty() && fileName.isEmpty() && fileData.isEmpty()
        && (ain != audioInput || vin != videoInput);

    bool audioWasTransmitting = false;
    bool videoWasTransmitting = false;
    if (liveCaptureChanged && sendbin) {
        {
            QMutexLocker locker(&rtpaudioout_mutex);
            audioWasTransmitting = rtpaudioout;
        }
        {
            QMutexLocker locker(&rtpvideoout_mutex);
            videoWasTransmitting = rtpvideoout;
        }

        // RtpWorker's legacy pipeline cannot replace a running capture source
        // in place. Rebuild its media pipelines while keeping the surrounding
        // GstRtpSessionContext/RtpSessionBridge alive; Jingle and RFC 3550
        // session state therefore remain outside this reset.
        cleanup();
        localAudioPayloadInfo.clear();
        localVideoPayloadInfo.clear();
        actual_localAudioPayloadInfo.clear();
        actual_localVideoPayloadInfo.clear();
        canTransmitAudio = false;
        canTransmitVideo = false;
    }

    ain      = audioInput;
    vin      = videoInput;
    infile   = fileName;
    indata   = fileData;
    loopFile = loop;

    const bool fileCapture = !fileName.isEmpty() || !fileData.isEmpty();
    if (liveCaptureChanged) {
        QMutexLocker audioLocker(&rtpaudioout_mutex);
        rtpaudioout = audioWasTransmitting && (!audioInput.isEmpty() || fileCapture);
        audioLocker.unlock();

        QMutexLocker videoLocker(&rtpvideoout_mutex);
        rtpvideoout = videoWasTransmitting && (!videoInput.isEmpty() || fileCapture);
    }
}

} // namespace PsiMedia
