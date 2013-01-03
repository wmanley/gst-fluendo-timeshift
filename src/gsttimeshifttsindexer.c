/* GStreamer
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

#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/base/gstbasetransform.h>
#include "gsttimeshifttsindexer.h"

GST_DEBUG_CATEGORY_STATIC (gst_time_shift_ts_indexer_debug_category);
#define GST_CAT_DEFAULT gst_time_shift_ts_indexer_debug_category

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

enum
{
  PROP_0
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

  g_object_class_install_property (gobject_class, PROP_INDEX,
      g_param_spec_object ("index", "Index",
          "The index into which to write indexing information",
          GST_TYPE_INDEX, (G_PARAM_READABLE | G_PARAM_WRITABLE)));
  g_object_class_install_property (gobject_class, PROP_PCR_PID,
      g_param_spec_int ("pcr-pid", "PCR pid",
          "Defines the PCR pid to collect the time (-1 = undefined)",
          INVALID_PID, 0x1fff, INVALID_PID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DELTA,
      g_param_spec_int ("delta", "Delta",
          "Delta time between index entries in miliseconds "
          "(-1 = use random access flag)",
          -1, 10000, DEFAULT_DELTA,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_time_shift_ts_indexer_init (GstTimeShiftTsIndexer * indexer)
{
  GstBaseTransform *base = GST_BASE_TRANSFORM (indexer);
  gst_base_transform_set_passthrough(base, TRUE);
}

void
gst_time_shift_ts_indexer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstTimeShiftTsIndexer *timeshifttsindexer = GST_TIME_SHIFT_TS_INDEXER (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_time_shift_ts_indexer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstTimeShiftTsIndexer *timeshifttsindexer = GST_TIME_SHIFT_TS_INDEXER (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_time_shift_ts_indexer_dispose (GObject * object)
{
  /* GstTimeShiftTsIndexer *timeshifttsindexer = GST_TIME_SHIFT_TS_INDEXER (object); */

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_time_shift_ts_indexer_finalize (GObject * object)
{
  /* GstTimeShiftTsIndexer *timeshifttsindexer = GST_TIME_SHIFT_TS_INDEXER (object); */

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_time_shift_ts_indexer_start (GstBaseTransform * trans)
{

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

  return GST_FLOW_OK;
}

