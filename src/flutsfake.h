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

#ifndef __FLUTSFAKE_H__
#define __FLUTSFAKE_H__

#include "flutsbase.h"

G_BEGIN_DECLS

#define GST_FLUFAKESHIFTER_TYPE \
  (gst_flufakeshifter_get_type())
#define GST_FLUFAKESHIFTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_FLUFAKESHIFTER_TYPE,GstFluFakeShifter))
#define GST_FLUFAKESHIFTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_FLUFAKESHIFTER_TYPE,GstFluFakeShifterClass))
#define GST_IS_FLUTSFAKE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_FLUFAKESHIFTER_TYPE))
#define GST_IS_FLUTSFAKE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_FLUFAKESHIFTER_TYPE))
#define GST_FLUFAKESHIFTER_CAST(obj) \
  ((GstFluFakeShifter *)(obj))
typedef struct _GstFluFakeShifter GstFluFakeShifter;
typedef struct _GstFluFakeShifterClass GstFluFakeShifterClass;

struct _GstFluFakeShifter
{
  GstFluTSBase base;
};

struct _GstFluFakeShifterClass
{
  GstFluTSBaseClass base_class;
};

GType gst_flufakeshifter_get_type (void);

G_END_DECLS

#endif /* __FLUTSFAKE_H__ */
