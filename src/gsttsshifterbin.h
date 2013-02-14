/* GStreamer MPEG TS Time Shifting
 * Copyright (C) 2013 YouView TV Ltd. <krzysztof.konopko@youview.com>
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

#ifndef __GST_TS_SHIFTERBIN_H__
#define __GST_TS_SHIFTERBIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_TS_SHIFTER_BIN \
  (gst_ts_shifter_bin_get_type())
#define GST_TS_SHIFTER_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TS_SHIFTER_BIN,GstTSShifterBin))
#define GST_TS_SHIFTER_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TS_SHIFTER_BIN,GstTSShifterBinClass))
#define GST_IS_TS_SHIFTER_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TS_SHIFTER_BIN))
#define GST_IS_TS_SHIFTER_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TS_SHIFTER_BIN))
#define GST_TS_SHIFTER_BIN_CAST(obj) ((GstTSShifterBin *) (obj))
typedef struct _GstTSShifterBin GstTSShifterBin;
typedef struct _GstTSShifterBinClass GstTSShifterBinClass;

struct _GstTSShifterBin
{
  GstBin parent_instance;

  GstElement *parser;
  GstElement *indexer;
  GstElement *timeshifter;
  GstElement *seeker;
};

struct _GstTSShifterBinClass
{
  GstBinClass parent_class;
};

GType gst_ts_shifter_bin_get_type (void);

G_END_DECLS
#endif /* __GST_TS_SHIFTER_H__ */
