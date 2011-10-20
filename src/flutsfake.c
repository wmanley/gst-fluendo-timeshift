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

#include "flutsfake.h"

GST_DEBUG_CATEGORY_EXTERN (ts_fake);
#define GST_CAT_DEFAULT ts_fake

GST_BOILERPLATE (GstFluFakeShifter, gst_flufakeshifter, GstFluTSBase,
    GST_FLUTSBASE_TYPE);

static GstElementDetails flufakeshifter_details = {
  "Fluendo Time Shift for fake streams",
  "Generic",
  "Provide time shift operations on fake streams",
  "Fluendo S.A. <support@fluendo.com>"
};

static GstStaticPadTemplate flufakeshifter_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate flufakeshifter_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static void
gst_flufakeshifter_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&flufakeshifter_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&flufakeshifter_sink_factory));
  gst_element_class_set_details (element_class, &flufakeshifter_details);
}

static void
gst_flufakeshifter_class_init (GstFluFakeShifterClass * klass)
{
  //GstFluTSBaseClass *base_class = GST_FLUTSBASE_CLASS (klass);

}

static void
gst_flufakeshifter_init (GstFluFakeShifter * ts,
    GstFluFakeShifterClass * g_class)
{
  gst_flutsbase_add_pads (GST_FLUTSBASE (ts));
}
