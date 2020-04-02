/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2003,2004 David A. Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/* Element-Checklist-Version: 5 */

/**
 * SECTION:element-legacyresample
 *
 * legacyresample resamples raw audio buffers to different sample rates using
 * a configurable windowing function to enhance quality.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! legacyresample ! audio/x-raw-int,
 * rate=8000 ! alsasink
 * ]| Decode an Ogg/Vorbis downsample to 8Khz and play sound through alsa.
 * To create the Ogg/Vorbis file refer to the documentation of vorbisenc.
 * </refsect2>
 *
 * Last reviewed on 2006-03-02 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

/*#define DEBUG_ENABLED */
#include "../audioresample/gstaudioresample.h"
#include <gst/audio/audio.h>
#include <gst/base/gstbasetransform.h>

GST_DEBUG_CATEGORY_STATIC(audioresample_debug);
#define GST_CAT_DEFAULT audioresample_debug

/* elementfactory information */
static const GstElementDetails gst_audioresample_details
    = GST_ELEMENT_DETAILS("Audio scaler", "Filter/Converter/Audio", "Resample audio", "David Schleef <ds@schleef.org>");

#define DEFAULT_FILTERLEN 16

enum { PROP_0, PROP_FILTERLEN };

#define SUPPORTED_CAPS                                                                                                 \
    GST_STATIC_CAPS("audio/x-raw-int, "                                                                                \
                    "rate = (int) [ 1, MAX ], "                                                                        \
                    "channels = (int) [ 1, MAX ], "                                                                    \
                    "endianness = (int) BYTE_ORDER, "                                                                  \
                    "width = (int) 16, "                                                                               \
                    "depth = (int) 16, "                                                                               \
                    "signed = (boolean) true;"                                                                         \
                    "audio/x-raw-int, "                                                                                \
                    "rate = (int) [ 1, MAX ], "                                                                        \
                    "channels = (int) [ 1, MAX ], "                                                                    \
                    "endianness = (int) BYTE_ORDER, "                                                                  \
                    "width = (int) 32, "                                                                               \
                    "depth = (int) 32, "                                                                               \
                    "signed = (boolean) true;"                                                                         \
                    "audio/x-raw-float, "                                                                              \
                    "rate = (int) [ 1, MAX ], "                                                                        \
                    "channels = (int) [ 1, MAX ], "                                                                    \
                    "endianness = (int) BYTE_ORDER, "                                                                  \
                    "width = (int) 32; "                                                                               \
                    "audio/x-raw-float, "                                                                              \
                    "rate = (int) [ 1, MAX ], "                                                                        \
                    "channels = (int) [ 1, MAX ], "                                                                    \
                    "endianness = (int) BYTE_ORDER, "                                                                  \
                    "width = (int) 64")

static GstStaticPadTemplate gst_audioresample_sink_template
    = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, SUPPORTED_CAPS);

static GstStaticPadTemplate gst_audioresample_src_template
    = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, SUPPORTED_CAPS);

static void gst_audioresample_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_audioresample_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* vmethods */
static gboolean      audioresample_get_unit_size(GstBaseTransform *base, GstCaps *caps, guint *size);
static GstCaps *     audioresample_transform_caps(GstBaseTransform *base, GstPadDirection direction, GstCaps *caps);
static void          audioresample_fixate_caps(GstBaseTransform *base, GstPadDirection direction, GstCaps *caps,
                                               GstCaps *othercaps);
static gboolean      audioresample_transform_size(GstBaseTransform *trans, GstPadDirection direction, GstCaps *incaps,
                                                  guint insize, GstCaps *outcaps, guint *outsize);
static gboolean      audioresample_set_caps(GstBaseTransform *base, GstCaps *incaps, GstCaps *outcaps);
static GstFlowReturn audioresample_pushthrough(GstAudioresample *audioresample);
static GstFlowReturn audioresample_transform(GstBaseTransform *base, GstBuffer *inbuf, GstBuffer *outbuf);
static gboolean      audioresample_event(GstBaseTransform *base, GstEvent *event);
static gboolean      audioresample_start(GstBaseTransform *base);
static gboolean      audioresample_stop(GstBaseTransform *base);

static gboolean            audioresample_query(GstPad *pad, GstQuery *query);
static const GstQueryType *audioresample_query_type(GstPad *pad);

#define DEBUG_INIT(bla) GST_DEBUG_CATEGORY_INIT(audioresample_debug, "legacyresample", 0, "audio resampling element");

GST_BOILERPLATE_FULL(GstAudioresample, gst_audioresample, GstBaseTransform, GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_audioresample_base_init(gpointer g_class)
{
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(g_class);

    gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&gst_audioresample_src_template));
    gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&gst_audioresample_sink_template));

    gst_element_class_set_details(gstelement_class, &gst_audioresample_details);
}

static void gst_audioresample_class_init(GstAudioresampleClass *klass)
{
    GObjectClass *gobject_class;

    gobject_class = (GObjectClass *)klass;

    gobject_class->set_property = gst_audioresample_set_property;
    gobject_class->get_property = gst_audioresample_get_property;

    g_object_class_install_property(gobject_class, PROP_FILTERLEN,
                                    g_param_spec_int("filter-length", "filter length", "Length of the resample filter",
                                                     0, G_MAXINT, DEFAULT_FILTERLEN,
                                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

    GST_BASE_TRANSFORM_CLASS(klass)->start          = GST_DEBUG_FUNCPTR(audioresample_start);
    GST_BASE_TRANSFORM_CLASS(klass)->stop           = GST_DEBUG_FUNCPTR(audioresample_stop);
    GST_BASE_TRANSFORM_CLASS(klass)->transform_size = GST_DEBUG_FUNCPTR(audioresample_transform_size);
    GST_BASE_TRANSFORM_CLASS(klass)->get_unit_size  = GST_DEBUG_FUNCPTR(audioresample_get_unit_size);
    GST_BASE_TRANSFORM_CLASS(klass)->transform_caps = GST_DEBUG_FUNCPTR(audioresample_transform_caps);
    GST_BASE_TRANSFORM_CLASS(klass)->fixate_caps    = GST_DEBUG_FUNCPTR(audioresample_fixate_caps);
    GST_BASE_TRANSFORM_CLASS(klass)->set_caps       = GST_DEBUG_FUNCPTR(audioresample_set_caps);
    GST_BASE_TRANSFORM_CLASS(klass)->transform      = GST_DEBUG_FUNCPTR(audioresample_transform);
    GST_BASE_TRANSFORM_CLASS(klass)->event          = GST_DEBUG_FUNCPTR(audioresample_event);

    GST_BASE_TRANSFORM_CLASS(klass)->passthrough_on_same_caps = TRUE;
}

static void gst_audioresample_init(GstAudioresample *audioresample, GstAudioresampleClass *klass)
{
    GstBaseTransform *trans;

    trans = GST_BASE_TRANSFORM(audioresample);

    /* buffer alloc passthrough is too impossible. FIXME, it
     * is trivial in the passthrough case. */
    gst_pad_set_bufferalloc_function(trans->sinkpad, NULL);

    audioresample->filter_length = DEFAULT_FILTERLEN;

    audioresample->need_discont = FALSE;

    gst_pad_set_query_function(trans->srcpad, audioresample_query);
    gst_pad_set_query_type_function(trans->srcpad, audioresample_query_type);
}

/* vmethods */
static gboolean audioresample_start(GstBaseTransform *base)
{
    GstAudioresample *audioresample = GST_AUDIORESAMPLE(base);

    audioresample->resample  = resample_new();
    audioresample->ts_offset = -1;
    audioresample->offset    = -1;
    audioresample->next_ts   = -1;

    resample_set_filter_length(audioresample->resample, audioresample->filter_length);

    return TRUE;
}

static gboolean audioresample_stop(GstBaseTransform *base)
{
    GstAudioresample *audioresample = GST_AUDIORESAMPLE(base);

    if (audioresample->resample) {
        resample_free(audioresample->resample);
        audioresample->resample = NULL;
    }

    gst_caps_replace(&audioresample->sinkcaps, NULL);
    gst_caps_replace(&audioresample->srccaps, NULL);

    return TRUE;
}

static gboolean audioresample_get_unit_size(GstBaseTransform *base, GstCaps *caps, guint *size)
{
    gint          width, channels;
    GstStructure *structure;
    gboolean      ret;

    g_assert(size);

    /* this works for both float and int */
    structure = gst_caps_get_structure(caps, 0);
    ret       = gst_structure_get_int(structure, "width", &width);
    ret &= gst_structure_get_int(structure, "channels", &channels);
    g_return_val_if_fail(ret, FALSE);

    *size = width * channels / 8;

    return TRUE;
}

static GstCaps *audioresample_transform_caps(GstBaseTransform *base, GstPadDirection direction, GstCaps *caps)
{
    GstCaps *     res;
    GstStructure *structure;

    /* transform caps gives one single caps so we can just replace
     * the rate property with our range. */
    res       = gst_caps_copy(caps);
    structure = gst_caps_get_structure(res, 0);
    gst_structure_set(structure, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    return res;
}

/* Fixate rate to the allowed rate that has the smallest difference */
static void audioresample_fixate_caps(GstBaseTransform *base, GstPadDirection direction, GstCaps *caps,
                                      GstCaps *othercaps)
{
    GstStructure *s;
    gint          rate;

    s = gst_caps_get_structure(caps, 0);
    if (!gst_structure_get_int(s, "rate", &rate))
        return;

    s = gst_caps_get_structure(othercaps, 0);
    gst_structure_fixate_field_nearest_int(s, "rate", rate);
}

static gboolean resample_set_state_from_caps(ResampleState *state, GstCaps *incaps, GstCaps *outcaps, gint *channels,
                                             gint *inrate, gint *outrate)
{
    GstStructure * structure;
    gboolean       ret;
    gint           myinrate, myoutrate;
    int            mychannels;
    gint           width, depth;
    ResampleFormat format;

    GST_DEBUG("incaps %" GST_PTR_FORMAT ", outcaps %" GST_PTR_FORMAT, incaps, outcaps);

    structure = gst_caps_get_structure(incaps, 0);

    /* get width */
    ret = gst_structure_get_int(structure, "width", &width);
    if (!ret)
        goto no_width;

    /* figure out the format */
    if (g_str_equal(gst_structure_get_name(structure), "audio/x-raw-float")) {
        if (width == 32)
            format = RESAMPLE_FORMAT_F32;
        else if (width == 64)
            format = RESAMPLE_FORMAT_F64;
        else
            goto wrong_depth;
    } else {
        /* for int, depth and width must be the same */
        ret = gst_structure_get_int(structure, "depth", &depth);
        if (!ret || width != depth)
            goto not_equal;

        if (width == 16)
            format = RESAMPLE_FORMAT_S16;
        else if (width == 32)
            format = RESAMPLE_FORMAT_S32;
        else
            goto wrong_depth;
    }
    ret = gst_structure_get_int(structure, "rate", &myinrate);
    ret &= gst_structure_get_int(structure, "channels", &mychannels);
    if (!ret)
        goto no_in_rate_channels;

    structure = gst_caps_get_structure(outcaps, 0);
    ret       = gst_structure_get_int(structure, "rate", &myoutrate);
    if (!ret)
        goto no_out_rate;

    if (channels)
        *channels = mychannels;
    if (inrate)
        *inrate = myinrate;
    if (outrate)
        *outrate = myoutrate;

    resample_set_format(state, format);
    resample_set_n_channels(state, mychannels);
    resample_set_input_rate(state, myinrate);
    resample_set_output_rate(state, myoutrate);

    return TRUE;

    /* ERRORS */
no_width : {
    GST_DEBUG("failed to get width from caps");
    return FALSE;
}
not_equal : {
    GST_DEBUG("width %d and depth %d must be the same", width, depth);
    return FALSE;
}
wrong_depth : {
    GST_DEBUG("unknown depth %d found", depth);
    return FALSE;
}
no_in_rate_channels : {
    GST_DEBUG("could not get input rate and channels");
    return FALSE;
}
no_out_rate : {
    GST_DEBUG("could not get output rate");
    return FALSE;
}
}

static gboolean audioresample_transform_size(GstBaseTransform *base, GstPadDirection direction, GstCaps *caps,
                                             guint size, GstCaps *othercaps, guint *othersize)
{
    GstAudioresample *audioresample = GST_AUDIORESAMPLE(base);
    ResampleState *   state;
    GstCaps *         srccaps, *sinkcaps;
    gboolean          use_internal = FALSE; /* whether we use the internal state */
    gboolean          ret          = TRUE;

    GST_LOG_OBJECT(base, "asked to transform size %d in direction %s", size,
                   direction == GST_PAD_SINK ? "SINK" : "SRC");
    if (direction == GST_PAD_SINK) {
        sinkcaps = caps;
        srccaps  = othercaps;
    } else {
        sinkcaps = othercaps;
        srccaps  = caps;
    }

    /* if the caps are the ones that _set_caps got called with; we can use
     * our own state; otherwise we'll have to create a state */
    if (gst_caps_is_equal(sinkcaps, audioresample->sinkcaps) && gst_caps_is_equal(srccaps, audioresample->srccaps)) {
        use_internal = TRUE;
        state        = audioresample->resample;
    } else {
        GST_DEBUG_OBJECT(audioresample, "caps are not the set caps, creating state");
        state = resample_new();
        resample_set_filter_length(state, audioresample->filter_length);
        resample_set_state_from_caps(state, sinkcaps, srccaps, NULL, NULL, NULL);
    }

    if (direction == GST_PAD_SINK) {
        /* asked to convert size of an incoming buffer */
        *othersize = resample_get_output_size_for_input(state, size);
    } else {
        /* asked to convert size of an outgoing buffer */
        *othersize = resample_get_input_size_for_output(state, size);
    }
    g_assert(*othersize % state->sample_size == 0);

    /* we make room for one extra sample, given that the resampling filter
     * can output an extra one for non-integral i_rate/o_rate */
    GST_LOG_OBJECT(base, "transformed size %d to %d", size, *othersize);

    if (!use_internal) {
        resample_free(state);
    }

    return ret;
}

static gboolean audioresample_set_caps(GstBaseTransform *base, GstCaps *incaps, GstCaps *outcaps)
{
    gboolean          ret;
    gint              inrate, outrate;
    int               channels;
    GstAudioresample *audioresample = GST_AUDIORESAMPLE(base);

    GST_DEBUG_OBJECT(base, "incaps %" GST_PTR_FORMAT ", outcaps %" GST_PTR_FORMAT, incaps, outcaps);

    ret = resample_set_state_from_caps(audioresample->resample, incaps, outcaps, &channels, &inrate, &outrate);

    g_return_val_if_fail(ret, FALSE);

    audioresample->channels = channels;
    GST_DEBUG_OBJECT(audioresample, "set channels to %d", channels);
    audioresample->i_rate = inrate;
    GST_DEBUG_OBJECT(audioresample, "set i_rate to %d", inrate);
    audioresample->o_rate = outrate;
    GST_DEBUG_OBJECT(audioresample, "set o_rate to %d", outrate);

    /* save caps so we can short-circuit in the size_transform if the caps
     * are the same */
    gst_caps_replace(&audioresample->sinkcaps, incaps);
    gst_caps_replace(&audioresample->srccaps, outcaps);

    return TRUE;
}

static gboolean audioresample_event(GstBaseTransform *base, GstEvent *event)
{
    GstAudioresample *audioresample;

    audioresample = GST_AUDIORESAMPLE(base);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_FLUSH_START:
        break;
    case GST_EVENT_FLUSH_STOP:
        if (audioresample->resample)
            resample_input_flush(audioresample->resample);
        audioresample->ts_offset = -1;
        audioresample->next_ts   = -1;
        audioresample->offset    = -1;
        break;
    case GST_EVENT_NEWSEGMENT:
        resample_input_pushthrough(audioresample->resample);
        audioresample_pushthrough(audioresample);
        audioresample->ts_offset = -1;
        audioresample->next_ts   = -1;
        audioresample->offset    = -1;
        break;
    case GST_EVENT_EOS:
        resample_input_eos(audioresample->resample);
        audioresample_pushthrough(audioresample);
        break;
    default:
        break;
    }
    return parent_class->event(base, event);
}

static GstFlowReturn audioresample_do_output(GstAudioresample *audioresample, GstBuffer *outbuf)
{
    int            outsize;
    int            outsamples;
    ResampleState *r;

    r = audioresample->resample;

    outsize = resample_get_output_size(r);
    GST_LOG_OBJECT(audioresample, "audioresample can give me %d bytes", outsize);

    /* protect against mem corruption */
    if (outsize > GST_BUFFER_SIZE(outbuf)) {
        GST_WARNING_OBJECT(audioresample, "overriding audioresample's outsize %d with outbuffer's size %d", outsize,
                           GST_BUFFER_SIZE(outbuf));
        outsize = GST_BUFFER_SIZE(outbuf);
    }
    /* catch possibly wrong size differences */
    if (GST_BUFFER_SIZE(outbuf) - outsize > r->sample_size) {
        GST_WARNING_OBJECT(audioresample, "audioresample's outsize %d too far from outbuffer's size %d", outsize,
                           GST_BUFFER_SIZE(outbuf));
    }

    outsize    = resample_get_output_data(r, GST_BUFFER_DATA(outbuf), outsize);
    outsamples = outsize / r->sample_size;
    GST_LOG_OBJECT(audioresample, "resample gave me %d bytes or %d samples", outsize, outsamples);

    GST_BUFFER_OFFSET(outbuf)    = audioresample->offset;
    GST_BUFFER_TIMESTAMP(outbuf) = audioresample->next_ts;

    if (audioresample->ts_offset != -1) {
        audioresample->offset += outsamples;
        audioresample->ts_offset += outsamples;
        audioresample->next_ts = gst_util_uint64_scale_int(audioresample->ts_offset, GST_SECOND, audioresample->o_rate);
        GST_BUFFER_OFFSET_END(outbuf) = audioresample->offset;

        /* we calculate DURATION as the difference between "next" timestamp
         * and current timestamp so we ensure a contiguous stream, instead of
         * having rounding errors. */
        GST_BUFFER_DURATION(outbuf) = audioresample->next_ts - GST_BUFFER_TIMESTAMP(outbuf);
    } else {
        /* no valid offset know, we can still sortof calculate the duration though */
        GST_BUFFER_DURATION(outbuf) = gst_util_uint64_scale_int(outsamples, GST_SECOND, audioresample->o_rate);
    }

    /* check for possible mem corruption */
    if (outsize > GST_BUFFER_SIZE(outbuf)) {
        /* this is an error that when it happens, would need fixing in the
         * resample library; we told it we wanted only GST_BUFFER_SIZE (outbuf),
         * and it gave us more ! */
        GST_WARNING_OBJECT(audioresample,
                           "audioresample, you memory corrupting bastard. "
                           "you gave me outsize %d while my buffer was size %d",
                           outsize, GST_BUFFER_SIZE(outbuf));
        return GST_FLOW_ERROR;
    }
    /* catch possibly wrong size differences */
    if (GST_BUFFER_SIZE(outbuf) - outsize > r->sample_size) {
        GST_WARNING_OBJECT(audioresample, "audioresample's written outsize %d too far from outbuffer's size %d",
                           outsize, GST_BUFFER_SIZE(outbuf));
    }
    GST_BUFFER_SIZE(outbuf) = outsize;

    if (G_UNLIKELY(audioresample->need_discont)) {
        GST_DEBUG_OBJECT(audioresample, "marking this buffer with the DISCONT flag");
        GST_BUFFER_FLAG_SET(outbuf, GST_BUFFER_FLAG_DISCONT);
        audioresample->need_discont = FALSE;
    }

    GST_LOG_OBJECT(audioresample,
                   "transformed to buffer of %d bytes, ts %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
                   ", offset %" G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
                   outsize, GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(outbuf)), GST_TIME_ARGS(GST_BUFFER_DURATION(outbuf)),
                   GST_BUFFER_OFFSET(outbuf), GST_BUFFER_OFFSET_END(outbuf));

    return GST_FLOW_OK;
}

static gboolean audioresample_check_discont(GstAudioresample *audioresample, GstClockTime timestamp)
{
    if (timestamp != GST_CLOCK_TIME_NONE && audioresample->prev_ts != GST_CLOCK_TIME_NONE
        && audioresample->prev_duration != GST_CLOCK_TIME_NONE
        && timestamp != audioresample->prev_ts + audioresample->prev_duration) {
        /* Potentially a discontinuous buffer. However, it turns out that many
         * elements generate imperfect streams due to rounding errors, so we permit
         * a small error (up to one sample) without triggering a filter
         * flush/restart (if triggered incorrectly, this will be audible) */
        GstClockTimeDiff diff = timestamp - (audioresample->prev_ts + audioresample->prev_duration);

        if (ABS(diff) > GST_SECOND / audioresample->i_rate) {
            GST_WARNING_OBJECT(audioresample, "encountered timestamp discontinuity of %" G_GINT64_FORMAT, diff);
            return TRUE;
        }
    }

    return FALSE;
}

static GstFlowReturn audioresample_transform(GstBaseTransform *base, GstBuffer *inbuf, GstBuffer *outbuf)
{
    GstAudioresample *audioresample;
    ResampleState *   r;
    guchar *          data, *datacopy;
    gulong            size;
    GstClockTime      timestamp;

    audioresample = GST_AUDIORESAMPLE(base);
    r             = audioresample->resample;

    data      = GST_BUFFER_DATA(inbuf);
    size      = GST_BUFFER_SIZE(inbuf);
    timestamp = GST_BUFFER_TIMESTAMP(inbuf);

    GST_LOG_OBJECT(audioresample,
                   "transforming buffer of %ld bytes, ts %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
                   ", offset %" G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
                   size, GST_TIME_ARGS(timestamp), GST_TIME_ARGS(GST_BUFFER_DURATION(inbuf)), GST_BUFFER_OFFSET(inbuf),
                   GST_BUFFER_OFFSET_END(inbuf));

    /* check for timestamp discontinuities and flush/reset if needed */
    if (G_UNLIKELY(audioresample_check_discont(audioresample, timestamp))) {
        /* Flush internal samples */
        audioresample_pushthrough(audioresample);
        /* Inform downstream element about discontinuity */
        audioresample->need_discont = TRUE;
        /* We want to recalculate the offset */
        audioresample->ts_offset = -1;
    }

    if (audioresample->ts_offset == -1) {
        /* if we don't know the initial offset yet, calculate it based on the
         * input timestamp. */
        if (GST_CLOCK_TIME_IS_VALID(timestamp)) {
            GstClockTime stime;

            /* offset used to calculate the timestamps. We use the sample offset for
             * this to make it more accurate. We want the first buffer to have the
             * same timestamp as the incoming timestamp. */
            audioresample->next_ts   = timestamp;
            audioresample->ts_offset = gst_util_uint64_scale_int(timestamp, r->o_rate, GST_SECOND);
            /* offset used to set as the buffer offset, this offset is always
             * relative to the stream time, note that timestamp is not... */
            stime                 = (timestamp - base->segment.start) + base->segment.time;
            audioresample->offset = gst_util_uint64_scale_int(stime, r->o_rate, GST_SECOND);
        }
    }
    audioresample->prev_ts       = timestamp;
    audioresample->prev_duration = GST_BUFFER_DURATION(inbuf);

    /* need to memdup, resample takes ownership. */
    datacopy = g_memdup(data, size);
    resample_add_input_data(r, datacopy, size, g_free, datacopy);

    return audioresample_do_output(audioresample, outbuf);
}

/* push remaining data in the buffers out */
static GstFlowReturn audioresample_pushthrough(GstAudioresample *audioresample)
{
    int               outsize;
    ResampleState *   r;
    GstBuffer *       outbuf;
    GstFlowReturn     res = GST_FLOW_OK;
    GstBaseTransform *trans;

    r = audioresample->resample;

    outsize = resample_get_output_size(r);
    if (outsize == 0) {
        GST_DEBUG_OBJECT(audioresample, "no internal buffers needing flush");
        goto done;
    }

    trans = GST_BASE_TRANSFORM(audioresample);

    res = gst_pad_alloc_buffer(trans->srcpad, GST_BUFFER_OFFSET_NONE, outsize, GST_PAD_CAPS(trans->srcpad), &outbuf);
    if (G_UNLIKELY(res != GST_FLOW_OK)) {
        GST_WARNING_OBJECT(audioresample, "failed allocating buffer of %d bytes", outsize);
        goto done;
    }

    res = audioresample_do_output(audioresample, outbuf);
    if (G_UNLIKELY(res != GST_FLOW_OK))
        goto done;

    res = gst_pad_push(trans->srcpad, outbuf);

done:
    return res;
}

static gboolean audioresample_query(GstPad *pad, GstQuery *query)
{
    GstAudioresample *audioresample = GST_AUDIORESAMPLE(gst_pad_get_parent(pad));
    GstBaseTransform *trans         = GST_BASE_TRANSFORM(audioresample);
    gboolean          res           = TRUE;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY: {
        GstClockTime min, max;
        gboolean     live;
        guint64      latency;
        GstPad *     peer;
        gint         rate              = audioresample->i_rate;
        gint         resampler_latency = audioresample->filter_length / 2;

        if (gst_base_transform_is_passthrough(trans))
            resampler_latency = 0;

        if ((peer = gst_pad_get_peer(trans->sinkpad))) {
            if ((res = gst_pad_query(peer, query))) {
                gst_query_parse_latency(query, &live, &min, &max);

                GST_DEBUG("Peer latency: min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT, GST_TIME_ARGS(min),
                          GST_TIME_ARGS(max));

                /* add our own latency */
                if (rate != 0 && resampler_latency != 0)
                    latency = gst_util_uint64_scale(resampler_latency, GST_SECOND, rate);
                else
                    latency = 0;

                GST_DEBUG("Our latency: %" GST_TIME_FORMAT, GST_TIME_ARGS(latency));

                min += latency;
                if (max != GST_CLOCK_TIME_NONE)
                    max += latency;

                GST_DEBUG("Calculated total latency : min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
                          GST_TIME_ARGS(min), GST_TIME_ARGS(max));

                gst_query_set_latency(query, live, min, max);
            }
            gst_object_unref(peer);
        }
        break;
    }
    default:
        res = gst_pad_query_default(pad, query);
        break;
    }
    gst_object_unref(audioresample);
    return res;
}

static const GstQueryType *audioresample_query_type(GstPad *pad)
{
    static const GstQueryType types[] = { GST_QUERY_LATENCY, 0 };

    return types;
}

static void gst_audioresample_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstAudioresample *audioresample;

    audioresample = GST_AUDIORESAMPLE(object);

    switch (prop_id) {
    case PROP_FILTERLEN:
        audioresample->filter_length = g_value_get_int(value);
        GST_DEBUG_OBJECT(GST_ELEMENT(audioresample), "new filter length %d", audioresample->filter_length);
        if (audioresample->resample) {
            resample_set_filter_length(audioresample->resample, audioresample->filter_length);
            gst_element_post_message(GST_ELEMENT(audioresample), gst_message_new_latency(GST_OBJECT(audioresample)));
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_audioresample_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstAudioresample *audioresample;

    audioresample = GST_AUDIORESAMPLE(object);

    switch (prop_id) {
    case PROP_FILTERLEN:
        g_value_set_int(value, audioresample->filter_length);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean plugin_init(GstPlugin *plugin)
{
    resample_init();

    if (!gst_element_register(plugin, "legacyresample", GST_RANK_MARGINAL, GST_TYPE_AUDIORESAMPLE)) {
        return FALSE;
    }

    return TRUE;
}

void gstelements_audioresample_register()
{
    gst_plugin_register_static(GST_VERSION_MAJOR, GST_VERSION_MINOR, "legacyresample", "Resamples audio", plugin_init,
                               "1.0.4", "LGPL", "my-application", "my-application", "http://www.my-application.net/");
}
