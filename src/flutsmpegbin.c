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

#include "flutsmpegbin.h"

GST_DEBUG_CATEGORY_EXTERN (ts_mpeg_bin);
#define GST_CAT_DEFAULT ts_mpeg_bin

G_DEFINE_TYPE (GstFluMPEGShifterBin, gst_flumpegshifter_bin, GST_TYPE_BIN);

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
gst_flumpegshifter_bin_class_init (GstFluMPEGShifterBinClass * klass)
{
  //GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  //gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbin_class = GST_BIN_CLASS (klass);

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
gst_flumpegshifter_bin_init (GstFluMPEGShifterBin * ts_bin)
{
  GstBin *bin = GST_BIN (ts_bin);

  ts_bin->parser = gst_element_factory_make ("tsparse", "parser");
  g_return_if_fail (ts_bin->parser);

  ts_bin->timeshifter =
      gst_element_factory_make ("flumpegshifter", "timeshifter");
  g_return_if_fail (ts_bin->timeshifter);

  gst_bin_add_many (bin, ts_bin->parser, ts_bin->timeshifter, NULL);
  g_return_if_fail (gst_element_link_many (ts_bin->parser, ts_bin->timeshifter,
          NULL));

  mirror_pad (ts_bin->parser, "sink", bin);
  mirror_pad (ts_bin->timeshifter, "src", bin);
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
    g_object_set (ts_bin->timeshifter, "pcr-pid", pcr_pid, NULL);
  }

  GST_BIN_CLASS (gst_flumpegshifter_bin_parent_class)
      ->handle_message (bin, msg);
}
