/*
 * Copyright (C) 2008  Barracuda Networks, Inc.
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

#ifndef __GST_VIDEOMAXRATE_H__
#define __GST_VIDEOMAXRATE_H__

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEOMAXRATE (gst_videomaxrate_get_type())
#define GST_VIDEOMAXRATE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_VIDEOMAXRATE, GstVideoMaxRate))
#define GST_VIDEOMAXRATE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VIDEOMAXRATE, GstVideoMaxRateClass))
#define GST_IS_VIDEOMAXRATE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VIDEOMAXRATE))
#define GST_IS_VIDEOMAXRATE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VIDEOMAXRATE))

typedef struct _GstVideoMaxRate      GstVideoMaxRate;
typedef struct _GstVideoMaxRateClass GstVideoMaxRateClass;

struct _GstVideoMaxRate {
    GstBaseTransform parent;

    gint to_rate_numerator;
    gint to_rate_denominator;

    gboolean     have_last_ts;
    GstClockTime last_ts;
};

struct _GstVideoMaxRateClass {
    GstBaseTransformClass parent_class;
};

GType gst_videomaxrate_get_type(void);

G_END_DECLS

#endif /* __GST_VIDEOMAXRATE_H__ */
