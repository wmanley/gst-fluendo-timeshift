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

static void
gst_flumpegshifter_class_init (GstFluMPEGShifterClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

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
