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

#include "flutsmpeg.h"

GST_DEBUG_CATEGORY_EXTERN (ts_mpeg);
#define GST_CAT_DEFAULT ts_mpeg

#define gst_flumpegshifter_parent_class parent_class
G_DEFINE_TYPE (GstFluMPEGShifter, gst_flumpegshifter, GST_FLUTSBASE_TYPE);

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS ("video/mpegts"));

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS ("video/mpegts"));

enum
{
  PROP_0,
};

#define DEFAULT_DELTA           500

#define TS_PACKET_SYNC_CODE     0x47
#define TS_MIN_PACKET_SIZE      188
#define TS_MAX_PACKET_SIZE      208
#define INVALID_PID             -1

#define CLOCK_BASE 9LL
#define CLOCK_FREQ (CLOCK_BASE * 10000)

#define MPEGTIME_TO_GSTTIME(time) (gst_util_uint64_scale ((time), \
            GST_MSECOND/10, CLOCK_BASE))
#define GSTTIME_TO_MPEGTIME(time) (gst_util_uint64_scale ((time), \
            CLOCK_BASE, GST_MSECOND/10))

static gboolean
is_next_sync_valid (const guint8 * in_data, guint size, guint offset)
{
  static const guint packet_sizes[] = { 188, 192, 204, 208 };
  gint i;

  for (i = 0; i < 4 && (offset + packet_sizes[i]) < size; i++) {
    if (in_data[offset + packet_sizes[i]] == TS_PACKET_SYNC_CODE) {
      return TRUE;
    }
  }
  return FALSE;
}

static inline guint64
gst_flumpegshifter_parse_pcr (GstFluMPEGShifter * ts, guint8 * data)
{
  guint16 pid;
  guint32 pcr1;
  guint16 pcr2;
  guint64 pcr = (guint64) -1, pcr_ext;

  if (TS_PACKET_SYNC_CODE == data[0]) {
    /* Check Adaptation field, if it == b10 or b11 */
    if (data[3] & 0x20) {
      /* Check PID Match */
      pid = GST_READ_UINT16_BE (data + 1);
      pid &= 0x1fff;

      if (pid == (guint16) ts->pcr_pid) {
        /* Check Adaptation field size */
        if (data[4]) {
          /* Check if random access flag is present */
          if (ts->delta == -1 && GST_CLOCK_TIME_IS_VALID (ts->base_time) &&
              !(data[5] & 0x40)) {
            /* random access flag not set just skip after first PCR */
            goto beach;
          }
          /* Check if PCR is present */
          if (data[5] & 0x10) {
            pcr1 = GST_READ_UINT32_BE (data + 6);
            pcr2 = GST_READ_UINT16_BE (data + 10);
            pcr = ((guint64) pcr1) << 1;
            pcr |= (pcr2 & 0x8000) >> 15;
            pcr_ext = (pcr2 & 0x01ff);
            if (pcr_ext)
              pcr = (pcr * 300 + pcr_ext % 300) / 300;
          }
        }
      }
    }
  }

beach:
  return pcr;
}

static inline guint64
gst_flumpegshifter_get_pcr (GstFluMPEGShifter * ts, guint8 ** in_data,
    gsize * in_size, guint64 * offset)
{
  guint64 pcr = (guint64) -1;
  gint i = 0;
  guint8 *data = *in_data;
  gsize size = *in_size;

  /* mpegtsparse pushes PES packet buffers so this case must be handled
   * without checking for next SYNC code */
  if (size >= TS_MIN_PACKET_SIZE && size <= TS_MAX_PACKET_SIZE) {
    pcr = gst_flumpegshifter_parse_pcr (ts, data);
  } else {
    while ((i + TS_MAX_PACKET_SIZE) < size) {
      if (TS_PACKET_SYNC_CODE == data[i]) {
        /* Check the next SYNC byte for all packets except the last packet
         * in a buffer... */
        if (G_LIKELY (is_next_sync_valid (data, size, i))) {
          pcr = gst_flumpegshifter_parse_pcr (ts, data + i);
          if (pcr == -1) {
            /* Skip to start of next TSPacket (pre-subract for the i++ later) */
            i += (TS_MIN_PACKET_SIZE - 1);
          } else {
            *in_data += i;
            *in_size -= i;
            *offset += i;
            break;
          }
        }
      }
      i++;                      /* next byte in buffer until we find sync */
    }
  }
  return pcr;
}

static guint64
gst_flumpegshifter_seek (GstFluTSBase * base,
    GstSeekType type, gint64 start)
{
  GstFluMPEGShifter *ts = GST_FLUMPEGSHIFTER_CAST (base);
  GstIndexEntry *entry = NULL;
  gint64 offset = -1;
  gint64 time;
  GstClockTime pos = 0;

  if (type == GST_SEEK_TYPE_NONE) {
    /* Base class checks: Should never happen */
    goto beach;
  }

  GST_DEBUG_OBJECT (ts, "seeking at time %" GST_TIME_FORMAT " type %d",
      GST_TIME_ARGS (start), type);

  if (!base->index) {
    GST_DEBUG_OBJECT (ts, "no index");
    goto beach;
  }

  if (type == GST_SEEK_TYPE_SET) {
    pos = start;
  } else if (type == GST_SEEK_TYPE_END) {
    pos = ts->last_time + start;
  }

  GST_DEBUG_OBJECT (ts, "seek in index for %" GST_TIME_FORMAT,
      GST_TIME_ARGS (pos));

  /* Let's check if we have an index entry for that seek time */
  entry = gst_index_get_assoc_entry (base->index, base->index_id,
      GST_INDEX_LOOKUP_BEFORE, GST_ASSOCIATION_FLAG_NONE, GST_FORMAT_TIME, pos);

  if (entry) {
    gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, &offset);
    gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &time);

    GST_DEBUG_OBJECT (ts, "found index entry at %" GST_TIME_FORMAT " pos %"
        G_GUINT64_FORMAT, GST_TIME_ARGS (time), offset);
  }

beach:
  return offset;
}

static void
gst_flumpegshifter_update_segment (GstFluTSBase * base, guint8 * data, gsize size)
{
  GstFluMPEGShifter *ts = GST_FLUMPEGSHIFTER_CAST (base);
  GstClockTime start = 0, time = 0;
  guint64 pcr, offset = 0;

  if (G_UNLIKELY (base->segment.format != GST_FORMAT_TIME)) {
    gst_segment_init (&base->segment, GST_FORMAT_TIME);
  }

  pcr = gst_flumpegshifter_get_pcr (ts, &data, &size, &offset);
  if (pcr != (guint64) -1) {
    /* FIXME: handle wraparounds */
    start = time = MPEGTIME_TO_GSTTIME (pcr);

    if (GST_CLOCK_TIME_IS_VALID (ts->base_time)) {
      time -= ts->base_time;
    }

    GST_LOG_OBJECT (ts, "found PCR %" G_GUINT64_FORMAT "(%" GST_TIME_FORMAT
        ") at offset %" G_GUINT64_FORMAT " position %" GST_TIME_FORMAT,
        pcr, GST_TIME_ARGS (start), offset, GST_TIME_ARGS (time));
    base->segment.start = start;
    base->segment.time = time;
  }
}

static gboolean
gst_flumpegshifter_query (GstFluTSBase * base, GstQuery * query)
{
  GstFluMPEGShifter *ts = GST_FLUMPEGSHIFTER_CAST (base);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      if (format == GST_FORMAT_TIME) {
        if (GST_CLOCK_TIME_IS_VALID (ts->base_time)) {
          GstClockTime time = MPEGTIME_TO_GSTTIME (ts->last_pcr);
          time -= ts->base_time;
          if (time) {
            GST_LOG_OBJECT (ts,
                "replying duration query with %" GST_TIME_FORMAT,
                GST_TIME_ARGS (time));
            gst_query_set_duration (query, GST_FORMAT_TIME, time);
            ret = TRUE;
          } else {
            ret = FALSE;
          }
        }
      } else if (format == GST_FORMAT_BYTES) {
        GST_LOG_OBJECT (ts, "replying duration query with %" G_GUINT64_FORMAT,
            ts->current_offset);
        gst_query_set_duration (query, GST_FORMAT_BYTES, ts->current_offset);
        ret = TRUE;
      }
      break;
    }
    case GST_QUERY_SEEKING:
    {
      GstFormat fmt;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt == GST_FORMAT_BYTES || fmt == GST_FORMAT_TIME) {
        gst_query_set_seeking (query, fmt, TRUE, 0, -1);
        ret = TRUE;
      }
      break;
    }
    default:
      break;
  }
  return ret;
}

static void
gst_flumpegshifter_class_init (GstFluMPEGShifterClass * klass)
{
  GstFluTSBaseClass *bclass = GST_FLUTSBASE_CLASS (klass);
  GObjectClass *gclass = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);


  /* base TS vmethods */
  bclass->collect_time = gst_flumpegshifter_collect_time;
  bclass->seek = gst_flumpegshifter_seek;
  bclass->update_segment = gst_flumpegshifter_update_segment;
  bclass->query = gst_flumpegshifter_query;

  /* GstElement related stuff */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details_simple (element_class,
      "Fluendo Time Shift for MPEG TS streams",
      "Generic",
      "Provide time shift operations on MPEG TS streams",
      "Fluendo S.A. <support@fluendo.com>");
}

static void
gst_flumpegshifter_init (GstFluMPEGShifter * ts)
{
}
