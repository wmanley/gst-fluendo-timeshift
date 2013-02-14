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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gsttimeshifttsindexer
 *
 * Populates a time/byte offset index for MPEG-TS streams based upon PCR
 * information.  An index to populate should be passed in as the "index"
 * property.
 *
 * This element is used by flutsmpegbin to create an index for timeshifting.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/base/gstbasetransform.h>
#include "gsttimeshifttsindexer.h"

GST_DEBUG_CATEGORY_STATIC (gst_time_shift_ts_indexer_debug_category);
#define GST_CAT_DEFAULT gst_time_shift_ts_indexer_debug_category

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

/* prototypes */


static void gst_time_shift_ts_indexer_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_time_shift_ts_indexer_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_time_shift_ts_indexer_dispose (GObject * object);
static void gst_time_shift_ts_indexer_finalize (GObject * object);

static gboolean gst_time_shift_ts_indexer_start (GstBaseTransform * trans);
static gboolean gst_time_shift_ts_indexer_stop (GstBaseTransform * trans);
static GstFlowReturn
gst_time_shift_ts_indexer_transform_ip (GstBaseTransform * trans, GstBuffer * buf);
static inline guint64
gst_time_shift_ts_indexer_get_pcr (GstTimeShiftTsIndexer * ts, guint8 * data,
    gsize size);

enum
{
  PROP_0,
  PROP_PCR_PID,
};

/* pad templates */

static GstStaticPadTemplate gst_time_shift_ts_indexer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts")
    );

static GstStaticPadTemplate gst_time_shift_ts_indexer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts")
    );


/* class initialization */

G_DEFINE_TYPE (GstTimeShiftTsIndexer, gst_time_shift_ts_indexer, GST_TYPE_BASE_TRANSFORM);
#define parent_class gst_time_shift_ts_indexer_parent_class

static void
gst_time_shift_ts_indexer_class_init (GstTimeShiftTsIndexerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_time_shift_ts_indexer_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_time_shift_ts_indexer_src_template));

  gst_element_class_set_static_metadata (element_class,
      "Indexer for MPEG-TS streams", "Generic",
      "Generates an index for mapping from time to bytes and vice-versa for "
      "MPEG-TS streams based upon MPEG-TS PCR.",
      "Fluendo S.A. <support@fluendo.com>, "
      "William Manley <will@williammanley.net>");

  GST_DEBUG_CATEGORY_INIT (gst_time_shift_ts_indexer_debug_category,
      "gst_time_shift_ts_indexer", 0, "Indexer for MPEG-TS streams");


  gobject_class->set_property = gst_time_shift_ts_indexer_set_property;
  gobject_class->get_property = gst_time_shift_ts_indexer_get_property;
  gobject_class->dispose = gst_time_shift_ts_indexer_dispose;
  gobject_class->finalize = gst_time_shift_ts_indexer_finalize;
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_time_shift_ts_indexer_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_time_shift_ts_indexer_stop);
  base_transform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_time_shift_ts_indexer_transform_ip);

  g_object_class_install_property (gobject_class, PROP_PCR_PID,
      g_param_spec_int ("pcr-pid", "PCR pid",
          "Defines the PCR pid to collect the time (-1 = undefined)",
          INVALID_PID, 0x1fff, INVALID_PID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_time_shift_ts_indexer_init (GstTimeShiftTsIndexer * indexer)
{
  GstBaseTransform *base = GST_BASE_TRANSFORM (indexer);
  gst_base_transform_set_passthrough(base, TRUE);

  indexer->pcr_pid = INVALID_PID;
  indexer->base_time = GST_CLOCK_TIME_NONE;
}

void
gst_time_shift_ts_indexer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTimeShiftTsIndexer *indexer = GST_TIME_SHIFT_TS_INDEXER (object);

  switch (property_id) {
    case PROP_PCR_PID:
      indexer->pcr_pid = g_value_get_int (value);
      GST_INFO_OBJECT (indexer, "configured pcr-pid: %d(%x)",
          indexer->pcr_pid, indexer->pcr_pid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_time_shift_ts_indexer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstTimeShiftTsIndexer *indexer = GST_TIME_SHIFT_TS_INDEXER (object);

  switch (property_id) {
    case PROP_PCR_PID:
      g_value_set_int (value, indexer->pcr_pid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_time_shift_ts_indexer_dispose (GObject * object)
{
  /* GstTimeShiftTsIndexer *indexer = GST_TIME_SHIFT_TS_INDEXER (object); */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_time_shift_ts_indexer_finalize (GObject * object)
{
  /* GstTimeShiftTsIndexer *indexer = GST_TIME_SHIFT_TS_INDEXER (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_time_shift_ts_indexer_start (GstBaseTransform * trans)
{
  GstTimeShiftTsIndexer *indexer = GST_TIME_SHIFT_TS_INDEXER (trans);

  indexer->last_time = GST_CLOCK_TIME_NONE;
  indexer->base_time = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean
gst_time_shift_ts_indexer_stop (GstBaseTransform * trans)
{

  return TRUE;
}

static GstFlowReturn
gst_time_shift_ts_indexer_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstTimeShiftTsIndexer *indexer = GST_TIME_SHIFT_TS_INDEXER (trans);

  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  guint64 pcr;

  /* collect time info from that buffer */
  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    ret = GST_FLOW_NOT_SUPPORTED;
    goto out;
  }

  pcr = gst_time_shift_ts_indexer_get_pcr (indexer, map.data, map.size);
  if (pcr != -1) {
    if (!GST_CLOCK_TIME_IS_VALID (indexer->base_time)) {
      /* First time we receive is time zero */
      indexer->base_time = MPEGTIME_TO_GSTTIME (pcr);
    }
    GST_BUFFER_PTS(buf) = MPEGTIME_TO_GSTTIME (pcr) - indexer->base_time;
    fprintf(stderr, "Found PCR: %lli diff %lli\n", (long long int) MPEGTIME_TO_GSTTIME (pcr), (long long int) GST_BUFFER_PTS(buf) - indexer->last_time);
    indexer->last_time = GST_BUFFER_PTS(buf) - indexer->base_time;
  }
  else if (indexer->last_time != GST_CLOCK_TIME_NONE) {
    GST_BUFFER_PTS(buf) = indexer->last_time;
  }
  else {
    GST_BUFFER_PTS(buf) = GST_CLOCK_TIME_NONE;
  }
  gst_buffer_unmap(buf, &map);
out:
  return ret;
}

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
gst_time_shift_ts_indexer_parse_pcr (GstTimeShiftTsIndexer * ts, guint8 * data)
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

  return pcr;
}

static inline guint64
gst_time_shift_ts_indexer_get_pcr (GstTimeShiftTsIndexer * ts, guint8 * data,
    gsize size)
{
  guint64 pcr = (guint64) -1;
  gint i = 0;

  /* mpegtsparse pushes PES packet buffers so this case must be handled
   * without checking for next SYNC code */
  if (size >= TS_MIN_PACKET_SIZE && size <= TS_MAX_PACKET_SIZE) {
    pcr = gst_time_shift_ts_indexer_parse_pcr (ts, data);
  } else {
    while ((i + TS_MAX_PACKET_SIZE) < size) {
      if (TS_PACKET_SYNC_CODE == data[i]) {
        /* Check the next SYNC byte for all packets except the last packet
         * in a buffer... */
        if (G_LIKELY (is_next_sync_valid (data, size, i))) {
          pcr = gst_time_shift_ts_indexer_parse_pcr (ts, data + i);
          if (pcr == -1) {
            /* Skip to start of next TSPacket (pre-subract for the i++ later) */
            i += (TS_MIN_PACKET_SIZE - 1);
          } else {
            break;
          }
        }
      }
      i++;                      /* next byte in buffer until we find sync */
    }
  }
  return pcr;
}

