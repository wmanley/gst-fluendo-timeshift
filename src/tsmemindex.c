/* GStreamer MPEG TS Time Shifting
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "tsindex.h"

#define GST_TYPE_TS_MEM_INDEX              \
  (gst_ts_memindex_get_type ())
#define GST_TS_MEM_INDEX(obj)              \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TS_MEM_INDEX, GstTSMemIndex))
#define GST_TS_MEM_INDEX_CLASS(klass)      \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TS_MEM_INDEX, GstTSMemIndexClass))
#define GST_IS_TS_MEM_INDEX(obj)           \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TS_MEM_INDEX))
#define GST_IS_TS_MEM_INDEX_CLASS(klass)   \
  (GST_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TS_MEM_INDEX))

/*
 * Object model:
 *
 * All entries are simply added to a GList first. Then we build
 * an index to each entry for each id/format
 *
 *
 *  memindex
 *    -----------------------------...
 *    !                  !
 *   id1                 id2
 *    ------------
 *    !          !
 *   format1  format2
 *    !          !
 *   GTree      GTree
 *
 *
 * The memindex creates a MemIndexId object for each writer id, a
 * Hashtable is kept to map the id to the MemIndexId
 *
 * The MemIndexId keeps a MemIndexFormatIndex for each format the
 * specific writer wants indexed.
 *
 * The MemIndexFormatIndex keeps all the values of the particular
 * format in a GTree, The values of the GTree point back to the entry.
 *
 * Finding a value for an id/format requires locating the correct GTree,
 * then do a lookup in the Tree to get the required value.
 */

typedef struct
{
  GstFormat format;
  gint offset;
  GTree *tree;
}
GstTSMemIndexFormatIndex;

typedef struct
{
  gint id;
  GHashTable *format_index;
}
GstTSMemIndexId;

typedef struct _GstTSMemIndex GstTSMemIndex;
typedef struct _GstTSMemIndexClass GstTSMemIndexClass;

struct _GstTSMemIndex
{
  GstTSIndex parent;

  GList *associations;

  GHashTable *id_index;
};

struct _GstTSMemIndexClass
{
  GstTSIndexClass parent_class;
};

static void gst_ts_memindex_finalize (GObject * object);

static void gst_ts_memindex_add_entry (GstTSIndex * index,
    GstTSIndexEntry * entry);
static GstTSIndexEntry *gst_ts_memindex_get_assoc_entry (GstTSIndex *
    index, gint id, GstTSIndexLookupMethod method,
    GstTSIndexAssociationFlags flags, GstFormat format, gint64 value,
    GCompareDataFunc func, gpointer user_data);

#define CLASS(mem_index) GST_TS_MEM_INDEX_CLASS (G_OBJECT_GET_CLASS (mem_index))

static GType gst_ts_memindex_get_type (void);

G_DEFINE_TYPE (GstTSMemIndex, gst_ts_memindex, GST_TYPE_TS_INDEX);

GstTSIndex *
gst_ts_memindex_new (void)
{
  return GST_TS_INDEX (g_object_new (gst_ts_memindex_get_type (), NULL));
}

static void
gst_ts_memindex_class_init (GstTSMemIndexClass * klass)
{
  GObjectClass *gobject_class;
  GstTSIndexClass *gstindex_class;

  gobject_class = (GObjectClass *) klass;
  gstindex_class = (GstTSIndexClass *) klass;

  gobject_class->finalize = gst_ts_memindex_finalize;

  gstindex_class->add_entry = GST_DEBUG_FUNCPTR (gst_ts_memindex_add_entry);
  gstindex_class->get_assoc_entry =
      GST_DEBUG_FUNCPTR (gst_ts_memindex_get_assoc_entry);
}

static void
gst_ts_memindex_init (GstTSMemIndex * index)
{
  index->associations = NULL;
  index->id_index = g_hash_table_new (g_int_hash, g_int_equal);
}

static void
gst_ts_memindex_free_format (gpointer key, gpointer value, gpointer user_data)
{
  GstTSMemIndexFormatIndex *index = (GstTSMemIndexFormatIndex *) value;

  if (index->tree) {
    g_tree_destroy (index->tree);
  }

  g_slice_free (GstTSMemIndexFormatIndex, index);
}

static void
gst_ts_memindex_free_id (gpointer key, gpointer value, gpointer user_data)
{
  GstTSMemIndexId *id_index = (GstTSMemIndexId *) value;

  if (id_index->format_index) {
    g_hash_table_foreach (id_index->format_index, gst_ts_memindex_free_format,
        NULL);
    g_hash_table_destroy (id_index->format_index);
    id_index->format_index = NULL;
  }

  g_slice_free (GstTSMemIndexId, id_index);
}

static void
gst_ts_memindex_finalize (GObject * object)
{
  GstTSMemIndex *memindex = GST_TS_MEM_INDEX (object);

  /* Delete the trees referencing the associations first */
  if (memindex->id_index) {
    g_hash_table_foreach (memindex->id_index, gst_ts_memindex_free_id, NULL);
    g_hash_table_destroy (memindex->id_index);
    memindex->id_index = NULL;
  }

  /* Then delete the associations themselves */
  if (memindex->associations) {
    g_list_foreach (memindex->associations, (GFunc) gst_ts_index_entry_free,
        NULL);
    g_list_free (memindex->associations);
    memindex->associations = NULL;
  }

  G_OBJECT_CLASS (gst_ts_memindex_parent_class)->finalize (object);
}

static void
gst_ts_memindex_add_id (GstTSIndex * index, GstTSIndexEntry * entry)
{
  GstTSMemIndex *memindex = GST_TS_MEM_INDEX (index);
  GstTSMemIndexId *id_index;

  id_index = g_hash_table_lookup (memindex->id_index, &entry->id);

  if (!id_index) {
    id_index = g_slice_new0 (GstTSMemIndexId);

    id_index->id = entry->id;
    id_index->format_index = g_hash_table_new (g_int_hash, g_int_equal);
    g_hash_table_insert (memindex->id_index, &id_index->id, id_index);
  }
}

static gint
mem_index_compare (gconstpointer a, gconstpointer b, gpointer user_data)
{
  GstTSMemIndexFormatIndex *index = user_data;
  gint64 val1, val2;
  gint64 diff;

  val1 = GST_TS_INDEX_ASSOC_VALUE (((GstTSIndexEntry *) a), index->offset);
  val2 = GST_TS_INDEX_ASSOC_VALUE (((GstTSIndexEntry *) b), index->offset);

  diff = (val2 - val1);

  return (diff == 0 ? 0 : (diff > 0 ? 1 : -1));
}

static void
gst_ts_memindex_index_format (GstTSMemIndexId * id_index,
    GstTSIndexEntry * entry, gint assoc)
{
  GstTSMemIndexFormatIndex *index;
  GstFormat *format;

  format = &GST_TS_INDEX_ASSOC_FORMAT (entry, assoc);

  index = g_hash_table_lookup (id_index->format_index, format);

  if (!index) {
    index = g_slice_new0 (GstTSMemIndexFormatIndex);

    index->format = *format;
    index->offset = assoc;
    index->tree = g_tree_new_with_data (mem_index_compare, index);

    g_hash_table_insert (id_index->format_index, &index->format, index);
  }

  g_tree_insert (index->tree, entry, entry);
}

static void
gst_ts_memindex_add_association (GstTSIndex * index, GstTSIndexEntry * entry)
{
  GstTSMemIndex *memindex = GST_TS_MEM_INDEX (index);
  GstTSMemIndexId *id_index;

  memindex->associations = g_list_prepend (memindex->associations, entry);

  id_index = g_hash_table_lookup (memindex->id_index, &entry->id);
  if (id_index) {
    gint i;

    for (i = 0; i < GST_TS_INDEX_NASSOCS (entry); i++) {
      gst_ts_memindex_index_format (id_index, entry, i);
    }
  }
}

static void
gst_ts_memindex_add_entry (GstTSIndex * index, GstTSIndexEntry * entry)
{
  switch (entry->type) {
    case GST_TS_INDEX_ENTRY_ID:
      gst_ts_memindex_add_id (index, entry);
      break;
    case GST_TS_INDEX_ENTRY_ASSOCIATION:
      gst_ts_memindex_add_association (index, entry);
      break;
    default:
      break;
  }
}

typedef struct
{
  gint64 value;
  GstTSMemIndexFormatIndex *index;
  gboolean exact;
  GstTSIndexEntry *lower;
  gint64 low_diff;
  GstTSIndexEntry *higher;
  gint64 high_diff;
}
GstTSMemIndexSearchData;

static gint
mem_index_search (gconstpointer a, gconstpointer b)
{
  GstTSMemIndexSearchData *data = (GstTSMemIndexSearchData *) b;
  GstTSMemIndexFormatIndex *index = data->index;
  gint64 val1, val2;
  gint64 diff;

  val1 = GST_TS_INDEX_ASSOC_VALUE (((GstTSIndexEntry *) a), index->offset);
  val2 = data->value;

  diff = (val1 - val2);
  if (diff == 0)
    return 0;

  /* exact matching, don't update low/high */
  if (data->exact)
    return (diff > 0 ? 1 : -1);

  if (diff < 0) {
    if (diff > data->low_diff) {
      data->low_diff = diff;
      data->lower = (GstTSIndexEntry *) a;
    }
    diff = -1;
  } else {
    if (diff < data->high_diff) {
      data->high_diff = diff;
      data->higher = (GstTSIndexEntry *) a;
    }
    diff = 1;
  }

  return diff;
}

static GstTSIndexEntry *
gst_ts_memindex_get_assoc_entry (GstTSIndex * index, gint id,
    GstTSIndexLookupMethod method,
    GstTSIndexAssociationFlags flags,
    GstFormat format, gint64 value, GCompareDataFunc func, gpointer user_data)
{
  GstTSMemIndex *memindex = GST_TS_MEM_INDEX (index);
  GstTSMemIndexId *id_index;
  GstTSMemIndexFormatIndex *format_index;
  GstTSIndexEntry *entry;
  GstTSMemIndexSearchData data;

  id_index = g_hash_table_lookup (memindex->id_index, &id);
  if (!id_index)
    return NULL;

  format_index = g_hash_table_lookup (id_index->format_index, &format);
  if (!format_index)
    return NULL;

  data.value = value;
  data.index = format_index;
  data.exact = (method == GST_TS_INDEX_LOOKUP_EXACT);

  /* setup data for low/high checks if we are not looking
   * for an exact match */
  if (!data.exact) {
    data.low_diff = G_MININT64;
    data.lower = NULL;
    data.high_diff = G_MAXINT64;
    data.higher = NULL;
  }

  entry = g_tree_search (format_index->tree, mem_index_search, &data);

  /* get the low/high values if we're not exact */
  if (entry == NULL && !data.exact) {
    if (method == GST_TS_INDEX_LOOKUP_BEFORE)
      entry = data.lower;
    else if (method == GST_TS_INDEX_LOOKUP_AFTER) {
      entry = data.higher;
    }
  }

  if (entry && ((GST_TS_INDEX_ASSOC_FLAGS (entry) & flags) != flags)) {
    if (method != GST_TS_INDEX_LOOKUP_EXACT) {
      GList *l_entry = g_list_find (memindex->associations, entry);

      entry = NULL;

      while (l_entry) {
        entry = (GstTSIndexEntry *) l_entry->data;

        if (entry->id == id
            && (GST_TS_INDEX_ASSOC_FLAGS (entry) & flags) == flags)
          break;

        if (method == GST_TS_INDEX_LOOKUP_BEFORE)
          l_entry = g_list_next (l_entry);
        else if (method == GST_TS_INDEX_LOOKUP_AFTER) {
          l_entry = g_list_previous (l_entry);
        }
      }
    } else {
      entry = NULL;
    }
  }

  return entry;
}
