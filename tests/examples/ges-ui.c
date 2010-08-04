/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <ges/ges.h>
#include <regex.h>

/* Application Data ********************************************************/

/**
 * Contains most of the application data so that signal handlers
 * and other callbacks have easy access.
 */

typedef struct App
{
  /* back-end objects */
  GESTimeline *timeline;
  GESTimelinePipeline *pipeline;
  GESTimelineLayer *layer;

  /* application state */
  int n_objects;

  int n_selected;
  GList *selected_objects;
  GType selected_type;

  gboolean can_add_transition;
  GstState state;

  GtkListStore *model;
  GtkTreeSelection *selection;

  /* widgets */
  GtkWidget *main_window;
  GtkWidget *properties;
  GtkWidget *filesource_properties;
  GtkWidget *text_properties;
  GtkWidget *generic_duration;
  GtkWidget *background_properties;

  GtkHScale *duration;
  GtkHScale *in_point;
  GtkHScale *volume;

  GtkAction *add_file;
  GtkAction *add_test;
  GtkAction *add_title;
  GtkAction *add_transition;
  GtkAction *delete;
  GtkAction *play;
  GtkAction *stop;

  GtkComboBox *halign;
  GtkComboBox *valign;
  GtkComboBox *background_type;

  GtkEntry *text;
  GtkEntry *seconds;

  GtkSpinButton *frequency;
} App;

/* Prototypes for auto-connected signal handlers ***************************/

/**
 * These are declared non-static for signal auto-connection
 */

void window_destroy_cb (GtkObject * window, App * app);
void quit_item_activate_cb (GtkMenuItem * item, App * app);
void delete_activate_cb (GtkAction * item, App * app);
void play_activate_cb (GtkAction * item, App * app);
void add_file_activate_cb (GtkAction * item, App * app);
void add_text_activate_cb (GtkAction * item, App * app);
void add_test_activate_cb (GtkAction * item, App * app);
void add_transition_activate_cb (GtkAction * item, App * app);
void app_selection_changed_cb (GtkTreeSelection * selection, App * app);
void halign_changed_cb (GtkComboBox * widget, App * app);
void valign_changed_cb (GtkComboBox * widget, App * app);
void background_type_changed_cb (GtkComboBox * widget, App * app);
void frequency_value_changed_cb (GtkSpinButton * widget, App * app);

gboolean
duration_scale_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);

gboolean
in_point_scale_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);

gboolean
volume_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);

/* UI state functions *******************************************************/

/**
 * Update properties of UI elements that depend on more than one thing.
 */

static void
update_delete_sensitivity (App * app)
{
  /* delete will work for multiple items */
  gtk_action_set_sensitive (app->delete,
      (app->n_selected > 0) && (app->state != GST_STATE_PLAYING));
}

static void
update_add_transition_sensitivity (App * app)
{
  gtk_action_set_sensitive (app->add_transition,
      (app->can_add_transition) &&
      (app->state != GST_STATE_PLAYING) && (app->n_objects));
}

/* Backend callbacks ********************************************************/

static gboolean
find_row_for_object (GtkListStore * model, GtkTreeIter * ret,
    GESTimelineObject * object)
{
  gtk_tree_model_get_iter_first ((GtkTreeModel *) model, ret);

  while (gtk_list_store_iter_is_valid (model, ret)) {
    GESTimelineObject *obj;
    gtk_tree_model_get ((GtkTreeModel *) model, ret, 2, &obj, -1);
    if (obj == object) {
      g_object_unref (obj);
      return TRUE;
    }
    g_object_unref (obj);
    gtk_tree_model_iter_next ((GtkTreeModel *) model, ret);
  }
  return FALSE;
}

/* this callback is registered for every timeline object, and updates the
 * corresponding duration cell in the model */
static void
timeline_object_notify_duration_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  GtkTreeIter iter;
  guint64 duration = 0;

  g_object_get (object, "duration", &duration, NULL);
  find_row_for_object (app->model, &iter, object);
  gtk_list_store_set (app->model, &iter, 1, duration, -1);
}

/* these guys are only connected to filesources that are the target of the
 * current selection */

static void
filesource_notify_duration_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  guint64 duration, max_inpoint;
  duration = GES_TIMELINE_OBJECT_DURATION (object);
  max_inpoint = GES_TIMELINE_FILE_SOURCE (object)->maxduration - duration;

  gtk_range_set_value (GTK_RANGE (app->duration), duration);
  gtk_range_set_fill_level (GTK_RANGE (app->in_point), max_inpoint);

  if (max_inpoint < GES_TIMELINE_OBJECT_INPOINT (object))
    g_object_set (object, "in-point", max_inpoint, NULL);

}

static void
filesource_notify_max_duration_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  gtk_range_set_range (GTK_RANGE (app->duration),
      0, (gdouble) GES_TIMELINE_FILE_SOURCE (object)->maxduration);
  gtk_range_set_range (GTK_RANGE (app->in_point),
      0, (gdouble) GES_TIMELINE_FILE_SOURCE (object)->maxduration);
}

static void
filesource_notify_in_point_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  gtk_range_set_value (GTK_RANGE (app->in_point),
      GES_TIMELINE_OBJECT_INPOINT (object));
}

static void
object_count_changed (App * app)
{
  gtk_action_set_sensitive (app->play, app->n_objects > 0);
}

static void
title_source_text_changed_cb (GESTimelineObject * object,
    GParamSpec * arg G_GNUC_UNUSED, App * app)
{
  GtkTreeIter iter;
  gchar *text;

  g_object_get (object, "text", &text, NULL);
  if (text) {
    find_row_for_object (app->model, &iter, object);
    gtk_list_store_set (app->model, &iter, 0, text, -1);
  }
}

static void
layer_object_added_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    App * app)
{
  GtkTreeIter iter;
  gchar *description;

  GST_INFO ("layer object added cb %p %p %p", layer, object, app);

  gtk_list_store_append (app->model, &iter);

  if (GES_IS_TIMELINE_FILE_SOURCE (object)) {
    g_object_get (G_OBJECT (object), "uri", &description, NULL);
    gtk_list_store_set (app->model, &iter, 0, description, 2, object, -1);
  }

  else if (GES_IS_TIMELINE_TITLE_SOURCE (object)) {
    gtk_list_store_set (app->model, &iter, 2, object, -1);
    g_signal_connect (G_OBJECT (object), "notify::text",
        G_CALLBACK (title_source_text_changed_cb), app);
    title_source_text_changed_cb (object, NULL, app);
  }

  else if (GES_IS_TIMELINE_TEST_SOURCE (object)) {
    gtk_list_store_set (app->model, &iter, 2, object, 0, "Test Source", -1);
  }

  else if (GES_IS_TIMELINE_TRANSITION (object)) {
    gtk_list_store_set (app->model, &iter, 2, object, 0, "Transition", -1);
  }

  g_signal_connect (G_OBJECT (object), "notify::duration",
      G_CALLBACK (timeline_object_notify_duration_cb), app);
  timeline_object_notify_duration_cb (object, NULL, app);

  app->n_objects++;
  object_count_changed (app);

  app->can_add_transition = !GES_IS_TIMELINE_TRANSITION (object);
  update_add_transition_sensitivity (app);
}

static void
layer_object_removed_cb (GESTimelineLayer * layer, GESTimelineObject * object,
    App * app)
{
  GtkTreeIter iter;
  GList *tmp;

  GST_INFO ("layer object removed cb %p %p %p", layer, object, app);

  if (!find_row_for_object (GTK_LIST_STORE (app->model), &iter, object)) {
    g_print ("object deleted but we don't own it");
    return;
  }
  app->n_objects--;
  object_count_changed (app);

  gtk_list_store_remove (app->model, &iter);
  tmp = g_list_last (GES_SIMPLE_TIMELINE_LAYER (app->layer)->objects);
  if (tmp) {
    app->can_add_transition = !GES_IS_TIMELINE_TRANSITION (tmp->data);
  }
  update_add_transition_sensitivity (app);
}

static void
pipeline_state_changed_cb (App * app)
{
  if (app->state == GST_STATE_PLAYING)
    gtk_action_set_stock_id (app->play, GTK_STOCK_MEDIA_PAUSE);
  else
    gtk_action_set_stock_id (app->play, GTK_STOCK_MEDIA_PLAY);

  update_delete_sensitivity (app);
  update_add_transition_sensitivity (app);

  gtk_action_set_sensitive (app->add_file, app->state != GST_STATE_PLAYING);
  gtk_action_set_sensitive (app->add_title, app->state != GST_STATE_PLAYING);
  gtk_action_set_sensitive (app->add_test, app->state != GST_STATE_PLAYING);
  gtk_widget_set_sensitive (app->properties, app->state != GST_STATE_PLAYING);
}

static void
bus_message_cb (GstBus * bus, GstMessage * message, App * app)
{
  const GstStructure *s;
  s = gst_message_get_structure (message);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("ERROR\n");
      break;
    case GST_MESSAGE_EOS:
      gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_READY);
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (s && GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (app->pipeline)) {
        GstState old, new, pending;
        gst_message_parse_state_changed (message, &old, &new, &pending);
        app->state = new;
        pipeline_state_changed_cb (app);
      }
      break;
    default:
      break;
  }
}

/* Static UI Callbacks ******************************************************/

static void
test_source_notify_volume_changed_cb (GESTimelineObject * object, GParamSpec *
    unused G_GNUC_UNUSED, App * app)
{
  gdouble volume;

  g_object_get (G_OBJECT (object), "volume", &volume, NULL);

  gtk_range_set_value (GTK_RANGE (app->volume), volume);
}

static gboolean
check_time (const gchar * time)
{
  static regex_t re;
  static gboolean compiled = FALSE;

  if (!compiled) {
    compiled = TRUE;
    regcomp (&re, "^[0-9][0-9]:[0-5][0-9]:[0-5][0-9](.[0-9]+)?$",
        REG_EXTENDED | REG_NOSUB);
  }

  if (!regexec (&re, time, (size_t) 0, NULL, 0))
    return TRUE;
  return FALSE;
}

static guint64
str_to_time (const gchar * str)
{
  guint64 ret;
  guint64 h, m;
  gdouble s;
  gchar buf[15];

  buf[0] = str[0];
  buf[1] = str[1];
  buf[2] = '\0';

  h = strtoull (buf, NULL, 10);

  buf[0] = str[3];
  buf[1] = str[4];
  buf[2] = '\0';

  m = strtoull (buf, NULL, 10);

  strncpy (buf, &str[6], sizeof (buf));
  s = strtod (buf, NULL);

  ret = (h * 3600 * GST_SECOND) +
      (m * 60 * GST_SECOND) + ((guint64) (s * GST_SECOND));

  return ret;
}

static void
text_notify_text_changed_cb (GtkEntry * widget, GParamSpec * unused, App * app)
{
  GList *tmp;
  const gchar *text;

  text = gtk_entry_get_text (widget);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "text", text, NULL);
  }
}

static void
seconds_notify_text_changed_cb (GtkEntry * widget, GParamSpec * unused,
    App * app)
{
  GList *tmp;
  const gchar *text;

  text = gtk_entry_get_text (app->seconds);

  if (!check_time (text)) {
    gtk_entry_set_icon_from_stock (app->seconds,
        GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_DIALOG_WARNING);
  } else {
    gtk_entry_set_icon_from_stock (app->seconds,
        GTK_ENTRY_ICON_SECONDARY, NULL);
    for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
      g_object_set (GES_TIMELINE_OBJECT (tmp->data), "duration",
          (guint64) str_to_time (text), NULL);
    }
  }
}

static void
duration_cell_func (GtkTreeViewColumn * column, GtkCellRenderer * renderer,
    GtkTreeModel * model, GtkTreeIter * iter, gpointer user)
{
  gchar buf[30];
  guint64 duration;

  gtk_tree_model_get (model, iter, 1, &duration, -1);
  g_snprintf (buf, sizeof (buf), "%u:%02u:%02u.%09u", GST_TIME_ARGS (duration));
  g_object_set (renderer, "text", &buf, NULL);
}

/* UI Initialization ********************************************************/

static void
connect_to_filesource (GESTimelineObject * object, App * app)
{
  g_signal_connect (G_OBJECT (object), "notify::max-duration",
      G_CALLBACK (filesource_notify_max_duration_cb), app);
  filesource_notify_max_duration_cb (object, NULL, app);

  g_signal_connect (G_OBJECT (object), "notify::duration",
      G_CALLBACK (filesource_notify_duration_cb), app);
  filesource_notify_duration_cb (object, NULL, app);

  g_signal_connect (G_OBJECT (object), "notify::in-point",
      G_CALLBACK (filesource_notify_in_point_cb), app);
  filesource_notify_in_point_cb (object, NULL, app);
}

static void
disconnect_from_filesource (GESTimelineObject * object, App * app)
{
  g_signal_handlers_disconnect_by_func (G_OBJECT (object),
      filesource_notify_duration_cb, app);

  g_signal_handlers_disconnect_by_func (G_OBJECT (object),
      filesource_notify_max_duration_cb, app);
}

static void
connect_to_title_source (GESTimelineObject * object, App * app)
{
  guint64 duration;
  gchar buf[30];

  GESTimelineTitleSource *obj;
  obj = GES_TIMELINE_TITLE_SOURCE (object);
  gtk_combo_box_set_active (app->halign, obj->halign);
  gtk_combo_box_set_active (app->valign, obj->valign);
  gtk_entry_set_text (app->text, obj->text);

  duration = GES_TIMELINE_OBJECT_DURATION (object);

  g_snprintf (buf, sizeof (buf), "%02u:%02u:%02u.%09u",
      GST_TIME_ARGS (duration));
  gtk_entry_set_text (app->seconds, buf);
}

static void
disconnect_from_title_source (GESTimelineObject * object, App * app)
{
}

static void
connect_to_test_source (GESTimelineObject * object, App * app)
{
  gchar buf[30];
  guint64 duration;
  GObjectClass *klass;
  GParamSpecDouble *pspec;

  GESTimelineTestSource *obj;
  obj = GES_TIMELINE_TEST_SOURCE (object);
  gtk_combo_box_set_active (app->background_type, obj->vpattern);

  duration = GES_TIMELINE_OBJECT_DURATION (object);

  g_snprintf (buf, sizeof (buf), "%02u:%02u:%02u.%09u",
      GST_TIME_ARGS (duration));
  gtk_entry_set_text (app->seconds, buf);

  g_signal_connect (G_OBJECT (object), "notify::volume",
      G_CALLBACK (test_source_notify_volume_changed_cb), app);
  test_source_notify_volume_changed_cb (object, NULL, app);

  klass = G_OBJECT_GET_CLASS (G_OBJECT (object));

  pspec = G_PARAM_SPEC_DOUBLE (g_object_class_find_property (klass, "volume"));
  gtk_range_set_range (GTK_RANGE (app->volume), pspec->minimum, pspec->maximum);

  pspec = G_PARAM_SPEC_DOUBLE (g_object_class_find_property (klass, "freq"));
  gtk_spin_button_set_range (app->frequency, pspec->minimum, pspec->maximum);
  gtk_spin_button_set_value (app->frequency,
      GES_TIMELINE_TEST_SOURCE (object)->freq);
}

static void
disconnect_from_test_source (GESTimelineObject * object, App * app)
{
  g_signal_handlers_disconnect_by_func (G_OBJECT (object),
      test_source_notify_volume_changed_cb, app);
}

static void
connect_to_object (GESTimelineObject * object, App * app)
{
  if (GES_IS_TIMELINE_FILE_SOURCE (object)) {
    connect_to_filesource (object, app);
  } else if (GES_IS_TIMELINE_TITLE_SOURCE (object)) {
    connect_to_title_source (object, app);
  } else if (GES_IS_TIMELINE_TEST_SOURCE (object)) {
    connect_to_test_source (object, app);
  }
}

static void
disconnect_from_object (GESTimelineObject * object, App * app)
{
  if (GES_IS_TIMELINE_FILE_SOURCE (object)) {
    disconnect_from_filesource (object, app);
  } else if (GES_IS_TIMELINE_TITLE_SOURCE (object)) {
    disconnect_from_title_source (object, app);
  } else if (GES_IS_TIMELINE_TEST_SOURCE (object)) {
    disconnect_from_test_source (object, app);
  }
}

static GtkListStore *
get_video_patterns (void)
{
  GEnumClass *enum_class;
  GESTimelineTestSource *tr;
  GESTimelineTestSourceClass *klass;
  GParamSpec *pspec;
  GEnumValue *v;
  GtkListStore *m;
  GtkTreeIter i;

  m = gtk_list_store_new (1, G_TYPE_STRING);

  tr = ges_timeline_test_source_new ();
  klass = GES_TIMELINE_TEST_SOURCE_GET_CLASS (tr);

  pspec = g_object_class_find_property (G_OBJECT_CLASS (klass), "vpattern");

  enum_class = G_ENUM_CLASS (g_type_class_ref (pspec->value_type));

  for (v = enum_class->values; v->value_nick != NULL; v++) {
    gtk_list_store_append (m, &i);
    gtk_list_store_set (m, &i, 0, v->value_name, -1);
  }

  g_type_class_unref (enum_class);
  g_object_unref (tr);

  return m;
}

#define GET_WIDGET(dest,name,type) {\
  if (!(dest =\
    type(gtk_builder_get_object(builder, name))))\
        goto fail;\
}

static gboolean
create_ui (App * app)
{
  GtkBuilder *builder;
  GtkTreeView *timeline;
  GtkTreeViewColumn *duration_col;
  GtkCellRenderer *duration_renderer;
  GtkCellRenderer *background_type_renderer;
  GtkListStore *backgrounds;
  GstBus *bus;

  /* construct widget tree */

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "ges-ui.glade", NULL);
  gtk_builder_connect_signals (builder, app);

  /* get a bunch of widgets from the XML tree */

  GET_WIDGET (timeline, "timeline_treeview", GTK_TREE_VIEW);
  GET_WIDGET (app->properties, "properties", GTK_WIDGET);
  GET_WIDGET (app->filesource_properties, "filesource_properties", GTK_WIDGET);
  GET_WIDGET (app->text_properties, "text_properties", GTK_WIDGET);
  GET_WIDGET (app->main_window, "window", GTK_WIDGET);
  GET_WIDGET (app->duration, "duration_scale", GTK_HSCALE);
  GET_WIDGET (app->in_point, "in_point_scale", GTK_HSCALE);
  GET_WIDGET (app->halign, "halign", GTK_COMBO_BOX);
  GET_WIDGET (app->valign, "valign", GTK_COMBO_BOX);
  GET_WIDGET (app->text, "text", GTK_ENTRY);
  GET_WIDGET (duration_col, "duration_column", GTK_TREE_VIEW_COLUMN);
  GET_WIDGET (duration_renderer, "duration_renderer", GTK_CELL_RENDERER);
  GET_WIDGET (app->add_file, "add_file", GTK_ACTION);
  GET_WIDGET (app->add_title, "add_text", GTK_ACTION);
  GET_WIDGET (app->add_test, "add_test", GTK_ACTION);
  GET_WIDGET (app->add_transition, "add_transition", GTK_ACTION);
  GET_WIDGET (app->delete, "delete", GTK_ACTION);
  GET_WIDGET (app->play, "play", GTK_ACTION);
  GET_WIDGET (app->stop, "stop", GTK_ACTION);
  GET_WIDGET (app->seconds, "seconds", GTK_ENTRY);
  GET_WIDGET (app->generic_duration, "generic_duration", GTK_WIDGET);
  GET_WIDGET (app->background_type, "background_type", GTK_COMBO_BOX);
  GET_WIDGET (app->background_properties, "background_properties", GTK_WIDGET);
  GET_WIDGET (app->frequency, "frequency", GTK_SPIN_BUTTON);
  GET_WIDGET (app->volume, "volume", GTK_HSCALE);

  /* get text notifications */

  g_signal_connect (app->text, "notify::text",
      G_CALLBACK (text_notify_text_changed_cb), app);

  g_signal_connect (app->seconds, "notify::text",
      G_CALLBACK (seconds_notify_text_changed_cb), app);

  /* we care when the tree selection changes */

  if (!(app->selection = gtk_tree_view_get_selection (timeline)))
    goto fail;

  gtk_tree_selection_set_mode (app->selection, GTK_SELECTION_MULTIPLE);

  g_signal_connect (app->selection, "changed",
      G_CALLBACK (app_selection_changed_cb), app);

  /* create the model for the treeview */

  if (!(app->model =
          gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_OBJECT)))
    goto fail;

  gtk_tree_view_set_model (timeline, GTK_TREE_MODEL (app->model));

  /* register custom cell data function */

  gtk_tree_view_column_set_cell_data_func (duration_col, duration_renderer,
      duration_cell_func, NULL, NULL);

  /* initialize combo boxes */

  if (!(backgrounds = get_video_patterns ()))
    goto fail;

  if (!(background_type_renderer = gtk_cell_renderer_text_new ()))
    goto fail;

  gtk_combo_box_set_model (app->background_type, (GtkTreeModel *)
      backgrounds);

  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (app->background_type),
      background_type_renderer, FALSE);

  gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (app->background_type),
      background_type_renderer, "text", 0);

  /* register callbacks on GES objects */

  g_signal_connect (app->layer, "object-added",
      G_CALLBACK (layer_object_added_cb), app);
  g_signal_connect (app->layer, "object-removed",
      G_CALLBACK (layer_object_removed_cb), app);

  bus = gst_pipeline_get_bus (GST_PIPELINE (app->pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), app);

  /* success */

  g_object_unref (G_OBJECT (builder));
  return TRUE;

fail:
  g_object_unref (G_OBJECT (builder));
  return FALSE;
}

#undef GET_WIDGET

/* application methods ******************************************************/

static void selection_foreach (GtkTreeModel * model, GtkTreePath * path,
    GtkTreeIter * iter, gpointer user);

static void
app_toggle_playback (App * app)
{
  if (app->state != GST_STATE_PLAYING) {
    gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_PLAYING);
  } else {
    gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_PAUSED);
  }
}

typedef struct
{
  GList *objects;
  guint n;
} select_info;

static void
app_update_selection (App * app)
{
  GList *cur;
  GType type;
  select_info info = { NULL, 0 };

  /* clear old selection */
  for (cur = app->selected_objects; cur; cur = cur->next) {
    disconnect_from_object (cur->data, app);
    g_object_unref (cur->data);
    cur->data = NULL;
  }
  g_list_free (app->selected_objects);
  app->selected_objects = NULL;
  app->n_selected = 0;

  /* get new selection */
  gtk_tree_selection_selected_foreach (GTK_TREE_SELECTION (app->selection),
      selection_foreach, &info);
  app->selected_objects = info.objects;
  app->n_selected = info.n;

  type = G_TYPE_NONE;
  if (app->selected_objects) {
    type = G_TYPE_FROM_INSTANCE (app->selected_objects->data);
    for (cur = app->selected_objects; cur; cur = cur->next) {
      if (type != G_TYPE_FROM_INSTANCE (cur->data)) {
        type = G_TYPE_NONE;
        break;
      }
    }
  }

  if (type != G_TYPE_NONE) {
    for (cur = app->selected_objects; cur; cur = cur->next) {
      connect_to_object (cur->data, app);
    }
  }

  app->selected_type = type;
}

static void
selection_foreach (GtkTreeModel * model, GtkTreePath * path, GtkTreeIter
    * iter, gpointer user)
{
  select_info *info = (select_info *) user;
  GESTimelineObject *obj;

  gtk_tree_model_get (model, iter, 2, &obj, -1);
  info->objects = g_list_append (info->objects, obj);

  info->n++;
  return;
}

static GList *
app_get_selected_objects (App * app)
{
  return g_list_copy (app->selected_objects);
}

static void
app_delete_objects (App * app, GList * objects)
{
  GList *cur;

  for (cur = objects; cur; cur = cur->next) {
    ges_timeline_layer_remove_object (app->layer,
        GES_TIMELINE_OBJECT (cur->data));
    cur->data = NULL;
  }

  g_list_free (objects);
}

static void
app_add_file (App * app, gchar * uri)
{
  GESTimelineObject *obj;

  GST_DEBUG ("adding file %s", uri);

  obj = GES_TIMELINE_OBJECT (ges_timeline_filesource_new (uri));

  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER (app->layer),
      obj, -1);
}

static void
app_add_title (App * app)
{
  GESTimelineObject *obj;

  GST_DEBUG ("adding title");

  obj = GES_TIMELINE_OBJECT (ges_timeline_title_source_new ());
  g_object_set (G_OBJECT (obj), "duration", GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER (app->layer),
      obj, -1);
}

static void
app_add_test (App * app)
{
  GESTimelineObject *obj;

  GST_DEBUG ("adding test");

  obj = GES_TIMELINE_OBJECT (ges_timeline_test_source_new ());
  g_object_set (G_OBJECT (obj), "duration", GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
      (app->layer), obj, -1);
}

static void
app_add_transition (App * app)
{
  GESTimelineObject *obj;

  GST_DEBUG ("adding transition");

  obj = GES_TIMELINE_OBJECT (ges_timeline_transition_new
      (GES_VIDEO_TRANSITION_TYPE_CROSSFADE));
  g_object_set (G_OBJECT (obj), "duration", GST_SECOND, NULL);

  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER
      (app->layer), obj, -1);
}

static void
app_dispose (App * app)
{
  if (app) {
    if (app->pipeline) {
      gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_NULL);
      gst_object_unref (app->pipeline);
    }

    g_free (app);
  }
}

static App *
app_new (void)
{
  App *ret;
  ret = g_new0 (App, 1);

  ret->selected_type = G_TYPE_NONE;

  if (!ret)
    return NULL;

  if (!(ret->timeline = ges_timeline_new_audio_video ()))
    goto fail;

  if (!(ret->pipeline = ges_timeline_pipeline_new ()))
    goto fail;

  if (!ges_timeline_pipeline_add_timeline (ret->pipeline, ret->timeline))
    goto fail;

  if (!(ret->layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ()))
    goto fail;

  if (!(ges_timeline_add_layer (ret->timeline, ret->layer)))
    goto fail;

  if (!(create_ui (ret)))
    goto fail;

  return ret;

fail:
  app_dispose (ret);
  return NULL;
}

/* UI callbacks  ************************************************************/

void
window_destroy_cb (GtkObject * window, App * app)
{
  gtk_main_quit ();
}

void
quit_item_activate_cb (GtkMenuItem * item, App * app)
{
  gtk_main_quit ();
}

void
delete_activate_cb (GtkAction * item, App * app)
{
  /* get a gslist of selected track objects */
  GList *objects = NULL;

  objects = app_get_selected_objects (app);
  app_delete_objects (app, objects);
}

void
add_file_activate_cb (GtkAction * item, App * app)
{
  GtkFileChooserDialog *dlg;

  GST_DEBUG ("add file signal handler");

  dlg = (GtkFileChooserDialog *) gtk_file_chooser_dialog_new ("Add File...",
      GTK_WINDOW (app->main_window),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

  g_object_set (G_OBJECT (dlg), "select-multiple", TRUE, NULL);

  if (gtk_dialog_run ((GtkDialog *) dlg) == GTK_RESPONSE_OK) {
    GSList *uris;
    GSList *cur;
    uris = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (dlg));
    for (cur = uris; cur; cur = cur->next)
      app_add_file (app, cur->data);
    g_slist_free (uris);
  }
  gtk_widget_destroy ((GtkWidget *) dlg);
}

void
add_text_activate_cb (GtkAction * item, App * app)
{
  app_add_title (app);
}

void
add_test_activate_cb (GtkAction * item, App * app)
{
  app_add_test (app);
}

void
add_transition_activate_cb (GtkAction * item, App * app)
{
  app_add_transition (app);
}

void
play_activate_cb (GtkAction * item, App * app)
{
  app_toggle_playback (app);
}

void
app_selection_changed_cb (GtkTreeSelection * selection, App * app)
{
  app_update_selection (app);

  update_delete_sensitivity (app);
  update_add_transition_sensitivity (app);

  gtk_widget_set_visible (app->properties, app->n_selected > 0);

  gtk_widget_set_visible (app->filesource_properties,
      app->selected_type == GES_TYPE_TIMELINE_FILE_SOURCE);

  gtk_widget_set_visible (app->text_properties,
      app->selected_type == GES_TYPE_TIMELINE_TITLE_SOURCE);

  gtk_widget_set_visible (app->generic_duration,
      app->selected_type == GES_TYPE_TIMELINE_TITLE_SOURCE ||
      app->selected_type == GES_TYPE_TIMELINE_TEST_SOURCE);

  gtk_widget_set_visible (app->background_properties,
      app->selected_type == GES_TYPE_TIMELINE_TEST_SOURCE);
}

gboolean
duration_scale_change_value_cb (GtkRange * range, GtkScrollType unused,
    gdouble value, App * app)
{
  GList *i;

  for (i = app->selected_objects; i; i = i->next) {
    guint64 duration, maxduration;
    maxduration = GES_TIMELINE_FILE_SOURCE (i->data)->maxduration;
    duration = (value < maxduration ? (value > 0 ? value : 0) : maxduration);
    g_object_set (G_OBJECT (i->data), "duration", (guint64) duration, NULL);
  }
  return TRUE;
}

gboolean
in_point_scale_change_value_cb (GtkRange * range, GtkScrollType unused,
    gdouble value, App * app)
{
  GList *i;

  for (i = app->selected_objects; i; i = i->next) {
    guint64 in_point, maxduration;
    maxduration = GES_TIMELINE_FILE_SOURCE (i->data)->maxduration -
        GES_TIMELINE_OBJECT_DURATION (i->data);
    in_point = (value < maxduration ? (value > 0 ? value : 0) : maxduration);
    g_object_set (G_OBJECT (i->data), "in-point", (guint64) in_point, NULL);
  }
  return TRUE;
}

void
halign_changed_cb (GtkComboBox * widget, App * app)
{
  GList *tmp;
  int active;

  active = gtk_combo_box_get_active (app->halign);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "halignment", active, NULL);
  }
}

void
valign_changed_cb (GtkComboBox * widget, App * app)
{
  GList *tmp;
  int active;

  active = gtk_combo_box_get_active (app->valign);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "valignment", active, NULL);
  }
}

void
background_type_changed_cb (GtkComboBox * widget, App * app)
{
  GList *tmp;
  gint p;
  p = gtk_combo_box_get_active (widget);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "vpattern", (gint) p, NULL);
  }
}

void
frequency_value_changed_cb (GtkSpinButton * widget, App * app)
{
  GList *tmp;
  gdouble value;

  value = gtk_spin_button_get_value (widget);

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "freq", (gdouble) value, NULL);
  }
}

gboolean
volume_change_value_cb (GtkRange * widget, GtkScrollType unused, gdouble
    value, App * app)
{
  GList *tmp;

  value = value >= 0 ? (value <= 2.0 ? value : 2.0) : 0;

  for (tmp = app->selected_objects; tmp; tmp = tmp->next) {
    g_object_set (G_OBJECT (tmp->data), "volume", (gdouble) value, NULL);
  }
  return TRUE;
}

/* main *********************************************************************/

int
main (int argc, char *argv[])
{
  App *app;

  /* intialize GStreamer and GES */
  if (!g_thread_supported ())
    g_thread_init (NULL);

  gst_init (&argc, &argv);
  ges_init ();

  /* initialize UI */
  gtk_init (&argc, &argv);

  if ((app = app_new ())) {
    gtk_main ();
    app_dispose (app);
  }

  return 0;
}
