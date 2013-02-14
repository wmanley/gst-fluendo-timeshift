/* GStreamer
 * Copyright (C) 2011 Fluendo S.A. <support@fluendo.com>
 * Copyright (C) 2013 YouView TV Ltd. <will@williammanley.net>
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

#ifndef _GST_TIME_SHIFT_TS_INDEXER_H_
#define _GST_TIME_SHIFT_TS_INDEXER_H_

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_TIME_SHIFT_TS_INDEXER   (gst_time_shift_ts_indexer_get_type())
#define GST_TIME_SHIFT_TS_INDEXER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TIME_SHIFT_TS_INDEXER,GstTimeShiftTsIndexer))
#define GST_TIME_SHIFT_TS_INDEXER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TIME_SHIFT_TS_INDEXER,GstTimeShiftTsIndexerClass))
#define GST_IS_TIME_SHIFT_TS_INDEXER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TIME_SHIFT_TS_INDEXER))
#define GST_IS_TIME_SHIFT_TS_INDEXER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TIME_SHIFT_TS_INDEXER))

typedef struct _GstTimeShiftTsIndexer GstTimeShiftTsIndexer;
typedef struct _GstTimeShiftTsIndexerClass GstTimeShiftTsIndexerClass;

struct _GstTimeShiftTsIndexer
{
  GstBaseTransform base_timeshifttsindexer;

  /* Properties */
  gint16 pcr_pid;

  /* PCR tracking */
  GstClockTime base_time;
  GstClockTime last_time;
};

struct _GstTimeShiftTsIndexerClass
{
  GstBaseTransformClass base_timeshifttsindexer_class;
};

GType gst_time_shift_ts_indexer_get_type (void);

G_END_DECLS

#endif
