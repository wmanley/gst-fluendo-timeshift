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

#ifndef __FLUTSMPEGBIN_H__
#define __FLUTSMPEGBIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_FLUMPEGSHIFTER_BIN \
  (gst_flumpegshifter_bin_get_type())
#define GST_FLUMPEGSHIFTER_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FLUMPEGSHIFTER_BIN,GstFluMPEGShifterBin))
#define GST_FLUMPEGSHIFTER_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FLUMPEGSHIFTER_BIN,GstFluMPEGShifterBinClass))
#define GST_IS_FLUMPEGSHIFTER_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FLUMPEGSHIFTER_BIN))
#define GST_IS_FLUMPEGSHIFTER_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FLUMPEGSHIFTER_BIN))
#define GST_FLUMPEGSHIFTER_BIN_CAST(obj) ((GstFluMPEGShifterBin *) (obj))

typedef struct _GstFluMPEGShifterBin GstFluMPEGShifterBin;
typedef struct _GstFluMPEGShifterBinClass GstFluMPEGShifterBinClass;

struct _GstFluMPEGShifterBin
{
  GstBin parent_instance;

  GstElement * parser;
  GstElement * indexer;
  GstElement * timeshifter;
  GstElement * seeker;
};

struct _GstFluMPEGShifterBinClass
{
  GstBinClass parent_class;
};

GType gst_flumpegshifter_bin_get_type (void);

G_END_DECLS

#endif /* __FLUTSMPEGBIN_H__ */
