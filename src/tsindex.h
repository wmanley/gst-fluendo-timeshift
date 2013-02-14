/* GStreamer MPEG TS Time Shifting
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * tsindex.h: Header for GstTSIndex, base class to handle efficient
 *               storage or caching of seeking information.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __TS_INDEX_H__
#define __TS_INDEX_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_TS_INDEX                  (gst_ts_index_get_type ())
#define GST_TS_INDEX(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TS_INDEX, GstTSIndex))
#define GST_IS_TS_INDEX(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TS_INDEX))
#define GST_TS_INDEX_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TS_INDEX, GstTSIndexClass))
#define GST_IS_TS_INDEX_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TS_INDEX))
#define GST_TS_INDEX_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TS_INDEX, GstTSIndexClass))
#define GST_TYPE_TS_INDEX_ENTRY            (gst_ts_index_entry_get_type())
typedef struct _GstTSIndexEntry GstTSIndexEntry;
typedef struct _GstTSIndex GstTSIndex;
typedef struct _GstTSIndexClass GstTSIndexClass;

/**
 * GstTSIndexEntryType:
 * @GST_TS_INDEX_ENTRY_ID: This entry is an id that maps an index id to its owner object
 * @GST_TS_INDEX_ENTRY_ASSOCIATION: This entry is an association between formats
 *
 * The different types of entries in the index.
 */
typedef enum
{
  GST_TS_INDEX_ENTRY_ID,
  GST_TS_INDEX_ENTRY_ASSOCIATION,
} GstTSIndexEntryType;

/**
 * GstTSIndexLookupMethod:
 * @GST_TS_INDEX_LOOKUP_EXACT: There has to be an exact indexentry with the given format/value
 * @GST_TS_INDEX_LOOKUP_BEFORE: The exact entry or the one before it
 * @GST_TS_INDEX_LOOKUP_AFTER: The exact entry or the one after it
 *
 * Specify the method to find an index entry in the index.
 */
typedef enum
{
  GST_TS_INDEX_LOOKUP_EXACT,
  GST_TS_INDEX_LOOKUP_BEFORE,
  GST_TS_INDEX_LOOKUP_AFTER
} GstTSIndexLookupMethod;

/**
 * GST_TS_INDEX_NASSOCS:
 * @entry: The entry to query
 *
 * Get the number of associations in the entry.
 */
#define GST_TS_INDEX_NASSOCS(entry)                ((entry)->data.assoc.nassocs)

/**
 * GST_TS_INDEX_ASSOC_FLAGS:
 * @entry: The entry to query
 *
 *  Get the flags for this entry.
 */
#define GST_TS_INDEX_ASSOC_FLAGS(entry)            ((entry)->data.assoc.flags)

/**
 * GST_TS_INDEX_ASSOC_FORMAT:
 * @entry: The entry to query
 * @i: The format index
 *
 * Get the i-th format of the entry.
 */
#define GST_TS_INDEX_ASSOC_FORMAT(entry,i)         ((entry)->data.assoc.assocs[(i)].format)

/**
 * GST_TS_INDEX_ASSOC_VALUE:
 * @entry: The entry to query
 * @i: The value index
 *
 * Get the i-th value of the entry.
 */
#define GST_TS_INDEX_ASSOC_VALUE(entry,i)          ((entry)->data.assoc.assocs[(i)].value)

typedef struct _GstTSIndexAssociation GstTSIndexAssociation;

/**
 * GstTSIndexAssociation:
 * @format: the format of the association
 * @value: the value of the association
 *
 * An association in an entry.
 */
struct _GstTSIndexAssociation
{
  GstFormat format;
  gint64 value;
};

/**
 * GstTSIndexAssociationFlags:
 * @GST_TS_INDEX_ASSOCIATION_FLAG_NONE: no extra flags
 * @GST_TS_INDEX_ASSOCIATION_FLAG_KEY_UNIT: the entry marks a key unit, a key unit is one
 *  that marks a place where one can randomly seek to.
 * @GST_TS_INDEX_ASSOCIATION_FLAG_DELTA_UNIT: the entry marks a delta unit, a delta unit
 *  is one that marks a place where one can relatively seek to.
 * @GST_TS_INDEX_ASSOCIATION_FLAG_LAST: extra user defined flags should start here.
 *
 * Flags for an association entry.
 */
typedef enum
{
  GST_TS_INDEX_ASSOCIATION_FLAG_NONE = 0,
  GST_TS_INDEX_ASSOCIATION_FLAG_KEY_UNIT = (1 << 0),
  GST_TS_INDEX_ASSOCIATION_FLAG_DELTA_UNIT = (1 << 1),
  GST_TS_INDEX_ASSOCIATION_FLAG_LAST = (1 << 8)
} GstTSIndexAssociationFlags;

/**
 * GST_TS_INDEX_ID_INVALID:
 *
 * Constant for an invalid index id
 */
#define GST_TS_INDEX_ID_INVALID                    (-1)

/**
 * GST_TS_INDEX_ID_DESCRIPTION:
 * @entry: The entry to query
 *
 * Get the description of the id entry
 */
#define GST_TS_INDEX_ID_DESCRIPTION(entry)         ((entry)->data.id.description)

/**
 * GstTSIndexEntry:
 *
 * The basic element of an index.
 */
struct _GstTSIndexEntry
{
  /*< private > */
  GstTSIndexEntryType type;
  gint id;

  union
  {
    struct
    {
      gchar *description;
    } id;
    struct
    {
      gint nassocs;
      GstTSIndexAssociation *assocs;
      GstTSIndexAssociationFlags flags;
    } assoc;
  } data;
};

/**
 * GstTSIndexFlags:
 * @GST_TS_INDEX_WRITABLE: The index is writable
 * @GST_TS_INDEX_READABLE: The index is readable
 * @GST_TS_INDEX_FLAG_LAST: First flag that can be used by subclasses
 *
 * Flags for this index
 */
typedef enum
{
  GST_TS_INDEX_WRITABLE = (GST_OBJECT_FLAG_LAST << 0),
  GST_TS_INDEX_READABLE = (GST_OBJECT_FLAG_LAST << 1),
  GST_TS_INDEX_FLAG_LAST = (GST_OBJECT_FLAG_LAST << 8)
} GstTSIndexFlags;

/**
 * GST_TS_INDEX_IS_READABLE:
 * @obj: The index to check
 *
 * Check if the index can be read from
 */
#define GST_TS_INDEX_IS_READABLE(obj)    (GST_OBJECT_FLAG_IS_SET (obj, GST_TS_INDEX_READABLE))

/**
 * GST_TS_INDEX_IS_WRITABLE:
 * @obj: The index to check
 *
 * Check if the index can be written to
 */
#define GST_TS_INDEX_IS_WRITABLE(obj)    (GST_OBJECT_FLAG_IS_SET (obj, GST_TS_INDEX_WRITABLE))

/**
 * GstTSIndex:
 *
 * Opaque #GstTSIndex structure.
 */
struct _GstTSIndex
{
  GstObject object;

  /*< private > */
  /* TODO: Remove the concept of seperate writers */
  GHashTable *writers;
  gint last_id;
  gint id;
};

struct _GstTSIndexClass
{
  GstObjectClass parent_class;

  /*< protected > */
    gboolean (*get_writer_id) (GstTSIndex * index, gint * id, gchar * writer);

  /* abstract methods */
  void (*add_entry) (GstTSIndex * index, GstTSIndexEntry * entry);

  GstTSIndexEntry *(*get_assoc_entry) (GstTSIndex * index, gint id,
      GstTSIndexLookupMethod method, GstTSIndexAssociationFlags flags,
      GstFormat format, gint64 value,
      GCompareDataFunc func, gpointer user_data);
};

GType gst_ts_index_get_type (void);

GstTSIndex *gst_ts_memindex_new (void);

GstTSIndexEntry *gst_ts_index_add_associationv (GstTSIndex * index,
    GstTSIndexAssociationFlags flags, gint n,
    const GstTSIndexAssociation * list);

GstTSIndexEntry *gst_ts_index_add_id (GstTSIndex * index, gint id,
    gchar * description);

GstTSIndexEntry *gst_ts_index_get_assoc_entry (GstTSIndex * index,
    GstTSIndexLookupMethod method,
    GstTSIndexAssociationFlags flags, GstFormat format, gint64 value);

GstTSIndexEntry *gst_ts_index_get_assoc_entry_full (GstTSIndex * index,
    GstTSIndexLookupMethod method,
    GstTSIndexAssociationFlags flags, GstFormat format, gint64 value,
    GCompareDataFunc func, gpointer user_data);

/* working with index entries */
GType gst_ts_index_entry_get_type (void);

GstTSIndexEntry *gst_ts_index_entry_copy (GstTSIndexEntry * entry);

void gst_ts_index_entry_free (GstTSIndexEntry * entry);

gboolean gst_ts_index_entry_assoc_map (GstTSIndexEntry * entry,
    GstFormat format, gint64 * value);

#define GstIndex GstTSIndex
#define GST_TYPE_INDEX GST_TYPE_TS_INDEX
#define GstIndexEntry GstTSIndexEntry
#define GstIndexAssociation GstTSIndexAssociation

#define gst_index_factory_make(name) gst_ts_memindex_new()
#define gst_index_get_writer_id gst_ts_index_get_writer_id
#define gst_index_add_associationv gst_ts_index_add_associationv
#define gst_index_get_assoc_entry gst_ts_index_get_assoc_entry
#define gst_index_entry_assoc_map gst_ts_index_entry_assoc_map

#define GST_ASSOCIATION_FLAG_NONE GST_TS_INDEX_ASSOCIATION_FLAG_NONE
#define GST_INDEX_LOOKUP_BEFORE GST_TS_INDEX_LOOKUP_BEFORE

G_END_DECLS
#endif /* __TS_INDEX_H__ */
