/*
 * Copyright (C) 2009  Barracuda Networks, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 *
 */

#ifndef PSI_PIPELINE_H
#define PSI_PIPELINE_H

#include "psimediaprovider.h"
#include <QString>
#include <gst/gstelement.h>

namespace PsiMedia {

class PipelineDeviceContext;
class PipelineDeviceContextPrivate;
class DeviceMonitor;

class PipelineContext {
public:
    PipelineContext();
    ~PipelineContext();

    PipelineContext(const PipelineContext &)            = delete;
    PipelineContext &operator=(const PipelineContext &) = delete;

    // set the pipeline to playing (activate) or to null (deactivate)
    // FIXME: when we make dynamic pipelines work, we can remove these
    //   functions.
    void activate();
    void deactivate();

    GstElement *element();

private:
    friend class PipelineDeviceContext;
    friend class PipelineDeviceContextPrivate;

    class Private;
    Private *d;
};

// this is for hinting video input properties.  the actual video quality may
//   end up being something else, but in general it will try to be the closest
//   possible quality to satisfy the maximum hinted of all the references.
//   thus, if one ref is made, set to 640x480, and another ref is made, set to
//   320x240, the quality generated by the device (and therefore, both refs)
//   will probably be 640x480.
class PipelineDeviceOptions {
public:
    QSize   videoSize;
    int     fps = -1;
    bool    aec = false; // echo cancellation (will be enabled when prober is available)
    QString echoProberName;
};

class PipelineDeviceContext {
public:
    static PipelineDeviceContext *create(PipelineContext *pipeline, const QString &id, PDevice::Type type,
                                         DeviceMonitor               *deviceMonitor = 0,
                                         const PipelineDeviceOptions &opts          = PipelineDeviceOptions());
    ~PipelineDeviceContext();

    PipelineDeviceContext(const PipelineDeviceContext &)            = delete;
    PipelineDeviceContext &operator=(const PipelineDeviceContext &) = delete;

    // after creation, the device element is in the NULL state, and
    //   potentially not linked to dependent internal elements.  call
    //   activate() to cause internals to be finalized and set to
    //   PLAYING.  the purpose of the activate() call is to give you time
    //   to get your own elements into the pipeline, linked, and perhaps
    //   set to PLAYING before the device starts working.
    //
    // note: this only applies to input (src) elements.  output (sink)
    //   elements start out activated.
    // FIXME: this function currently does nothing
    void activate();

    // call this in order to stop the device element.  it will be safely
    //   set to the NULL state, so that you may then unlink your own
    //   elements from it.
    // FIXME: this function currently does nothing
    void deactivate();

    GstElement           *element();
    void                  setOptions(const PipelineDeviceOptions &opts);
    PipelineDeviceOptions options() const;

private:
    PipelineDeviceContext();

    PipelineDeviceContextPrivate *d;
};

}

#endif
