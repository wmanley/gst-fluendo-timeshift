/* GStreamer
 * Copyright (C) 2012 FIXME <fixme@example.com>
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

#ifndef _GST_MPEGTS_INDEXER_H_
#define _GST_MPEGTS_INDEXER_H_

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_MPEGTS_INDEXER   (gst_mpegts_indexer_get_type())
#define GST_MPEGTS_INDEXER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEGTS_INDEXER,GstMpegtsIndexer))
#define GST_MPEGTS_INDEXER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEGTS_INDEXER,GstMpegtsIndexerClass))
#define GST_IS_MPEGTS_INDEXER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEGTS_INDEXER))
#define GST_IS_MPEGTS_INDEXER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEGTS_INDEXER))

typedef struct _GstMpegtsIndexer GstMpegtsIndexer;
typedef struct _GstMpegtsIndexerClass GstMpegtsIndexerClass;

struct _GstMpegtsIndexer
{
  GstBaseTransform base_mpegtsindexer;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* Properties */
  GstIndex *index;
  gint index_id;

  /* MPEG-TS specific: */
  gint16 pcr_pid;
  GstClockTimeDiff delta;

  /* PCR tracking */
  guint64 last_pcr;
  guint64 current_offset;
  GstClockTime base_time;
  GstClockTime last_time;
};

struct _GstMpegtsIndexerClass
{
  GstBaseTransformClass base_mpegtsindexer_class;
};

GType gst_mpegts_indexer_get_type (void);

G_END_DECLS

#endif
