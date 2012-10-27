/* GStreamer
 * Copyright (C) 2001 RidgeRun (http://www.ridgerun.com/)
 * Written by Erik Walthinsen <omega@ridgerun.com>
 *
 * flutsindex.c: Index for mappings and other data
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

#include "gst-compat.h"
#include "flutsindex.h"

static void gst_flutsindex_finalize (GObject * object);

GType
gst_flutsindex_entry_get_type (void)
{
  static GType index_entry_type = 0;

  if (!index_entry_type) {
    index_entry_type = g_boxed_type_register_static ("GstFluTSIndexEntry",
        (GBoxedCopyFunc) gst_flutsindex_entry_copy,
        (GBoxedFreeFunc) gst_flutsindex_entry_free);
  }
  return index_entry_type;
}

G_DEFINE_TYPE (GstFluTSIndex, gst_flutsindex, GST_TYPE_OBJECT);

static void
gst_flutsindex_class_init (GstFluTSIndexClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_flutsindex_finalize;
}

static void
gst_flutsindex_init (GstFluTSIndex * index)
{
  index->writers = g_hash_table_new (NULL, NULL);
  index->last_id = 0;

  GST_OBJECT_FLAG_SET (index, GST_FLUTSINDEX_WRITABLE);
  GST_OBJECT_FLAG_SET (index, GST_FLUTSINDEX_READABLE);
}

static void
gst_flutsindex_free_writer (gpointer key, gpointer value, gpointer user_data)
{
  GstFluTSIndexEntry *entry = (GstFluTSIndexEntry *) value;

  if (entry) {
    gst_flutsindex_entry_free (entry);
  }
}

static void
gst_flutsindex_finalize (GObject * object)
{
  GstFluTSIndex *index = GST_FLUTSINDEX (object);

  if (index->writers) {
    g_hash_table_foreach (index->writers, gst_flutsindex_free_writer, NULL);
    g_hash_table_destroy (index->writers);
    index->writers = NULL;
  }

  G_OBJECT_CLASS (gst_flutsindex_parent_class)->finalize (object);
}

static inline void
gst_flutsindex_add_entry (GstFluTSIndex * index, GstFluTSIndexEntry * entry)
{
  GstFluTSIndexClass *iclass;

  iclass = GST_FLUTSINDEX_GET_CLASS (index);

  if (iclass->add_entry) {
    iclass->add_entry (index, entry);
  }
}

/**
 * gst_flutsindex_entry_copy:
 * @entry: the entry to copy
 *
 * Copies an entry and returns the result.
 *
 * Free-function: gst_flutsindex_entry_free
 *
 * Returns: (transfer full): a newly allocated #GstFluTSIndexEntry.
 */
GstFluTSIndexEntry *
gst_flutsindex_entry_copy (GstFluTSIndexEntry * entry)
{
  GstFluTSIndexEntry *new_entry = g_slice_new (GstFluTSIndexEntry);

  memcpy (new_entry, entry, sizeof (GstFluTSIndexEntry));
  return new_entry;
}

/**
 * gst_flutsindex_entry_free:
 * @entry: (transfer full): the entry to free
 *
 * Free the memory used by the given entry.
 */
void
gst_flutsindex_entry_free (GstFluTSIndexEntry * entry)
{
  switch (entry->type) {
    case GST_FLUTSINDEX_ENTRY_ID:
      if (entry->data.id.description) {
        g_free (entry->data.id.description);
        entry->data.id.description = NULL;
      }
      break;
    case GST_FLUTSINDEX_ENTRY_ASSOCIATION:
      if (entry->data.assoc.assocs) {
        g_free (entry->data.assoc.assocs);
        entry->data.assoc.assocs = NULL;
      }
      break;
  }

  g_slice_free (GstFluTSIndexEntry, entry);
}

/**
 * gst_flutsindex_add_id:
 * @index: the index to add the entry to
 * @id: the id of the index writer
 * @description: the description of the index writer
 *
 * Add an id entry into the index.
 *
 * Returns: a pointer to the newly added entry in the index.
 */
GstFluTSIndexEntry *
gst_flutsindex_add_id (GstFluTSIndex * index, gint id, gchar * description)
{
  GstFluTSIndexEntry *entry;

  g_return_val_if_fail (GST_IS_INDEX (index), NULL);
  g_return_val_if_fail (description != NULL, NULL);

  if (!GST_FLUTSINDEX_IS_WRITABLE (index) || id == -1)
    return NULL;

  entry = g_slice_new (GstFluTSIndexEntry);
  entry->type = GST_FLUTSINDEX_ENTRY_ID;
  entry->id = id;
  entry->data.id.description = description;

  gst_flutsindex_add_entry (index, entry);

  return entry;
}

/**
 * gst_flutsindex_get_writer_id:
 * @index: the index to get a unique write id for
 * @writer: the GstObject to allocate an id for
 * @id: a pointer to a gint to hold the id
 *
 * Before entries can be added to the index, a writer
 * should obtain a unique id. The methods to add new entries
 * to the index require this id as an argument.
 *
 * The application can implement a custom function to map the writer object
 * to a string. That string will be used to register or look up an id
 * in the index.
 *
 * <note>
 * The caller must not hold @writer's #GST_OBJECT_LOCK, as the default
 * resolver may call functions that take the object lock as well, and
 * the lock is not recursive.
 * </note>
 *
 * Returns: TRUE if the writer would be mapped to an id.
 */
gboolean
gst_flutsindex_get_writer_id (GstFluTSIndex * index, GstObject * writer,
    gint * id)
{
  gchar *writer_string = NULL;
  GstFluTSIndexEntry *entry;
  GstFluTSIndexClass *iclass;
  gboolean success = FALSE;

  g_return_val_if_fail (GST_IS_INDEX (index), FALSE);
  g_return_val_if_fail (GST_IS_OBJECT (writer), FALSE);
  g_return_val_if_fail (id, FALSE);

  *id = -1;

  /* first try to get a previously cached id */
  entry = g_hash_table_lookup (index->writers, writer);
  if (entry == NULL) {

    iclass = GST_FLUTSINDEX_GET_CLASS (index);

    writer_string = gst_object_get_path_string (writer);

    /* if the index has a resolver, make it map this string to an id */
    if (iclass->get_writer_id) {
      success = iclass->get_writer_id (index, id, writer_string);
    }
    /* if the index could not resolve, we allocate one ourselves */
    if (!success) {
      *id = ++index->last_id;
    }

    entry = gst_flutsindex_add_id (index, *id, writer_string);
    if (!entry) {
      /* index is probably not writable, make an entry anyway
       * to keep it in our cache */
      entry = g_slice_new (GstFluTSIndexEntry);
      entry->type = GST_FLUTSINDEX_ENTRY_ID;
      entry->id = *id;
      entry->data.id.description = writer_string;
    }
    g_hash_table_insert (index->writers, writer, entry);
  } else {
    *id = entry->id;
  }

  return TRUE;
}

/**
 * gst_flutsindex_add_associationv:
 * @index: the index to add the entry to
 * @id: the id of the index writer
 * @flags: optinal flags for this entry
 * @n: number of associations
 * @list: list of associations
 *
 * Associate given format/value pairs with each other.
 *
 * Returns: a pointer to the newly added entry in the index.
 */
GstFluTSIndexEntry *
gst_flutsindex_add_associationv (GstFluTSIndex * index, gint id,
    GstFluTSIndexAssociationFlags flags, gint n,
    const GstFluTSIndexAssociation * list)
{
  GstFluTSIndexEntry *entry;

  g_return_val_if_fail (n > 0, NULL);
  g_return_val_if_fail (list != NULL, NULL);
  g_return_val_if_fail (GST_IS_INDEX (index), NULL);

  if (!GST_FLUTSINDEX_IS_WRITABLE (index) || id == -1)
    return NULL;

  entry = g_slice_new (GstFluTSIndexEntry);

  entry->type = GST_FLUTSINDEX_ENTRY_ASSOCIATION;
  entry->id = id;
  entry->data.assoc.flags = flags;
  entry->data.assoc.assocs =
      g_memdup (list, sizeof (GstFluTSIndexAssociation) * n);
  entry->data.assoc.nassocs = n;

  gst_flutsindex_add_entry (index, entry);

  return entry;
}

static gint
gst_flutsindex_compare_func (gconstpointer a, gconstpointer b,
    gpointer user_data)
{
  if (a < b)
    return -1;
  if (a > b)
    return 1;
  return 0;
}

/**
 * gst_flutsindex_get_assoc_entry:
 * @index: the index to search
 * @id: the id of the index writer
 * @method: The lookup method to use
 * @flags: Flags for the entry
 * @format: the format of the value
 * @value: the value to find
 *
 * Finds the given format/value in the index
 *
 * Returns: the entry associated with the value or NULL if the
 *   value was not found.
 */
GstFluTSIndexEntry *
gst_flutsindex_get_assoc_entry (GstFluTSIndex * index, gint id,
    GstFluTSIndexLookupMethod method, GstFluTSIndexAssociationFlags flags,
    GstFormat format, gint64 value)
{
  g_return_val_if_fail (GST_IS_FLUTSINDEX (index), NULL);

  if (id == -1)
    return NULL;

  return gst_flutsindex_get_assoc_entry_full (index, id, method, flags, format,
      value, gst_flutsindex_compare_func, NULL);
}

/**
 * gst_flutsindex_get_assoc_entry:
 * @index: the index to search
 * @id: the id of the index writer
 * @method: The lookup method to use
 * @flags: Flags for the entry
 * @format: the format of the value
 * @value: the value to find
 * @func: the function used to compare entries
 * @user_data: user data passed to the compare function
 *
 * Finds the given format/value in the index with the given
 * compare function and user_data.
 *
 * Returns: the entry associated with the value or NULL if the
 *   value was not found.
 */
GstFluTSIndexEntry *
gst_flutsindex_get_assoc_entry_full (GstFluTSIndex * index, gint id,
    GstFluTSIndexLookupMethod method, GstFluTSIndexAssociationFlags flags,
    GstFormat format, gint64 value, GCompareDataFunc func, gpointer user_data)
{
  GstFluTSIndexClass *iclass;

  g_return_val_if_fail (GST_IS_INDEX (index), NULL);

  if (id == -1)
    return NULL;

  iclass = GST_FLUTSINDEX_GET_CLASS (index);

  if (iclass->get_assoc_entry)
    return iclass->get_assoc_entry (index, id, method, flags, format, value,
        func, user_data);

  return NULL;
}

/**
 * gst_flutsindex_entry_assoc_map:
 * @entry: the index to search
 * @format: the format of the value the find
 * @value: a pointer to store the value
 *
 * Gets alternative formats associated with the indexentry.
 *
 * Returns: TRUE if there was a value associated with the given
 * format.
 */
gboolean
gst_flutsindex_entry_assoc_map (GstFluTSIndexEntry * entry,
    GstFormat format, gint64 * value)
{
  gint i;

  g_return_val_if_fail (entry != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  for (i = 0; i < GST_FLUTSINDEX_NASSOCS (entry); i++) {
    if (GST_FLUTSINDEX_ASSOC_FORMAT (entry, i) == format) {
      *value = GST_FLUTSINDEX_ASSOC_VALUE (entry, i);
      return TRUE;
    }
  }
  return FALSE;
}
