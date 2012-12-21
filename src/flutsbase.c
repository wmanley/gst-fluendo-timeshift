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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "flutsbase.h"

#include <glib/gstdio.h>

GST_DEBUG_CATEGORY_EXTERN (ts_base);
GST_DEBUG_CATEGORY_EXTERN (ts_flow);
#define GST_CAT_DEFAULT (ts_base)

enum
{
  LAST_SIGNAL
};

/* default property values */
#define DEFAULT_RECORDING_REMOVE   TRUE
#define DEFAULT_MIN_CACHE_SIZE     (4 * CACHE_SLOT_SIZE)        /* 4 cache slots */
#define DEFAULT_CACHE_SIZE         (256 * 1024 * 1024)          /* 256 MB */

enum
{
  PROP_0,
  PROP_CACHE_SIZE,
  PROP_RECORDING_TEMPLATE,
  PROP_RECORDING_REMOVE,
  PROP_LAST
};

#define STATUS(ts, pad, msg)                                              \
  GST_CAT_LOG_OBJECT (ts_flow, ts,                                        \
      "(%s:%s) " msg ":%" G_GUINT64_FORMAT " bytes",                      \
      GST_DEBUG_PAD_NAME (pad),                                           \
      gst_shifter_cache_fullness (ts->cache))

#define FLOW_MUTEX_LOCK(ts) G_STMT_START {                                \
  g_mutex_lock (ts->flow_lock);                                           \
} G_STMT_END

#define FLOW_MUTEX_LOCK_CHECK(ts,res,label) G_STMT_START {                \
  FLOW_MUTEX_LOCK (ts);                                                   \
  if (res != GST_FLOW_OK)                                                 \
    goto label;                                                           \
} G_STMT_END

#define FLOW_MUTEX_UNLOCK(ts) G_STMT_START {                              \
  g_mutex_unlock (ts->flow_lock);                                         \
} G_STMT_END

#define FLOW_WAIT_ADD_CHECK(ts, res, label) G_STMT_START {                \
  STATUS (ts, ts->srcpad, "wait for ADD");                                \
  g_cond_wait (ts->buffer_add, ts->flow_lock);                            \
  if (res != GST_FLOW_OK) {                                               \
    STATUS (ts, ts->srcpad, "received ADD wakeup");                       \
    goto label;                                                           \
  }                                                                       \
  STATUS (ts, ts->srcpad, "received ADD");                                \
} G_STMT_END

#define FLOW_SIGNAL_ADD(ts) G_STMT_START {                                \
  STATUS (ts, ts->sinkpad, "signal ADD");                                 \
  g_cond_signal (ts->buffer_add);                                         \
} G_STMT_END

static GstElementClass *parent_class = NULL;
static void gst_flutsbase_class_init (GstFluTSBaseClass * klass);
static void gst_flutsbase_init (GstFluTSBase * ts, GstFluTSBaseClass * klass);
static GstClockTime gst_flutsbase_bytes_to_stream_time(GstFluTSBase * ts,
    GstFormat format, GstSeekType type, gint64 start);


GType
gst_flutsbase_get_type (void)
{
  static volatile gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (GstFluTSBaseClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_flutsbase_class_init,
      NULL,
      NULL,
      sizeof (GstFluTSBase),
      0,
      (GInstanceInitFunc) gst_flutsbase_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstFluTSBase", &info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static void
gst_flutsbase_start (GstFluTSBase * ts)
{
  FLOW_MUTEX_LOCK (ts);
  if (ts->cache) {
    gst_shifter_cache_unref (ts->cache);
    ts->cache = NULL;
  }

  ts->cache = gst_shifter_cache_new (ts->cache_size, ts->recording_template);
  gst_shifter_cache_set_autoremove (ts->cache, ts->recording_remove);

  /* If this is our own index destroy it as the old entries might be wrong */
  if (ts->own_index) {
    gst_object_unref (ts->index);
    ts->index = NULL;
    ts->own_index = FALSE;
  }

  /* If no index was created, generate one */
  if (G_UNLIKELY (!ts->index)) {
    GST_DEBUG_OBJECT (ts, "no index provided creating our own");

    ts->index = gst_index_factory_make ("memindex");

    gst_index_get_writer_id (ts->index, GST_OBJECT (ts), &ts->index_id);
    ts->own_index = TRUE;
  }

  gst_segment_init (&ts->segment, GST_FORMAT_BYTES);
  ts->recording_started = FALSE;
  FLOW_MUTEX_UNLOCK (ts);
}

static void
gst_flutsbase_stop (GstFluTSBase * ts)
{
  gboolean is_recording = FALSE;
  FLOW_MUTEX_LOCK (ts);
  if (ts->cache) {
    gchar *filename = gst_shifter_cache_get_filename (ts->cache);
    is_recording = gst_shifter_cache_is_recording (ts->cache);
    gst_shifter_cache_stop_recording (ts->cache);

    if (is_recording && filename) {
      GstStructure *stru = gst_structure_new ("shifter-recording-stopped",
          "filename", G_TYPE_STRING, filename, NULL);
      gst_element_post_message (GST_ELEMENT_CAST (ts),
          gst_message_new_element (GST_OBJECT (ts), stru));
    }
    gst_shifter_cache_unref (ts->cache);
    ts->cache = NULL;
  }
  FLOW_MUTEX_UNLOCK (ts);
}

static void
gst_flutsbase_set_recording_template (GstFluTSBase * ts, const gchar * template)
{
  GstState state;

  /* the element must be stopped in order to do this */
  GST_OBJECT_LOCK (ts);
  state = GST_STATE (ts);
  if (state != GST_STATE_READY && state != GST_STATE_NULL)
    goto wrong_state;
  GST_OBJECT_UNLOCK (ts);

  /* set new location */
  g_free (ts->recording_template);
  ts->recording_template = g_strdup (template);

  return;

/* ERROR */
wrong_state:
  {
    GST_WARNING_OBJECT (ts,
        "setting recording-template property in wrong state");
    GST_OBJECT_UNLOCK (ts);
  }
}

/* Pop a buffer from the cache and push it downstream.
 * This functions returns the result of the push. */
static GstFlowReturn
gst_flutsbase_pop (GstFluTSBase * ts)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buffer;

  if (!(buffer = gst_shifter_cache_pop (ts->cache, ts->is_eos)))
    goto no_item;

  if (ts->srcresult == GST_FLOW_FLUSHING) {
    gst_buffer_unref (buffer);
    goto out_flushing;
  }

  if (ts->stream_start_event) {
    if (!gst_pad_push_event (ts->srcpad, ts->stream_start_event)) {
      goto stream_start_failed;
    }
    ts->stream_start_event = NULL;
  }

  if (G_UNLIKELY (ts->need_newsegment)) {
    GstFluTSBaseClass *bclass = GST_FLUTSBASE_GET_CLASS (ts);
    GstEvent *newsegment;

    GstClockTime time = gst_flutsbase_bytes_to_stream_time(ts, GST_BUFFER_OFFSET);
    if (time != GST_TIME_NONE) {
      ts->segment.start = ts->segment.time = time;
      GST_BUFFER_TIMESTAMP (buffer) = ts->segment.start;
    } else if (ts->segment.format == GST_FORMAT_BYTES) {
      ts->segment.start = ts->segment.time = GST_BUFFER_OFFSET (buffer);
    }

    GST_DEBUG_OBJECT (ts, "pushing segment %" GST_SEGMENT_FORMAT, &ts->segment);

    newsegment = gst_event_new_segment (&ts->segment);
    if (newsegment) {
      if (!gst_pad_push_event (ts->srcpad, newsegment)) {
        goto segment_failed;
      }
    }
    ts->need_newsegment = FALSE;
  }
  ts->cur_bytes = GST_BUFFER_OFFSET_END (buffer);
  FLOW_MUTEX_UNLOCK (ts);

  GST_CAT_LOG_OBJECT (ts_flow, ts,
      "pushing buffer %p of size %d, offset %" G_GUINT64_FORMAT,
      buffer, gst_buffer_get_size (buffer), GST_BUFFER_OFFSET (buffer));

  ret = gst_pad_push (ts->srcpad, buffer);

  /* need to check for srcresult here as well */
  FLOW_MUTEX_LOCK_CHECK (ts, ts->srcresult, out_flushing);
  if (ret == GST_FLOW_EOS) {
    GST_CAT_LOG_OBJECT (ts_flow, ts, "got GST_FLOW_EOS from downstream");
    /* stop pushing buffers, we pop all buffers until we see an item that we
     * can push again, which is EOS or NEWSEGMENT. If there is nothing in the
     * cache we can push, we set a flag to make the sinkpad refuse more
     * buffers with an EOS return value until we receive something
     * pushable again or we get flushed. */
    while ((buffer = gst_shifter_cache_pop (ts->cache, ts->is_eos))) {
      GST_CAT_LOG_OBJECT (ts_flow, ts, "dropping UNEXPECTED buffer %p", buffer);
      gst_buffer_unref (buffer);
    }
    /* no more items in the cache. Set the unexpected flag so that upstream
     * make us refuse any more buffers on the sinkpad. Since we will still
     * accept EOS and NEWSEGMENT we return GST_FLOW_OK to the caller
     * so that the task function does not shut down. */
    ts->unexpected = TRUE;
    ret = GST_FLOW_OK;
  }

  return ret;

  /* ERRORS */
no_item:
  {
    if (ts->is_eos) {
      GST_CAT_LOG_OBJECT (ts_flow, ts, "pushing EOS");
      gst_pad_push_event (ts->srcpad, gst_event_new_eos ());
      gst_pad_pause_task (ts->srcpad);
      GST_CAT_LOG_OBJECT (ts_flow, ts, "pause task, reason: EOS");
      return GST_FLOW_OK;
    } else {
      GST_CAT_LOG_OBJECT (ts_flow, ts, "exit because we have no item");
      return GST_FLOW_ERROR;
    }
  }
out_flushing:
  {
    GST_CAT_LOG_OBJECT (ts_flow, ts, "exit because we are flushing");
    return GST_FLOW_FLUSHING;
  }
segment_failed:
  {
    GST_CAT_LOG_OBJECT (ts_flow, ts, "push of SEGMENT event failed");
    return GST_FLOW_FLUSHING;
  }
stream_start_failed:
  {
    ts->stream_start_event = NULL;
    GST_CAT_LOG_OBJECT (ts_flow, ts, "push of STREAM_START event failed");
    return GST_FLOW_FLUSHING;
  }
}

/* called repeadedly with @pad as the source pad. This function should push out
 * data to the peer element. */
static void
gst_flutsbase_loop (GstPad * pad)
{
  GstFluTSBase *ts;
  GstFlowReturn ret;

  ts = GST_FLUTSBASE (GST_PAD_PARENT (pad));

  /* have to lock for thread-safety */
  FLOW_MUTEX_LOCK_CHECK (ts, ts->srcresult, out_flushing);

  if (gst_shifter_cache_is_empty (ts->cache) && !ts->is_eos) {
    GST_CAT_LOG_OBJECT (ts_flow, ts, "empty, waiting for new data");
    do {
      /* Wait for data to be available, we could be unlocked because of a flush. */
      FLOW_WAIT_ADD_CHECK (ts, ts->srcresult, out_flushing);
    }
    while (gst_shifter_cache_is_empty (ts->cache) && !ts->is_eos);
  }
  ret = gst_flutsbase_pop (ts);
  ts->srcresult = ret;
  if (ret != GST_FLOW_OK)
    goto out_flushing;

  FLOW_MUTEX_UNLOCK (ts);

  return;

  /* ERRORS */
out_flushing:
  {
    gboolean eos = ts->is_eos;
    GstFlowReturn ret = ts->srcresult;

    gst_pad_pause_task (ts->srcpad);
    FLOW_MUTEX_UNLOCK (ts);
    GST_CAT_LOG_OBJECT (ts_flow, ts,
        "pause task, reason:  %s", gst_flow_get_name (ts->srcresult));
    /* let app know about us giving up if upstream is not expected to do so */
    /* UNEXPECTED is already taken care of elsewhere */
    if (eos && (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS)) {
      GST_ELEMENT_ERROR (ts, STREAM, FAILED,
          ("Internal data flow error."),
          ("streaming task paused, reason %s (%d)",
              gst_flow_get_name (ret), ret));
      GST_CAT_LOG_OBJECT (ts_flow, ts, "pushing EOS");
      gst_pad_push_event (ts->srcpad, gst_event_new_eos ());
    }
    return;
  }
}

static GstFlowReturn
gst_flutsbase_push (GstFluTSBase * ts, guint8 * data, gsize size)
{
  GstFluTSBaseClass *bclass = GST_FLUTSBASE_GET_CLASS (ts);

  /* we have to lock since we span threads */
  FLOW_MUTEX_LOCK_CHECK (ts, ts->sinkresult, out_flushing);
  /* when we received EOS, we refuse more data */
  if (ts->is_eos)
    goto out_eos;
  /* when we received unexpected from downstream, refuse more buffers */
  if (ts->unexpected)
    goto out_unexpected;

  /* collect time info from that buffer */
  if (bclass->collect_time) {
    bclass->collect_time (ts, data, size);
  }
  /* add data to the cache */
  gst_shifter_cache_push (ts->cache, data, size);
  FLOW_SIGNAL_ADD (ts);

  if (G_UNLIKELY (!ts->recording_started &&
          gst_shifter_cache_is_recording (ts->cache))) {
    gchar *filename = gst_shifter_cache_get_filename (ts->cache);

    GstStructure *stru = gst_structure_new ("shifter-recording-started",
        "filename", G_TYPE_STRING, filename, NULL);
    gst_element_post_message (GST_ELEMENT_CAST (ts),
        gst_message_new_element (GST_OBJECT (ts), stru));
    ts->recording_started = TRUE;
  }

  FLOW_MUTEX_UNLOCK (ts);

  return GST_FLOW_OK;

  /* special conditions */
out_flushing:
  {
    GstFlowReturn ret = ts->sinkresult;

    GST_CAT_LOG_OBJECT (ts_flow, ts,
        "exit because task paused, reason: %s", gst_flow_get_name (ret));
    FLOW_MUTEX_UNLOCK (ts);
    return ret;
  }
out_eos:
  {
    GST_CAT_LOG_OBJECT (ts_flow, ts, "exit because we received EOS");
    FLOW_MUTEX_UNLOCK (ts);
    return GST_FLOW_EOS;
  }
out_unexpected:
  {
    GST_CAT_LOG_OBJECT (ts_flow, ts, "exit because we received UNEXPECTED");
    FLOW_MUTEX_UNLOCK (ts);
    return GST_FLOW_EOS;
  }
}

static GstFlowReturn
gst_flutsbase_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFluTSBase *ts = GST_FLUTSBASE (parent);
  GstFlowReturn res;
  GstMapInfo map;

  GST_CAT_LOG_OBJECT (ts_flow, ts,
      "received buffer %p of size %d, time %" GST_TIME_FORMAT ", duration %"
      GST_TIME_FORMAT, buffer, gst_buffer_get_size (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  res = gst_flutsbase_push (ts, map.data, map.size);
  gst_buffer_unmap (buffer, &map);

  gst_buffer_unref (buffer);
 
  return res;
}

static GstClockTime
gst_flutsbase_bytes_to_stream_time(GstFluTSBase * ts, GstFormat format,
    GstSeekType type, gint64 start)
{
  /* Let's check if we have an index entry for that seek bytes */
  entry = gst_index_get_assoc_entry (base->index, base->index_id,
      GST_INDEX_LOOKUP_BEFORE, GST_ASSOCIATION_FLAG_NONE, GST_FORMAT_BYTES, len);

  if (entry) {
    gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, &offset);
    gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &time);

    GST_DEBUG_OBJECT (base, "found index entry at %" GST_TIME_FORMAT " pos %"
        G_GUINT64_FORMAT, GST_TIME_ARGS (time), offset);
    return time;
  }
  else {
    return GST_TIME_NONE;
  }
}

static guint64
gst_flutsbase_get_bytes_offset(GstFluTSBase * ts, GstFormat format,
    GstSeekType type, gint64 start)
{
  guint64 offset;
  GstFluTSBaseClass *bclass;

  bclass = GST_FLUTSBASE_GET_CLASS (ts);

  if (type == GST_SEEK_TYPE_NONE) {
    offset = -1;
  } else {
    switch (format) {
      case GST_FORMAT_BYTES: {
        GST_DEBUG_OBJECT (ts, "seeking at bytes %" G_GINT64_FORMAT " type %d",
            start, type);

        if (type == GST_SEEK_TYPE_SET) {
          offset = start;
        } else if (type == GST_SEEK_TYPE_END) {
          offset = gst_shifter_cache_get_total_bytes_received(ts->cache) + start;
        } else {
          offset = -1;
        }
        break;
      }
      case GST_FORMAT_TIME: {
        if (bclass->seek) {
          FLOW_MUTEX_LOCK (ts);
          offset = bclass->seek (ts, type, start);
          FLOW_MUTEX_UNLOCK (ts);
        } else {
          GST_WARNING_OBJECT (ts, "seeking by TIME is not implemented");
          offset = -1;
        }
        break;
      }
      default: {
        GST_WARNING_OBJECT (ts, "Only seeking in TIME and BYTES supported");
        offset = -1;
      }
    };
  }
  return offset;
}

static gboolean
gst_flutsbase_handle_seek (GstFluTSBase * ts, GstEvent * event)
{
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gdouble rate;
  guint64 offset = 0;
  gboolean ret = FALSE;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &start_type, &start, &stop_type, &stop);

  if (!(flags & GST_SEEK_FLAG_FLUSH)) {
    GST_WARNING_OBJECT (ts, "we only support flushing seeks");
    goto beach;
  }

  if (rate < 0.0) {
    GST_WARNING_OBJECT (ts, "we only support forward playback");
    goto beach;
  }

  offset = gst_flutsbase_get_bytes_offset(ts, format, start_type, start);
  if (G_UNLIKELY (offset == (guint64) -1 || !gst_shifter_cache_has_offset (ts->cache, offset))) {
    GST_WARNING_OBJECT (ts, "seek failed");
    goto beach;
  }

  /* remember the rate */
  ts->segment.rate = rate;

  GST_DEBUG_OBJECT (ts, "seeking at offset %" G_GUINT64_FORMAT, offset);

  /* now unblock the loop function */
  FLOW_MUTEX_LOCK (ts);
  /* Flush start downstream to make sure loop is idle */
  gst_pad_push_event (ts->srcpad, gst_event_new_flush_start ());
  ts->srcresult = GST_FLOW_FLUSHING;
  /* unblock the loop function */
  FLOW_SIGNAL_ADD (ts);
  FLOW_MUTEX_UNLOCK (ts);

  /* make sure it pauses, this should happen since we sent
   * flush_start downstream. */
  gst_pad_pause_task (ts->srcpad);
  GST_DEBUG_OBJECT (ts, "loop stopped");
  /* Flush stop downstream to ensure that all pushed cache slots come back
   * to our control */
  gst_pad_push_event (ts->srcpad, gst_event_new_flush_stop (TRUE));

  /* Reconfigure the cache to handle the new offset */
  FLOW_MUTEX_LOCK (ts);
  gst_shifter_cache_seek (ts->cache, offset);

  /* Restart the pushing loop */
  ts->srcresult = GST_FLOW_OK;
  ts->is_eos = FALSE;
  ts->unexpected = FALSE;
  ts->need_newsegment = TRUE;
  gst_pad_start_task (ts->srcpad, (GstTaskFunction) gst_flutsbase_loop,
      ts->srcpad, NULL);
  FLOW_MUTEX_UNLOCK (ts);
  GST_DEBUG_OBJECT (ts, "loop started");
  ret = TRUE;
beach:
  return ret;
}

static gboolean
gst_flutsbase_handle_custom_upstream (GstFluTSBase * ts, GstEvent * event)
{
  gboolean ret = FALSE;
  const GstStructure *stru;

  stru = gst_event_get_structure (event);
  if (stru && gst_structure_has_name (stru, "shifter-start-recording")) {
    if (GST_STATE (ts) < GST_STATE_PAUSED) {
      GST_DEBUG_OBJECT (ts, "Received event while not in PAUSED/PLAYING state");
      goto beach;
    }
    FLOW_MUTEX_LOCK (ts);
    ret = gst_shifter_cache_start_recording (ts->cache);
    FLOW_MUTEX_UNLOCK (ts);
  }

beach:
  return ret;
}

static inline gboolean
gst_flutsbase_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFluTSBase *ts = GST_FLUTSBASE (parent);
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GST_CAT_LOG_OBJECT (ts_flow, ts, "received eos event");
      FLOW_MUTEX_LOCK (ts);
      ts->is_eos = TRUE;
      /* Ensure to unlock the pushing loop */
      FLOW_SIGNAL_ADD (ts);
      FLOW_MUTEX_UNLOCK (ts);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GST_CAT_LOG_OBJECT (ts_flow, ts, "received newsegment event");
      FLOW_MUTEX_LOCK (ts);
      ts->unexpected = FALSE;
      FLOW_MUTEX_UNLOCK (ts);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH_START:
    {
      GST_CAT_LOG_OBJECT (ts_flow, ts, "received flush start event");
      /* forward event */
      gst_pad_push_event (ts->srcpad, event);

      /* now unblock the chain function */
      FLOW_MUTEX_LOCK (ts);
      ts->srcresult = GST_FLOW_FLUSHING;
      ts->sinkresult = GST_FLOW_FLUSHING;
      /* unblock the loop and chain functions */
      FLOW_SIGNAL_ADD (ts);
      FLOW_MUTEX_UNLOCK (ts);

      /* make sure it pauses, this should happen since we sent
       * flush_start downstream. */
      gst_pad_pause_task (ts->srcpad);
      GST_CAT_LOG_OBJECT (ts_flow, ts, "loop stopped");
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_CAT_LOG_OBJECT (ts_flow, ts, "received flush stop event");

      /* forward event */
      gst_pad_push_event (ts->srcpad, event);

      gst_flutsbase_start (ts);

      FLOW_MUTEX_LOCK (ts);
      gst_event_replace (&ts->stream_start_event, NULL);
      ts->cur_bytes = 0;
      ts->srcresult = GST_FLOW_OK;
      ts->sinkresult = GST_FLOW_OK;
      ts->is_eos = FALSE;
      ts->unexpected = FALSE;
      gst_pad_start_task (ts->srcpad, (GstTaskFunction) gst_flutsbase_loop,
          ts->srcpad, NULL);
      FLOW_MUTEX_UNLOCK (ts);
      break;
    }
    case GST_EVENT_STREAM_START:
    {
      gst_event_replace (&ts->stream_start_event, event);
      gst_event_unref (event);
      break;
    }
    default:
    {
      GST_CAT_LOG_OBJECT (ts_flow, ts, "dropped event %s",
          GST_EVENT_TYPE_NAME (event));
      gst_event_unref (event);
      ret = FALSE;
      break;
    }
  }

  return ret;
}

static inline gboolean
gst_flutsbase_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = TRUE;
  GstFluTSBase *ts = GST_FLUTSBASE (parent);

  if (G_UNLIKELY (ts == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GST_CAT_LOG_OBJECT (ts_flow, ts, "received seek event");
      /* Do the seek ourself now */
      ret = gst_flutsbase_handle_seek (ts, event);
      break;
    }
    case GST_EVENT_QOS:
    {
      break;
    }
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      ret = gst_flutsbase_handle_custom_upstream (ts, event);
      break;
    }
    default:
      GST_CAT_LOG_OBJECT (ts_flow, ts, "dropped event %s",
          GST_EVENT_TYPE_NAME (event));
      ret = FALSE;
      break;
  }

  gst_event_unref (event);

  return ret;
}

static gboolean
gst_flutsbase_query (GstElement * element, GstQuery * query)
{
  gboolean ret = TRUE;
  GstFluTSBase * ts = GST_FLUTSBASE (element);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 pos = -1;
      GstFormat format;

      /* get format */
      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_BYTES:
          pos = ts->cur_bytes;
          break;
        default:
          GST_WARNING_OBJECT (ts, "dropping query in %s format, don't "
              "know how to handle", gst_format_get_name (format));
          ret = FALSE;
          break;
      }
      /* set updated position */
      if (ret)
        gst_query_set_position (query, format, pos);
      break;
    }
    case GST_QUERY_DURATION:
    {
      GST_LOG_OBJECT (ts, "doing peer query");

      GstFluTSBaseClass *bclass = GST_FLUTSBASE_GET_CLASS (ts);

      if (bclass->query) {
        ret = bclass->query (ts, query);
      } else {
        ret = FALSE;
      }
      break;
    }
    case GST_QUERY_SEEKING:
    {
      GstFluTSBaseClass *bclass = GST_FLUTSBASE_GET_CLASS (ts);

      if (bclass->query) {
        ret = bclass->query (ts, query);
      } else {
        ret = FALSE;
      }
      break;
    }
    case GST_QUERY_LATENCY:
    {
      gst_query_set_latency (query, FALSE, 0, -1);
      break;
    }

    default:
      ret = FALSE;
      break;
  }

  return ret;
}

/* sink currently only operates in push mode */
static inline gboolean
gst_flutsbase_sink_activate (GstPad * pad, GstObject * parent, gboolean active)
{
  GstFluTSBase *ts = GST_FLUTSBASE (parent);

  if (active) {
    FLOW_MUTEX_LOCK (ts);
    GST_DEBUG_OBJECT (ts, "activating push mode");
    ts->srcresult = GST_FLOW_OK;
    ts->sinkresult = GST_FLOW_OK;
    ts->is_eos = FALSE;
    ts->unexpected = FALSE;
    FLOW_MUTEX_UNLOCK (ts);
  } else {
    /* unblock chain function */
    FLOW_MUTEX_LOCK (ts);
    GST_DEBUG_OBJECT (ts, "deactivating push mode");
    ts->srcresult = GST_FLOW_FLUSHING;
    ts->sinkresult = GST_FLOW_FLUSHING;
    gst_event_replace (&ts->stream_start_event, NULL);
    FLOW_MUTEX_UNLOCK (ts);
  }

  return TRUE;
}

/* src operating in push mode, we start a task on the source pad that pushes out
 * buffers from the cache */
static inline gboolean
gst_flutsbase_src_activate (GstPad * pad, GstObject * parent, gboolean active)
{
  GstFluTSBase *ts = GST_FLUTSBASE (parent);
  gboolean ret = FALSE;

  if (active) {
    FLOW_MUTEX_LOCK (ts);
    GST_DEBUG_OBJECT (ts, "activating push mode");
    ts->srcresult = GST_FLOW_OK;
    ts->sinkresult = GST_FLOW_OK;
    ts->is_eos = FALSE;
    ts->unexpected = FALSE;
    ret = gst_pad_start_task (pad, (GstTaskFunction) gst_flutsbase_loop, pad, NULL);
    FLOW_MUTEX_UNLOCK (ts);
  } else {
    /* unblock loop function */
    FLOW_MUTEX_LOCK (ts);
    GST_DEBUG_OBJECT (ts, "deactivating push mode");
    ts->srcresult = GST_FLOW_FLUSHING;
    ts->sinkresult = GST_FLOW_FLUSHING;
    /* the item add signal will unblock */
    FLOW_SIGNAL_ADD (ts);
    FLOW_MUTEX_UNLOCK (ts);

    /* step 2, make sure streaming finishes */
    ret = gst_pad_stop_task (pad);
  }
 
  return ret;
}

static gboolean
gst_flutsbase_handle_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  return gst_flutsbase_sink_event (pad, parent, event);
}

static gboolean
gst_flutsbase_handle_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  return gst_flutsbase_src_event (pad, parent, event);
}

static gboolean
gst_flutsbase_handle_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  return gst_flutsbase_query (GST_ELEMENT (parent), query);
}

static gboolean
gst_flutsbase_sink_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean ret = FALSE;

  if (mode == GST_PAD_MODE_PUSH) {
    ret = gst_flutsbase_sink_activate (pad, parent, active);
  }
  return ret;
}

static gboolean
gst_flutsbase_src_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean ret = FALSE;

  if (mode == GST_PAD_MODE_PUSH) {
    ret = gst_flutsbase_src_activate (pad, parent, active);
  }
  return ret;
}

static GstStateChangeReturn
gst_flutsbase_change_state (GstElement * element, GstStateChange transition)
{
  GstFluTSBase *ts;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  ts = GST_FLUTSBASE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_flutsbase_start (ts);
      gst_event_replace (&ts->stream_start_event, NULL);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_flutsbase_stop (ts);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_flutsbase_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFluTSBase *ts = GST_FLUTSBASE (object);

  /* someone could change size here, and since this
   * affects the get/put funcs, we need to lock for safety. */
  FLOW_MUTEX_LOCK (ts);

  switch (prop_id) {
    case PROP_CACHE_SIZE:
      ts->cache_size = g_value_get_uint64 (value);
      break;
    case PROP_RECORDING_TEMPLATE:
      gst_flutsbase_set_recording_template (ts, g_value_get_string (value));
      break;
    case PROP_RECORDING_REMOVE:
      ts->recording_remove = g_value_get_boolean (value);
      if (ts->cache) {
        gst_shifter_cache_set_autoremove (ts->cache, ts->recording_remove);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  FLOW_MUTEX_UNLOCK (ts);
}

static void
gst_flutsbase_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFluTSBase *ts = GST_FLUTSBASE (object);

  FLOW_MUTEX_LOCK (ts);

  switch (prop_id) {
    case PROP_CACHE_SIZE:
      g_value_set_uint64 (value, ts->cache_size);
      break;
    case PROP_RECORDING_TEMPLATE:
      g_value_set_string (value, ts->recording_template);
      break;
    case PROP_RECORDING_REMOVE:
      g_value_set_boolean (value, ts->recording_remove);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  FLOW_MUTEX_UNLOCK (ts);
}

static void
gst_flutsbase_finalize (GObject * object)
{
  GstFluTSBase *ts = GST_FLUTSBASE (object);

  GST_DEBUG_OBJECT (ts, "finalizing tsbase");

  if (ts->cache) {
    gst_shifter_cache_unref (ts->cache);
    ts->cache = NULL;
  }
  g_mutex_free (ts->flow_lock);
  g_cond_free (ts->buffer_add);

  /* recording_file path cleanup  */
  g_free (ts->recording_template);

  if (ts->index) {
    gst_object_unref (ts->index);
    ts->index = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_flutsbase_class_init (GstFluTSBaseClass * klass)
{
  GObjectClass *gclass = G_OBJECT_CLASS (klass);
  GstElementClass *eclass = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gclass->set_property = gst_flutsbase_set_property;
  gclass->get_property = gst_flutsbase_get_property;

  /* properties */
  g_object_class_install_property (gclass, PROP_CACHE_SIZE,
      g_param_spec_uint64 ("cache-size",
          "Cache size in bytes",
          "Max. amount of data cached in memory (bytes)",
          DEFAULT_MIN_CACHE_SIZE, G_MAXUINT64, DEFAULT_CACHE_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gclass, PROP_RECORDING_TEMPLATE,
      g_param_spec_string ("recording-template", "Recording File Template",
          "File template to store recorded files in, should contain directory "
          "and a prefix filename. (NULL == disabled)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gclass, PROP_RECORDING_REMOVE,
      g_param_spec_boolean ("recording-remove", "Remove the Recorded File",
          "Remove the recorded file after use",
          DEFAULT_RECORDING_REMOVE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* set several parent class virtual functions */
  gclass->finalize = gst_flutsbase_finalize;

  eclass->change_state = GST_DEBUG_FUNCPTR (gst_flutsbase_change_state);
  eclass->query = GST_DEBUG_FUNCPTR (gst_flutsbase_query);
}

static void
gst_flutsbase_init (GstFluTSBase * ts, GstFluTSBaseClass * klass)
{
  ts->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "sink"), "sink");

  gst_pad_set_chain_function (ts->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flutsbase_chain));
  gst_pad_set_activatemode_function (ts->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flutsbase_sink_activate_mode));
  gst_pad_set_event_function (ts->sinkpad,
      GST_DEBUG_FUNCPTR (gst_flutsbase_handle_sink_event));
  gst_element_add_pad (GST_ELEMENT (ts), ts->sinkpad);

  ts->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "src"), "src");

  gst_pad_set_activatemode_function (ts->srcpad,
      GST_DEBUG_FUNCPTR (gst_flutsbase_src_activate_mode));
  gst_pad_set_event_function (ts->srcpad,
      GST_DEBUG_FUNCPTR (gst_flutsbase_handle_src_event));
  gst_pad_set_query_function (ts->srcpad,
      GST_DEBUG_FUNCPTR (gst_flutsbase_handle_src_query));
  gst_element_add_pad (GST_ELEMENT (ts), ts->srcpad);

  /* set default values */
  ts->cur_bytes = 0;

  ts->srcresult = GST_FLOW_FLUSHING;
  ts->sinkresult = GST_FLOW_FLUSHING;
  ts->is_eos = FALSE;
  ts->need_newsegment = TRUE;

  ts->flow_lock = g_mutex_new ();
  ts->buffer_add = g_cond_new ();

  /* tempfile related */
  ts->recording_template = NULL;
  ts->recording_remove = DEFAULT_RECORDING_REMOVE;

  ts->cache_size = DEFAULT_CACHE_SIZE;

  ts->index = NULL;
  ts->index_id = 0;
  ts->own_index = FALSE;

  GST_DEBUG_OBJECT (ts, "initialized time shift base");
}
