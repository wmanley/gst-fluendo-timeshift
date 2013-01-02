/* GStreamer
 * Copyright (C) 2013 FIXME <fixme@example.com>
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

#ifndef _GST_TIME_SHIFT_SEEKER_H_
#define _GST_TIME_SHIFT_SEEKER_H_

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_TIME_SHIFT_SEEKER   (gst_time_shift_seeker_get_type())
#define GST_TIME_SHIFT_SEEKER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TIME_SHIFT_SEEKER,GstTimeShiftSeeker))
#define GST_TIME_SHIFT_SEEKER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TIME_SHIFT_SEEKER,GstTimeShiftSeekerClass))
#define GST_IS_TIME_SHIFT_SEEKER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TIME_SHIFT_SEEKER))
#define GST_IS_TIME_SHIFT_SEEKER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TIME_SHIFT_SEEKER))

typedef struct _GstTimeShiftSeeker GstTimeShiftSeeker;
typedef struct _GstTimeShiftSeekerClass GstTimeShiftSeekerClass;

struct _GstTimeShiftSeeker
{
  GstBaseTransform base_timeshiftseeker;
};

struct _GstTimeShiftSeekerClass
{
  GstBaseTransformClass base_timeshiftseeker_class;
};

GType gst_time_shift_seeker_get_type (void);

G_END_DECLS

#endif
