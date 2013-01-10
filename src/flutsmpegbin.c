/* GStreamer Time Shifting Bin
 * Copyright (C) 2012 YouView TV Ltd.
 *
 * Author: Krzysztof Konopko <krzysztof.konopko@youview.com>
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

#include "flucache.h"
#include "flutsmpegbin.h"
#include "flutsindex.h"

GST_DEBUG_CATEGORY_EXTERN (ts_mpeg_bin);
#define GST_CAT_DEFAULT ts_mpeg_bin

G_DEFINE_TYPE (GstFluMPEGShifterBin, gst_flumpegshifter_bin, GST_TYPE_BIN);

#define DEFAULT_MIN_CACHE_SIZE  (4 * CACHE_SLOT_SIZE)   /* 4 cache slots */
#define DEFAULT_CACHE_SIZE      (32 * 1024 * 1024)      /* 32 MB */

enum
{
  PROP_0,
  PROP_CACHE_SIZE,
  PROP_ALLOCATOR_NAME,
  PROP_LAST
};

static void
gst_flumpegshifter_bin_handle_message (GstBin * bin, GstMessage * msg);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts"));

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts"));

static void
gst_flumpegshifter_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFluMPEGShifterBin *ts_bin = GST_FLUMPEGSHIFTER_BIN (object);

  switch (prop_id) {
    case PROP_CACHE_SIZE:
      g_object_set_property (G_OBJECT (ts_bin->timeshifter),
          "cache-size", value);
      break;

    case PROP_ALLOCATOR_NAME:
      g_object_set_property (G_OBJECT (ts_bin->timeshifter),
          "allocator-name", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_flumpegshifter_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFluMPEGShifterBin *ts_bin = GST_FLUMPEGSHIFTER_BIN (object);

  switch (prop_id) {
    case PROP_CACHE_SIZE:
      g_object_get_property (G_OBJECT (ts_bin->timeshifter),
          "cache-size", value);
      break;

    case PROP_ALLOCATOR_NAME:
      g_object_get_property (G_OBJECT (ts_bin->timeshifter),
          "allocator-name", value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_flumpegshifter_bin_class_init (GstFluMPEGShifterBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbin_class = GST_BIN_CLASS (klass);

  gobject_class->set_property = gst_flumpegshifter_bin_set_property;
  gobject_class->get_property = gst_flumpegshifter_bin_get_property;

  g_object_class_install_property (gobject_class, PROP_CACHE_SIZE,
      g_param_spec_uint64 ("cache-size",
          "Cache size in bytes",
          "Max. amount of data cached in memory (bytes)",
          DEFAULT_MIN_CACHE_SIZE, G_MAXUINT64, DEFAULT_CACHE_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALLOCATOR_NAME,
      g_param_spec_string ("allocator-name", "Allocator name",
          "The allocator to be used to allocate space for "
          "the ring buffer (NULL - default system allocator).",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gstbin_class->handle_message =
      GST_DEBUG_FUNCPTR (gst_flumpegshifter_bin_handle_message);

  gst_element_class_set_metadata (gstelement_class,
      "Fluendo Time Shift + TS parser for MPEG TS streams", "Generic/Bin",
      "Provide time shift operations on MPEG TS streams",
      "Krzysztof Konopko <krzysztof.konopko@youview.com>");
}

static void
mirror_pad (GstElement * element, const gchar * static_pad_name, GstBin * bin)
{
  GstPad *orig_pad, *ghost_pad;

  orig_pad = gst_element_get_static_pad (element, static_pad_name);
  g_return_if_fail (orig_pad);

  ghost_pad = gst_ghost_pad_new (static_pad_name, orig_pad);
  gst_object_unref (orig_pad);
  g_return_if_fail (ghost_pad);

  g_return_if_fail (gst_element_add_pad (GST_ELEMENT (bin), ghost_pad));
}

static void
gst_element_clear(GstElement ** elem)
{
  g_return_if_fail (!elem);
  if (*elem)
  {
    g_object_unref(G_OBJECT(*elem));
    *elem = NULL;
  }
}

static void
gst_flumpegshifter_bin_init (GstFluMPEGShifterBin * ts_bin)
{
  GstBin *bin = GST_BIN (ts_bin);

  ts_bin->parser = gst_element_factory_make ("tsparse", "parser");
  ts_bin->indexer = gst_element_factory_make ("timeshifttsindexer", "indexer");
  ts_bin->timeshifter =
      gst_element_factory_make ("flumpegshifter", "timeshifter");
  ts_bin->seeker = gst_element_factory_make ("timeshiftseeker", "seeker");
  if (!ts_bin->parser || !ts_bin->indexer || !ts_bin->timeshifter
      || !ts_bin->seeker)
    goto error;

  gst_bin_add_many (bin, ts_bin->parser, ts_bin->indexer, ts_bin->timeshifter,
          ts_bin->seeker, NULL);
  g_return_if_fail (gst_element_link_many (ts_bin->parser, ts_bin->indexer,
          ts_bin->timeshifter, ts_bin->seeker, NULL));

  GstIndex * index = gst_index_factory_make ("memindex");
  g_object_set (G_OBJECT (ts_bin->indexer), "index", index, NULL);
  g_object_set (G_OBJECT (ts_bin->seeker), "index", index, NULL);
  g_object_unref (index);

  mirror_pad (ts_bin->parser, "sink", bin);
  mirror_pad (ts_bin->seeker, "src", bin);

  return;
error:
  gst_element_clear (&ts_bin->parser);
  gst_element_clear (&ts_bin->timeshifter);
  gst_element_clear (&ts_bin->seeker);
}

static void
gst_flumpegshifter_bin_handle_message (GstBin * bin, GstMessage * msg)
{
  GstFluMPEGShifterBin *ts_bin = GST_FLUMPEGSHIFTER_BIN (bin);

  if (gst_message_has_name (msg, "pmt")) {
    guint pcr_pid;

    const GstStructure *gs = gst_message_get_structure (msg);

    if (!gst_structure_get_uint (gs, "pcr-pid", &pcr_pid)) {
      GST_ERROR ("Cannot extract PCR PID");
    }

    GST_DEBUG ("Setting PCR PID: %u", pcr_pid);
    g_object_set (ts_bin->indexer, "pcr-pid", pcr_pid, NULL);
  }

  GST_BIN_CLASS (gst_flumpegshifter_bin_parent_class)
      ->handle_message (bin, msg);
}
