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

#include "gst-compat.h"

#ifndef __FLUCACHE_H__
#define __FLUCACHE_H__

G_BEGIN_DECLS

#define CACHE_SLOT_SIZE (32 * 1024)     /* Ring Buffer data unit size */
/**
 * GstShifterCache:
 *
 * Opaque data cache.
 *
 * Use the acessor functions to get the stored values.
 *
 */
typedef struct _GstShifterCache GstShifterCache;

GstShifterCache *gst_shifter_cache_new (gsize size, gchar * filename_template);

GstShifterCache *gst_shifter_cache_ref (GstShifterCache * cache);
void gst_shifter_cache_unref (GstShifterCache * cache);

gboolean gst_shifter_cache_push (GstShifterCache * cache, guint8 *data, gsize size);
GstBuffer *gst_shifter_cache_pop (GstShifterCache * cache, gboolean drain);

gboolean gst_shifter_cache_has_offset (GstShifterCache * cache, guint64 offset);
gboolean gst_shifter_cache_seek (GstShifterCache * cache, guint64 offset);

gboolean gst_shifter_cache_is_empty (GstShifterCache * cache);
guint64 gst_shifter_cache_fullness (GstShifterCache * cache);

G_END_DECLS

#endif /* __FLUCACHE_H__ */
