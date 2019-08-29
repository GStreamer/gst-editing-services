/* GStreamer Editing Services
 * Copyright (C) 2012 Paul Lange <palango@gmx.de>
 * Copyright (C) <2014> Thibault Saunier <thibault.saunier@collabora.com>
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

#include <glib-object.h>
#include <gst/gst.h>

#include "ges-meta-container.h"
#include "ges-marker-list.h"

/**
* SECTION: gesmetacontainer
* @short_description: An interface for storing meta
*
* Interface that allows reading and writing meta
*/

static GQuark ges_meta_key;

G_DEFINE_INTERFACE_WITH_CODE (GESMetaContainer, ges_meta_container,
    G_TYPE_OBJECT, ges_meta_key =
    g_quark_from_static_string ("ges-meta-container-data"););

enum
{
  NOTIFY_SIGNAL,
  LAST_SIGNAL
};

static guint _signals[LAST_SIGNAL] = { 0 };

typedef struct RegisteredMeta
{
  GType item_type;
  GESMetaFlag flags;
} RegisteredMeta;

typedef struct ContainerData
{
  GstStructure *structure;
  GHashTable *static_items;
} ContainerData;

static void
ges_meta_container_default_init (GESMetaContainerInterface * iface)
{

  /**
   * GESMetaContainer::notify:
   * @container: a #GESMetaContainer
   * @prop: the key of the value that changed
   * @value: the #GValue containing the new value
   *
   * The notify signal is used to be notify of changes of values
   * of some metadatas
   */
  _signals[NOTIFY_SIGNAL] =
      g_signal_new ("notify-meta", G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE | G_SIGNAL_DETAILED |
      G_SIGNAL_NO_HOOKS, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_VALUE);
}

static void
_free_meta_container_data (ContainerData * data)
{
  gst_structure_free (data->structure);
  g_hash_table_unref (data->static_items);

  g_slice_free (ContainerData, data);
}

static void
_free_static_item (RegisteredMeta * item)
{
  g_slice_free (RegisteredMeta, item);
}

static ContainerData *
_create_container_data (GESMetaContainer * container)
{
  ContainerData *data = g_slice_new (ContainerData);
  data->structure = gst_structure_new_empty ("metadatas");
  data->static_items = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) (GDestroyNotify) _free_static_item);
  g_object_set_qdata_full (G_OBJECT (container), ges_meta_key, data,
      (GDestroyNotify) _free_meta_container_data);

  return data;
}

static GstStructure *
_meta_container_get_structure (GESMetaContainer * container)
{
  ContainerData *data;

  data = g_object_get_qdata (G_OBJECT (container), ges_meta_key);
  if (!data)
    data = _create_container_data (container);

  return data->structure;
}

typedef struct
{
  GESMetaForeachFunc func;
  const GESMetaContainer *container;
  gpointer data;
} MetadataForeachData;

static gboolean
structure_foreach_wrapper (GQuark field_id, const GValue * value,
    gpointer user_data)
{
  MetadataForeachData *data = (MetadataForeachData *) user_data;

  data->func (data->container, g_quark_to_string (field_id), value, data->data);
  return TRUE;
}

static gboolean
_append_foreach (GQuark field_id, const GValue * value, GESMetaContainer * self)
{
  ges_meta_container_set_meta (self, g_quark_to_string (field_id), value);

  return TRUE;
}

/**
 * ges_meta_container_foreach:
 * @container: container to iterate over
 * @func: (scope call): function to be called for each metadata
 * @user_data: (closure): user specified data
 *
 * Calls the given function for each metadata inside the meta container. Note
 * that if there is no metadata, the function won't be called at all.
 */
void
ges_meta_container_foreach (GESMetaContainer * container,
    GESMetaForeachFunc func, gpointer user_data)
{
  GstStructure *structure;
  MetadataForeachData foreach_data;

  g_return_if_fail (GES_IS_META_CONTAINER (container));
  g_return_if_fail (func != NULL);

  structure = _meta_container_get_structure (container);

  foreach_data.func = func;
  foreach_data.container = container;
  foreach_data.data = user_data;

  gst_structure_foreach (structure,
      (GstStructureForeachFunc) structure_foreach_wrapper, &foreach_data);
}

static gboolean
_register_meta (GESMetaContainer * container, GESMetaFlag flags,
    const gchar * meta_item, GType type)
{
  ContainerData *data;
  RegisteredMeta *static_item;

  data = g_object_get_qdata (G_OBJECT (container), ges_meta_key);
  if (!data)
    data = _create_container_data (container);
  else if (g_hash_table_lookup (data->static_items, meta_item)) {
    GST_WARNING_OBJECT (container, "Static meta %s already registered",
        meta_item);

    return FALSE;
  }

  static_item = g_slice_new0 (RegisteredMeta);
  static_item->item_type = type;
  static_item->flags = flags;
  g_hash_table_insert (data->static_items, g_strdup (meta_item), static_item);

  return TRUE;
}

/* _can_write_value should have been checked before calling */
static gboolean
_set_value (GESMetaContainer * container, const gchar * meta_item,
    const GValue * value)
{
  GstStructure *structure;
  gchar *val = gst_value_serialize (value);

  if (val == NULL) {
    GST_WARNING_OBJECT (container, "Could not set value on item: %s",
        meta_item);

    g_free (val);
    return FALSE;
  }

  structure = _meta_container_get_structure (container);

  GST_DEBUG_OBJECT (container, "Setting meta_item %s value: %s::%s",
      meta_item, G_VALUE_TYPE_NAME (value), val);

  gst_structure_set_value (structure, meta_item, value);
  g_signal_emit (container, _signals[NOTIFY_SIGNAL], 0, meta_item, value);

  g_free (val);
  return TRUE;
}

static gboolean
_can_write_value (GESMetaContainer * container, const gchar * item_name,
    GType type)
{
  ContainerData *data;
  RegisteredMeta *static_item = NULL;

  data = g_object_get_qdata (G_OBJECT (container), ges_meta_key);
  if (!data) {
    _create_container_data (container);
    return TRUE;
  }

  static_item = g_hash_table_lookup (data->static_items, item_name);

  if (static_item == NULL)
    return TRUE;

  if ((static_item->flags & GES_META_WRITABLE) == FALSE) {
    GST_WARNING_OBJECT (container, "Can not write %s", item_name);
    return FALSE;
  }

  if (static_item->item_type != type) {
    GST_WARNING_OBJECT (container, "Can not set value of type %s on %s "
        "its type is: %s", g_type_name (static_item->item_type), item_name,
        g_type_name (type));
    return FALSE;
  }

  return TRUE;
}

#define CREATE_SETTER(name, value_ctype, value_gtype,  setter_name)     \
gboolean                                                                \
ges_meta_container_set_ ## name (GESMetaContainer *container,      \
                           const gchar *meta_item, value_ctype value)   \
{                                                                       \
  GValue gval = { 0 };                                                  \
  gboolean ret;                                                         \
                                                                        \
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);      \
  g_return_val_if_fail (meta_item != NULL, FALSE);                      \
                                                                        \
  if (_can_write_value (container, meta_item, value_gtype) == FALSE)    \
    return FALSE;                                                       \
                                                                        \
  g_value_init (&gval, value_gtype);                                    \
  g_value_set_ ##setter_name (&gval, value);                            \
                                                                        \
  ret = _set_value (container, meta_item, &gval);                       \
  g_value_unset (&gval);                                                \
  return ret;                                                           \
}

/**
 * ges_meta_container_set_boolean:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
CREATE_SETTER (boolean, gboolean, G_TYPE_BOOLEAN, boolean);

/**
 * ges_meta_container_set_int:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
CREATE_SETTER (int, gint, G_TYPE_INT, int);

/**
 * ges_meta_container_set_uint:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
CREATE_SETTER (uint, guint, G_TYPE_UINT, uint);

/**
 * ges_meta_container_set_int64:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
CREATE_SETTER (int64, gint64, G_TYPE_INT64, int64);

/**
 * ges_meta_container_set_uint64:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
CREATE_SETTER (uint64, guint64, G_TYPE_UINT64, uint64);

/**
 * ges_meta_container_set_float:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
CREATE_SETTER (float, float, G_TYPE_FLOAT, float);

/**
 * ges_meta_container_set_double:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
CREATE_SETTER (double, double, G_TYPE_DOUBLE, double);

/**
 * ges_meta_container_set_date:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
CREATE_SETTER (date, const GDate *, G_TYPE_DATE, boxed);

/**
 * ges_meta_container_set_date_time:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
CREATE_SETTER (date_time, const GstDateTime *, GST_TYPE_DATE_TIME, boxed);

/**
* ges_meta_container_set_string:
* @container: Target container
* @meta_item: Name of the meta item to set
* @value: Value to set
*
* Sets the value of a given meta item
*
* Return: %TRUE if the meta could be added, %FALSE otherwise
*/
CREATE_SETTER (string, const gchar *, G_TYPE_STRING, string);

/**
 * ges_meta_container_set_meta:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @value: (allow-none): Value to set
 *
 * Sets the value of a given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 */
gboolean
ges_meta_container_set_meta (GESMetaContainer * container,
    const gchar * meta_item, const GValue * value)
{
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  if (value == NULL) {
    GstStructure *structure = _meta_container_get_structure (container);
    gst_structure_remove_field (structure, meta_item);

    g_signal_emit (container, _signals[NOTIFY_SIGNAL], 0, meta_item, value);

    return TRUE;
  }

  if (_can_write_value (container, meta_item, G_VALUE_TYPE (value)) == FALSE)
    return FALSE;

  return _set_value (container, meta_item, value);
}

/**
 * ges_meta_container_set_marker_list:
 * @container: Target container
 * @meta_item: Name of the meta item to set
 * @list: (allow-none) (transfer none): List to set
 *
 * Associates a marker list with the given meta item
 *
 * Return: %TRUE if the meta could be added, %FALSE otherwise
 * Since: 1.18
 */
gboolean
ges_meta_container_set_marker_list (GESMetaContainer * container,
    const gchar * meta_item, const GESMarkerList * list)
{
  gboolean ret;
  GValue v = G_VALUE_INIT;
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  if (list == NULL) {
    GstStructure *structure = _meta_container_get_structure (container);
    gst_structure_remove_field (structure, meta_item);

    g_signal_emit (container, _signals[NOTIFY_SIGNAL], 0, meta_item, list);

    return TRUE;
  }

  g_return_val_if_fail (GES_IS_MARKER_LIST ((gpointer) list), FALSE);

  if (_can_write_value (container, meta_item, GES_TYPE_MARKER_LIST) == FALSE)
    return FALSE;

  g_value_init_from_instance (&v, (gpointer) list);

  ret = _set_value (container, meta_item, &v);

  g_value_unset (&v);

  return ret;
}

/**
 * ges_meta_container_metas_to_string:
 * @container: a #GESMetaContainer
 *
 * Serializes a meta container to a string.
 *
 * Returns: (nullable): a newly-allocated string, or NULL in case of an error.
 * The string must be freed with g_free() when no longer needed.
 */
gchar *
ges_meta_container_metas_to_string (GESMetaContainer * container)
{
  GstStructure *structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), NULL);

  structure = _meta_container_get_structure (container);

  return gst_structure_to_string (structure);
}

/**
 * ges_meta_container_add_metas_from_string:
 * @container: Target container
 * @str: a string created with ges_meta_container_metas_to_string()
 *
 * Deserializes a meta container.
 *
 * Returns: TRUE on success, FALSE if there was an error.
 */
gboolean
ges_meta_container_add_metas_from_string (GESMetaContainer * container,
    const gchar * str)
{
  GstStructure *n_structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);

  n_structure = gst_structure_from_string (str, NULL);
  if (n_structure == NULL) {
    GST_WARNING_OBJECT (container, "Could not add metas: %s", str);
    return FALSE;
  }

  gst_structure_foreach (n_structure, (GstStructureForeachFunc) _append_foreach,
      container);

  gst_structure_free (n_structure);
  return TRUE;
}

/**
 * ges_meta_container_register_static_meta:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to register
 * @type: The required data type for this meta
 *
 * Registers a static meta on @container. This method lets you define a
 * static metadata, which means that @type will be the only type accepted
 * for this meta on this particular @container. Unlike
 * #ges_meta_container_register_meta, no initial value is set for this
 * meta, which means you can use this method to reserve the meta under
 * @meta_item to be optionally set later. Note, if a meta has already been
 * set, but not registered, under @meta_item before calling this method,
 * then its type must match @type, and its value will be left in place.
 * Otherwise, you'll likely want to include #GES_META_WRITABLE in @flags
 * to allow the value to be later set for this meta.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
gboolean
ges_meta_container_register_static_meta (GESMetaContainer * container,
    GESMetaFlag flags, const gchar * meta_item, GType type)
{
  GstStructure *structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  /* If the meta is already in use, and is of a different type, then we
   * want to fail since, unlike ges_meta_container_register_meta, we will
   * not be overwriting this value! If we didn't fail, the user could have
   * a false sense that this meta will always be of the reserved type.
   */
  structure = _meta_container_get_structure (container);
  if (gst_structure_has_field (structure, meta_item) &&
      gst_structure_get_field_type (structure, meta_item) != type) {
    gchar *value_string =
        g_strdup_value_contents (gst_structure_get_value (structure,
            meta_item));
    GST_WARNING_OBJECT (container,
        "Meta %s already assigned a value of %s, which is a different type",
        meta_item, value_string);
    g_free (value_string);
    return FALSE;
  }
  return _register_meta (container, flags, meta_item, type);
}

#define CREATE_REGISTER_STATIC(name, value_ctype, value_gtype, setter_name) \
gboolean                                                                      \
ges_meta_container_register_meta_ ## name (GESMetaContainer *container,\
    GESMetaFlag flags, const gchar *meta_item, value_ctype value)             \
{                                                                             \
  gboolean ret;                                                               \
  GValue gval = { 0 };                                                        \
                                                                              \
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);            \
  g_return_val_if_fail (meta_item != NULL, FALSE);                            \
                                                                              \
  if (!_register_meta (container, flags, meta_item, value_gtype))             \
    return FALSE;                                                             \
                                                                              \
  g_value_init (&gval, value_gtype);                                          \
  g_value_set_ ##setter_name (&gval, value);                                  \
                                                                              \
  ret = _set_value  (container, meta_item, &gval);                            \
                                                                              \
  g_value_unset (&gval);                                                      \
  return ret;                                                                 \
}

/**
 * ges_meta_container_register_meta_boolean:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
CREATE_REGISTER_STATIC (boolean, gboolean, G_TYPE_BOOLEAN, boolean);

/**
 * ges_meta_container_register_meta_int:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
CREATE_REGISTER_STATIC (int, gint, G_TYPE_INT, int);

/**
 * ges_meta_container_register_meta_uint:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
CREATE_REGISTER_STATIC (uint, guint, G_TYPE_UINT, uint);

/**
 * ges_meta_container_register_meta_int64:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
CREATE_REGISTER_STATIC (int64, gint64, G_TYPE_INT64, int64);

/**
 * ges_meta_container_register_meta_uint64:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
CREATE_REGISTER_STATIC (uint64, guint64, G_TYPE_UINT64, uint64);

/**
 * ges_meta_container_register_meta_float:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
*/
CREATE_REGISTER_STATIC (float, float, G_TYPE_FLOAT, float);

/**
 * ges_meta_container_register_meta_double:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
CREATE_REGISTER_STATIC (double, double, G_TYPE_DOUBLE, double);

/**
 * ges_meta_container_register_meta_date:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: (allow-none): Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
CREATE_REGISTER_STATIC (date, const GDate *, G_TYPE_DATE, boxed);

/**
 * ges_meta_container_register_meta_date_time:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: (allow-none): Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
CREATE_REGISTER_STATIC (date_time, const GstDateTime *, GST_TYPE_DATE_TIME,
    boxed);

/**
 * ges_meta_container_register_meta_string:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: (allow-none): Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the meta could be registered, %FALSE otherwise
 */
CREATE_REGISTER_STATIC (string, const gchar *, G_TYPE_STRING, string);

/**
 * ges_meta_container_register_meta:
 * @container: Target container
 * @flags: The #GESMetaFlag to be used
 * @meta_item: Name of the meta item to set
 * @value: Value to set
 *
 * Sets a static meta on @container. This method lets you define static
 * metadatas, which means that the type of the registered will be the only
 * type accepted for this meta on that particular @container.
 *
 * Return: %TRUE if the static meta could be added, %FALSE otherwise
 */
gboolean
ges_meta_container_register_meta (GESMetaContainer * container,
    GESMetaFlag flags, const gchar * meta_item, const GValue * value)
{
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  if (!_register_meta (container, flags, meta_item, G_VALUE_TYPE (value)))
    return FALSE;

  return _set_value (container, meta_item, value);
}

gboolean
ges_meta_container_check_meta_registered (GESMetaContainer * container,
    const gchar * meta_item, GESMetaFlag * flags, GType * type)
{
  ContainerData *data;
  RegisteredMeta *static_item;

  data = g_object_get_qdata (G_OBJECT (container), ges_meta_key);
  if (!data)
    return FALSE;

  static_item = g_hash_table_lookup (data->static_items, meta_item);
  if (static_item == NULL) {
    GST_WARNING_OBJECT (container, "Static meta %s has not been registered yet",
        meta_item);

    return FALSE;
  }

  if (type)
    *type = static_item->item_type;

  if (flags)
    *flags = static_item->flags;

  return TRUE;
}

/* Copied from gsttaglist.c */
/***** evil macros to get all the *_get_* functions right *****/

#define CREATE_GETTER(name,type)                                         \
gboolean                                                                 \
ges_meta_container_get_ ## name (GESMetaContainer *container,    \
                           const gchar *meta_item, type value)       \
{                                                                        \
  GstStructure *structure;                                                     \
                                                                         \
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);   \
  g_return_val_if_fail (meta_item != NULL, FALSE);                   \
  g_return_val_if_fail (value != NULL, FALSE);                           \
                                                                         \
  structure = _meta_container_get_structure (container);                    \
                                                                         \
  return gst_structure_get_ ## name (structure, meta_item, value);   \
}

/**
 * ges_meta_container_get_boolean:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 * @dest: (out): Destination to which value of meta item will be copied
 *
 * Gets the value of a given meta item, returns NULL if @meta_item
 * can not be found.
 */
CREATE_GETTER (boolean, gboolean *);

/**
 * ges_meta_container_get_int:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 * @dest: (out): Destination to which value of meta item will be copied
 *
 * Gets the value of a given meta item, returns NULL if @meta_item
 * can not be found.
 */
CREATE_GETTER (int, gint *);

/**
 * ges_meta_container_get_uint:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 * @dest: (out): Destination to which value of meta item will be copied
 *
 * Gets the value of a given meta item, returns NULL if @meta_item
 * can not be found.
 */
CREATE_GETTER (uint, guint *);

/**
 * ges_meta_container_get_double:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 * @dest: (out): Destination to which value of meta item will be copied
 *
 * Gets the value of a given meta item, returns NULL if @meta_item
 * can not be found.
 */
CREATE_GETTER (double, gdouble *);

/**
 * ges_meta_container_get_int64:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 * @dest: (out): Destination to which value of meta item will be copied
 *
 * Gets the value of a given meta item, returns %FALSE if @meta_item
 * can not be found.
 */
gboolean
ges_meta_container_get_int64 (GESMetaContainer * container,
    const gchar * meta_item, gint64 * dest)
{
  GstStructure *structure;
  const GValue *value;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  value = gst_structure_get_value (structure, meta_item);
  if (!value || G_VALUE_TYPE (value) != G_TYPE_INT64)
    return FALSE;

  *dest = g_value_get_int64 (value);

  return TRUE;
}

/**
 * ges_meta_container_get_uint64:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 * @dest: (out): Destination to which value of meta item will be copied
 *
 * Gets the value of a given meta item, returns %FALSE if @meta_item
 * can not be found.
 */
gboolean
ges_meta_container_get_uint64 (GESMetaContainer * container,
    const gchar * meta_item, guint64 * dest)
{
  GstStructure *structure;
  const GValue *value;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  value = gst_structure_get_value (structure, meta_item);
  if (!value || G_VALUE_TYPE (value) != G_TYPE_UINT64)
    return FALSE;

  *dest = g_value_get_uint64 (value);

  return TRUE;
}

/**
 * ges_meta_container_get_float:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 * @dest: (out): Destination to which value of meta item will be copied
 *
 * Gets the value of a given meta item, returns %FALSE if @meta_item
 * can not be found.
 */
gboolean
ges_meta_container_get_float (GESMetaContainer * container,
    const gchar * meta_item, gfloat * dest)
{
  GstStructure *structure;
  const GValue *value;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  value = gst_structure_get_value (structure, meta_item);
  if (!value || G_VALUE_TYPE (value) != G_TYPE_FLOAT)
    return FALSE;

  *dest = g_value_get_float (value);

  return TRUE;
}

/**
 * ges_meta_container_get_string:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 *
 * Gets the value of a given meta item, returns NULL if @meta_item
 * can not be found.
 */
const gchar *
ges_meta_container_get_string (GESMetaContainer * container,
    const gchar * meta_item)
{
  GstStructure *structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  return gst_structure_get_string (structure, meta_item);
}

/**
 * ges_meta_container_get_meta:
 * @container: Target container
 * @key: The key name of the meta to retrieve
 *
 * Gets the value of a given meta item, returns NULL if @key
 * can not be found.
 *
 * Returns: the #GValue corresponding to the meta with the given @key.
 */
const GValue *
ges_meta_container_get_meta (GESMetaContainer * container, const gchar * key)
{
  GstStructure *structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  return gst_structure_get_value (structure, key);
}

/**
 * ges_meta_container_get_marker_list:
 * @container: Target container
 * @key: The key name of the list to retrieve
 *
 * Gets the value of a given meta item, returns NULL if @key
 * can not be found.
 *
 * Returns: (transfer full): the #GESMarkerList corresponding to the meta with the given @key.
 * Since: 1.18
 */
GESMarkerList *
ges_meta_container_get_marker_list (GESMetaContainer * container,
    const gchar * key)
{
  GstStructure *structure;
  const GValue *v;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  v = gst_structure_get_value (structure, key);

  if (v == NULL) {
    return NULL;
  }

  return GES_MARKER_LIST (g_value_dup_object (v));
}

/**
 * ges_meta_container_get_date:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 * @dest: (out): Destination to which value of meta item will be copied
 *
 * Gets the value of a given meta item, returns NULL if @meta_item
 * can not be found.
 */
CREATE_GETTER (date, GDate **);

/**
 * ges_meta_container_get_date_time:
 * @container: Target container
 * @meta_item: Name of the meta item to get
 * @dest: (out): Destination to which value of meta item will be copied
 *
 * Gets the value of a given meta item, returns NULL if @meta_item
 * can not be found.
 */
CREATE_GETTER (date_time, GstDateTime **);
