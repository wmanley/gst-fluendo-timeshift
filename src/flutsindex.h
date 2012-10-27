/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * flutsindex.h: Header for GstFluTSIndex, base class to handle efficient
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __FLUTSINDEX_H__
#define __FLUTSINDEX_H__

G_BEGIN_DECLS
#define GST_TYPE_FLUTSINDEX                  (gst_flutsindex_get_type ())
#define GST_FLUTSINDEX(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FLUTSINDEX, GstFluTSIndex))
#define GST_IS_FLUTSINDEX(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FLUTSINDEX))
#define GST_FLUTSINDEX_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_FLUTSINDEX, GstFluTSIndexClass))
#define GST_IS_FLUTSINDEX_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_FLUTSINDEX))
#define GST_FLUTSINDEX_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_FLUTSINDEX, GstFluTSIndexClass))
#define GST_TYPE_FLUTSINDEX_ENTRY            (gst_flutsindex_entry_get_type())
typedef struct _GstFluTSIndexEntry GstFluTSIndexEntry;
typedef struct _GstFluTSIndex GstFluTSIndex;
typedef struct _GstFluTSIndexClass GstFluTSIndexClass;

/**
 * GstFluTSIndexEntryType:
 * @GST_FLUTSINDEX_ENTRY_ID: This entry is an id that maps an index id to its owner object
 * @GST_FLUTSINDEX_ENTRY_ASSOCIATION: This entry is an association between formats
 *
 * The different types of entries in the index.
 */
typedef enum
{
  GST_FLUTSINDEX_ENTRY_ID,
  GST_FLUTSINDEX_ENTRY_ASSOCIATION,
} GstFluTSIndexEntryType;

/**
 * GstFluTSIndexLookupMethod:
 * @GST_FLUTSINDEX_LOOKUP_EXACT: There has to be an exact indexentry with the given format/value
 * @GST_FLUTSINDEX_LOOKUP_BEFORE: The exact entry or the one before it
 * @GST_FLUTSINDEX_LOOKUP_AFTER: The exact entry or the one after it
 *
 * Specify the method to find an index entry in the index.
 */
typedef enum
{
  GST_FLUTSINDEX_LOOKUP_EXACT,
  GST_FLUTSINDEX_LOOKUP_BEFORE,
  GST_FLUTSINDEX_LOOKUP_AFTER
} GstFluTSIndexLookupMethod;

/**
 * GST_FLUTSINDEX_NASSOCS:
 * @entry: The entry to query
 *
 * Get the number of associations in the entry.
 */
#define GST_FLUTSINDEX_NASSOCS(entry)                ((entry)->data.assoc.nassocs)

/**
 * GST_FLUTSINDEX_ASSOC_FLAGS:
 * @entry: The entry to query
 *
 *  Get the flags for this entry.
 */
#define GST_FLUTSINDEX_ASSOC_FLAGS(entry)            ((entry)->data.assoc.flags)

/**
 * GST_FLUTSINDEX_ASSOC_FORMAT:
 * @entry: The entry to query
 * @i: The format index
 *
 * Get the i-th format of the entry.
 */
#define GST_FLUTSINDEX_ASSOC_FORMAT(entry,i)         ((entry)->data.assoc.assocs[(i)].format)

/**
 * GST_FLUTSINDEX_ASSOC_VALUE:
 * @entry: The entry to query
 * @i: The value index
 *
 * Get the i-th value of the entry.
 */
#define GST_FLUTSINDEX_ASSOC_VALUE(entry,i)          ((entry)->data.assoc.assocs[(i)].value)

typedef struct _GstFluTSIndexAssociation GstFluTSIndexAssociation;

/**
 * GstFluTSIndexAssociation:
 * @format: the format of the association
 * @value: the value of the association
 *
 * An association in an entry.
 */
struct _GstFluTSIndexAssociation
{
  GstFormat format;
  gint64 value;
};

/**
 * GstFluTSIndexAssociationFlags:
 * @GST_FLUTSINDEX_ASSOCIATION_FLAG_NONE: no extra flags
 * @GST_FLUTSINDEX_ASSOCIATION_FLAG_KEY_UNIT: the entry marks a key unit, a key unit is one
 *  that marks a place where one can randomly seek to.
 * @GST_FLUTSINDEX_ASSOCIATION_FLAG_DELTA_UNIT: the entry marks a delta unit, a delta unit
 *  is one that marks a place where one can relatively seek to.
 * @GST_FLUTSINDEX_ASSOCIATION_FLAG_LAST: extra user defined flags should start here.
 *
 * Flags for an association entry.
 */
typedef enum
{
  GST_FLUTSINDEX_ASSOCIATION_FLAG_NONE = 0,
  GST_FLUTSINDEX_ASSOCIATION_FLAG_KEY_UNIT = (1 << 0),
  GST_FLUTSINDEX_ASSOCIATION_FLAG_DELTA_UNIT = (1 << 1),
  GST_FLUTSINDEX_ASSOCIATION_FLAG_LAST = (1 << 8)
} GstFluTSIndexAssociationFlags;

/**
 * GST_FLUTSINDEX_ID_INVALID:
 *
 * Constant for an invalid index id
 */
#define GST_FLUTSINDEX_ID_INVALID                    (-1)

/**
 * GST_FLUTSINDEX_ID_DESCRIPTION:
 * @entry: The entry to query
 *
 * Get the description of the id entry
 */
#define GST_FLUTSINDEX_ID_DESCRIPTION(entry)         ((entry)->data.id.description)

/**
 * GstFluTSIndexEntry:
 *
 * The basic element of an index.
 */
struct _GstFluTSIndexEntry
{
  /*< private > */
  GstFluTSIndexEntryType type;
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
      GstFluTSIndexAssociation *assocs;
      GstFluTSIndexAssociationFlags flags;
    } assoc;
  } data;
};

/**
 * GstFluTSIndexFlags:
 * @GST_FLUTSINDEX_WRITABLE: The index is writable
 * @GST_FLUTSINDEX_READABLE: The index is readable
 * @GST_FLUTSINDEX_FLAG_LAST: First flag that can be used by subclasses
 *
 * Flags for this index
 */
typedef enum
{
  GST_FLUTSINDEX_WRITABLE = (GST_OBJECT_FLAG_LAST << 0),
  GST_FLUTSINDEX_READABLE = (GST_OBJECT_FLAG_LAST << 1),
  GST_FLUTSINDEX_FLAG_LAST = (GST_OBJECT_FLAG_LAST << 8)
} GstFluTSIndexFlags;

/**
 * GST_FLUTSINDEX_IS_READABLE:
 * @obj: The index to check
 *
 * Check if the index can be read from
 */
#define GST_FLUTSINDEX_IS_READABLE(obj)    (GST_OBJECT_FLAG_IS_SET (obj, GST_FLUTSINDEX_READABLE))

/**
 * GST_FLUTSINDEX_IS_WRITABLE:
 * @obj: The index to check
 *
 * Check if the index can be written to
 */
#define GST_FLUTSINDEX_IS_WRITABLE(obj)    (GST_OBJECT_FLAG_IS_SET (obj, GST_FLUTSINDEX_WRITABLE))

/**
 * GstFluTSIndex:
 *
 * Opaque #GstFluTSIndex structure.
 */
struct _GstFluTSIndex
{
  GstObject object;

  /*< private > */
  GHashTable *writers;
  gint last_id;
};

struct _GstFluTSIndexClass
{
  GstObjectClass parent_class;

  /*< protected > */
  gboolean (*get_writer_id) (GstFluTSIndex * index, gint * id,
      gchar * writer);

  /* abstract methods */
  void (*add_entry) (GstFluTSIndex * index, GstFluTSIndexEntry * entry);

  GstFluTSIndexEntry *(*get_assoc_entry) (GstFluTSIndex * index, gint id,
      GstFluTSIndexLookupMethod method, GstFluTSIndexAssociationFlags flags,
      GstFormat format, gint64 value,
      GCompareDataFunc func, gpointer user_data);
};

GType gst_flutsindex_get_type (void);

GstFluTSIndex *gst_flutsmemindex_new (void);

gboolean gst_flutsindex_get_writer_id (GstFluTSIndex * index,
    GstObject * writer, gint * id);

GstFluTSIndexEntry *gst_flutsindex_add_associationv (GstFluTSIndex * index,
    gint id, GstFluTSIndexAssociationFlags flags, gint n,
    const GstFluTSIndexAssociation * list);

GstFluTSIndexEntry *gst_flutsindex_add_id (GstFluTSIndex * index, gint id,
    gchar * description);

GstFluTSIndexEntry *gst_flutsindex_get_assoc_entry (GstFluTSIndex * index,
    gint id, GstFluTSIndexLookupMethod method,
    GstFluTSIndexAssociationFlags flags, GstFormat format, gint64 value);

GstFluTSIndexEntry *gst_flutsindex_get_assoc_entry_full (GstFluTSIndex * index,
    gint id, GstFluTSIndexLookupMethod method,
    GstFluTSIndexAssociationFlags flags, GstFormat format, gint64 value,
    GCompareDataFunc func, gpointer user_data);

/* working with index entries */
GType gst_flutsindex_entry_get_type (void);

GstFluTSIndexEntry *gst_flutsindex_entry_copy (GstFluTSIndexEntry * entry);

void gst_flutsindex_entry_free (GstFluTSIndexEntry * entry);

gboolean gst_flutsindex_entry_assoc_map (GstFluTSIndexEntry * entry,
    GstFormat format, gint64 * value);

#define GstIndex GstFluTSIndex
#define GstIndexEntry GstFluTSIndexEntry
#define GstIndexAssociation GstFluTSIndexAssociation

#define gst_index_factory_make(name) gst_flutsmemindex_new()
#define gst_index_get_writer_id gst_flutsindex_get_writer_id
#define gst_index_add_associationv gst_flutsindex_add_associationv
#define gst_index_get_assoc_entry gst_flutsindex_get_assoc_entry
#define gst_index_entry_assoc_map gst_flutsindex_entry_assoc_map

#define GST_ASSOCIATION_FLAG_NONE GST_FLUTSINDEX_ASSOCIATION_FLAG_NONE
#define GST_INDEX_LOOKUP_BEFORE GST_FLUTSINDEX_LOOKUP_BEFORE

G_END_DECLS
#endif /* _FLUTSINDEX_H__ */
