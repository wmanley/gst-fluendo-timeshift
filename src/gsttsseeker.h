/* GStreamer MPEG TS Time Shifting
 * Copyright (C) 2013 YouView TV Ltd. <william.manley@youview.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_TS_SEEKER_H_
#define _GST_TS_SEEKER_H_

#include <gst/base/gstbasetransform.h>
#include "tsindex.h"

G_BEGIN_DECLS
#define GST_TYPE_TS_SEEKER   (gst_ts_seeker_get_type())
#define GST_TS_SEEKER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TS_SEEKER,GstTSSeeker))
#define GST_TS_SEEKER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TS_SEEKER,GstTSSeekerClass))
#define GST_IS_TS_SEEKER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TS_SEEKER))
#define GST_IS_TS_SEEKER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TS_SEEKER))
typedef struct _GstTSSeeker GstTSSeeker;
typedef struct _GstTSSeekerClass GstTSSeekerClass;

struct _GstTSSeeker
{
  GstBaseTransform base_timeshiftseeker;

  /* Generated Index */
  GstIndex *index;

  gboolean timestamp_next_buffer;
};

struct _GstTSSeekerClass
{
  GstBaseTransformClass base_timeshiftseeker_class;
};

GType gst_ts_seeker_get_type (void);

G_END_DECLS
#endif
