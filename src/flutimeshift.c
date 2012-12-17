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

#include "flutsfake.h"
#include "flutsmpeg.h"
#include "flutsmpegbin.h"

GST_DEBUG_CATEGORY (ts_base);
GST_DEBUG_CATEGORY (ts_flow);
GST_DEBUG_CATEGORY (ts_fake);
GST_DEBUG_CATEGORY (ts_mpeg);
GST_DEBUG_CATEGORY (ts_mpeg_bin);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (ts_base, "flubaseshifter", 0,
      "Fluendo Time Shift element");

  GST_DEBUG_CATEGORY_INIT (ts_flow, "flushifter_flow", 0,
      "dataflow in the Time Shift element");

  GST_DEBUG_CATEGORY_INIT (ts_fake, "flufakeshifter", 0,
      "Fluendo Fake Time Shifting");

  GST_DEBUG_CATEGORY_INIT (ts_mpeg, "flumpegshifter", 0,
      "Fluendo MPEG Time Shifting");

  GST_DEBUG_CATEGORY_INIT (ts_mpeg_bin, "flumpegshifterbin", 0,
      "Fluendo MPEG Time Shifting bin");

  if (!gst_element_register (plugin, "flufakeshifter", GST_RANK_NONE,
          gst_flufakeshifter_get_type ()))
    return FALSE;

  if (!gst_element_register (plugin, "flumpegshifter", GST_RANK_NONE,
          gst_flumpegshifter_get_type ()))
    return FALSE;

  if (!gst_element_register (plugin, "flumpegshifterbin", GST_RANK_NONE,
          gst_flumpegshifter_bin_get_type ()))
    return FALSE;

  return TRUE;
}

#if GST_CHECK_VERSION (1,0,0)
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    flutimeshift, "Fluendo Time Shift element",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME,
    "http://www.fluendo.com, http://www.youview.com");
#else
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "flutimeshift", "Fluendo Time Shift element",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME,
    "http://www.fluendo.com");
#endif
