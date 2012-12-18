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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstmpegtsindexer
 *
 * The mpegtsindexer element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! mpegtsindexer ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "gstmpegtsindexer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mpegts_indexer_debug_category);
#define GST_CAT_DEFAULT gst_mpegts_indexer_debug_category

/* prototypes */


static void gst_mpegts_indexer_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mpegts_indexer_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mpegts_indexer_dispose (GObject * object);
static void gst_mpegts_indexer_finalize (GObject * object);

static gboolean gst_mpegts_indexer_start (GstBaseTransform * trans);
static gboolean gst_mpegts_indexer_stop (GstBaseTransform * trans);
static GstFlowReturn
gst_mpegts_indexer_transform_ip (GstBaseTransform * trans, GstBuffer * buf);

enum
{
  PROP_0,
  PROP_INDEX,
  PROP_PCR_PID,
  PROP_DELTA
};

/* pad templates */

static GstStaticPadTemplate gst_mpegts_indexer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts")
    );

static GstStaticPadTemplate gst_mpegts_indexer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts")
    );


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_mpegts_indexer_debug_category, "mpegtsindexer", 0, \
      "debug category for mpegtsindexer element");

GST_BOILERPLATE_FULL (GstMpegtsIndexer, gst_mpegts_indexer, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void
gst_mpegts_indexer_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mpegts_indexer_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mpegts_indexer_src_template));

  gst_element_class_set_static_metadata (element_class, "MPEG-TS indexer",
      "Generic", "Indexes MPEG-TS to allow fast time based seeking",
      "Josep Torra <josep@fluendo.com>, "
      "Krzysztof Konopko <krzysztof.konopko@youview.com>, "
      "William Manley <william.manley@youview.com>");
}

static void
gst_mpegts_indexer_class_init (GstMpegtsIndexerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  /* properties */
  g_object_class_install_property (gclass, PROP_INDEX,
      g_param_spec_object ("index", "Index",
          "The index into which to write indexing information",
          GST_TYPE_INDEX, (G_PARAM_READABLE | G_PARAM_WRITABLE)));
  
  g_object_class_install_property (gclass, PROP_PCR_PID,
      g_param_spec_int ("pcr-pid", "PCR pid",
          "Defines the PCR pid to collect the time (-1 = undefined)",
          INVALID_PID, 0x1fff, INVALID_PID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gclass, PROP_DELTA,
      g_param_spec_int ("delta", "Delta",
          "Delta time between index entries in miliseconds "
          "(-1 = use random access flag)",
          -1, 10000, DEFAULT_DELTA,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Base class method overrides */
  gobject_class->set_property = gst_mpegts_indexer_set_property;
  gobject_class->get_property = gst_mpegts_indexer_get_property;
  gobject_class->dispose = gst_mpegts_indexer_dispose;
  gobject_class->finalize = gst_mpegts_indexer_finalize;

  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_mpegts_indexer_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_mpegts_indexer_stop);
  base_transform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_mpegts_indexer_transform_ip);
  base_transform_class->passthrough_on_same_caps = TRUE;

  /* FIXME: Would it be better with this: */
  /*base_transform_class->prepare_output_buffer = GST_DEBUG_FUNCPTR (gst_mpegts_indexer_prepare_output_buffer);*/
}

static void
gst_mpegts_indexer_init (GstMpegtsIndexer * mpegtsindexer, GstMpegtsIndexerClass * mpegtsindexer_class)
{
  mpegtsindexer->sinkpad = gst_pad_new_from_static_template (
      &gst_mpegts_indexer_sink_template, "sink");
  mpegtsindexer->srcpad = gst_pad_new_from_static_template (
      &gst_mpegts_indexer_src_template, "src");

  ts->pcr_pid = INVALID_PID;
  ts->delta = DEFAULT_DELTA;

  ts->base_time = GST_CLOCK_TIME_NONE;
  ts->last_pcr = 0;
  ts->last_time = GST_CLOCK_TIME_NONE;
  ts->current_offset = 0;
}

static void
gst_mpegts_indexer_replace_index(GstMpegtsIndexer *mpegtsindexer,
    GstIndex * new_index)
{
  if (mpegtsindexer->index) {
    gst_object_unref (mpegtsindexer->index);
    mpegtsindexer->index = NULL;
    mpegtsindexer->index_id = 0;
  }
  if (new_index) {
    gst_object_ref (new_index);
    mpegtsindexer->index = new_index;
    gst_index_get_writer_id (mpegtsindexer->index, GST_OBJECT (mpegtsindexer),
      &mpegtsindexer->index_id);
  }
}

void
gst_mpegts_indexer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpegtsIndexer *mpegtsindexer = GST_MPEGTS_INDEXER (object); 
  /* FIXME: Add locking */
  switch (property_id) {
    case PROP_INDEX:
      gst_mpegts_indexer_replace_index(mpegtsindexer,
          g_value_dup_object(value));
      break;
    case PROP_PCR_PID:
      mpegtsindexer->pcr_pid = g_value_get_int (value);
      GST_INFO_OBJECT (mpegtsindexer, "configured pcr-pid: %d(%x)",
          mpegtsindexer->pcr_pid, mpegtsindexer->pcr_pid);
      break;
    case PROP_DELTA:
      mpegtsindexer->delta = g_value_get_int (value);
      if (mpegtsindexer->delta != -1) {
        mpegtsindexer->delta *= GST_MSECOND;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mpegts_indexer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpegtsIndexer *mpegtsindexer = GST_MPEGTS_INDEXER (object);

  /* FIXME: Add locking */
  switch (property_id) {
    case PROP_INDEX:
      g_value_set_object(mpegtsindexer->index);
      break;
    case PROP_PCR_PID:
      g_value_set_int (value, mpegtsindexer->pcr_pid);
      break;
    case PROP_DELTA:
      if (mpegtsindexer->delta != -1) {
        g_value_set_int (value, mpegtsindexer->delta / GST_MSECOND);
      } else {
        g_value_set_int (value, -1);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mpegts_indexer_dispose (GObject * object)
{
  GstMpegtsIndexer *mpegtsindexer = GST_MPEGTS_INDEXER (object);

  /* clean up as possible.  may be called multiple times */
  gst_mpegts_indexer_replace_index(mpegtsindexer, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_mpegts_indexer_finalize (GObject * object)
{
  /* GstMpegtsIndexer *mpegtsindexer = GST_MPEGTS_INDEXER (object); */

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_mpegts_indexer_start (GstBaseTransform * trans)
{
  if (G_UNLIKELY (!mpegtsindexer->index)) {
    GST_DEBUG_OBJECT (mpegtsindexer, "no index provided creating our own");

    gst_mpegts_indexer_replace_index(mpegtsindexer,
        gst_index_factory_make ("memindex"));
  }

  return FALSE;
}

static gboolean
gst_mpegts_indexer_stop (GstBaseTransform * trans)
{

  return FALSE;
}

static GstFlowReturn
gst_mpegts_indexer_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  gst_buffer_map (buffer, &map, GST_MAP_READ);
  res = gst_flumpegshifter_collect_time (ts, map.data, map.size);
  gst_buffer_unmap (buffer, &map);

  return GST_FLOW_ERROR;
}

/*
FIXME: Do I need this one?
static GstFlowReturn
gst_mpegts_indexer_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf)
{

  return GST_FLOW_ERROR;
}
*/

static gboolean
plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, "mpegtsindexer", GST_RANK_NONE,
      GST_TYPE_MPEGTS_INDEXER);
}

#ifndef VERSION
#define VERSION "0.0.FIXME"
#endif
#ifndef PACKAGE
#define PACKAGE "FIXME_package"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "FIXME_package_name"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://FIXME.org/"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mpegtsindexer,
    "FIXME plugin description",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)

