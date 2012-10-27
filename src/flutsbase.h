/* GStreamer Time Shifting
 * Copyright (C) 2011 Fluendo S.A. <support@fluendo.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __FLUTSBASE_H__
#define __FLUTSBASE_H__

#include "flucache.h"

G_BEGIN_DECLS
#define GST_FLUTSBASE_TYPE \
  (gst_flutsbase_get_type())
#define GST_FLUTSBASE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_FLUTSBASE_TYPE,GstFluTSBase))
#define GST_FLUTSBASE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_FLUTSBASE_TYPE,GstFluTSBaseClass))
#define GST_IS_FLUTSBASE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_FLUTSBASE_TYPE))
#define GST_IS_FLUTSBASE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_FLUTSBASE_TYPE))
#define GST_FLUTSBASE_CAST(obj) \
  ((GstFluTSBase *)(obj))
#define GST_FLUTSBASE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_FLUTSBASE_TYPE,GstFluTSBaseClass))
typedef struct _GstFluTSBase GstFluTSBase;
typedef struct _GstFluTSBaseClass GstFluTSBaseClass;

struct _GstFluTSBase
{
  GstElement element;

  /*< private > */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* Segment */
  GstSegment segment;

  /* flowreturn when srcpad is paused */
  GstFlowReturn srcresult;
  GstFlowReturn sinkresult;
  gboolean is_eos;
  gboolean unexpected;
  gboolean need_newsegment;

  /* the cache of data we're keeping our hands on */
  GstShifterCache *cache;
  guint64 cache_size;

  guint cur_bytes;              /* current position in bytes  */

  GMutex *flow_lock;            /* lock for flow control */
  GCond *buffer_add;            /* signals buffers added to the cache */

  /* recording location stuff */
  gchar *recording_template;
  gboolean recording_remove;
  gboolean recording_started;


  /* Generated Index */
  GstIndex *index;
  gint index_id;
  gboolean own_index;
};

struct _GstFluTSBaseClass
{
  GstElementClass parent_class;

  void (*collect_time) (GstFluTSBase * base, GstBuffer * buffer);
  guint64 (*seek) (GstFluTSBase * base, GstFormat format, GstSeekType type,
      gint64 start);
  void (*update_segment) (GstFluTSBase * base, GstBuffer * buffer);
  gboolean (*query) (GstFluTSBase * base, GstQuery * query);
};

GType gst_flutsbase_get_type (void);

void gst_flutsbase_add_pads (GstFluTSBase * ts);

G_END_DECLS
#endif /* __FLUTSBASE_H__ */
