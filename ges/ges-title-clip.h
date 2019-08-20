/* GStreamer Editing Services
 * Copyright (C) 2009 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2009 Nokia Corporation
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

#ifndef _GES_TIMELINE_TITLESOURCE
#define _GES_TIMELINE_TITLESOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-source-clip.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_TITLE_CLIP ges_title_clip_get_type()

#define GES_TITLE_CLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TITLE_CLIP, GESTitleClip))

#define GES_TITLE_CLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TITLE_CLIP, GESTitleClipClass))

#define GES_IS_TITLE_CLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TITLE_CLIP))

#define GES_IS_TITLE_CLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TITLE_CLIP))

#define GES_TITLE_CLIP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TITLE_CLIP, GESTitleClipClass))

typedef struct _GESTitleClipPrivate GESTitleClipPrivate;

/**
 * GESTitleClip:
 *
 * Render stand-alone titles in GESLayer.
 */

struct _GESTitleClip {
  GESSourceClip parent;

  /*< private >*/
  GESTitleClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESTitleClipClass {
  /*< private >*/
  GESSourceClipClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
GType ges_title_clip_get_type (void);

G_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_text( GESTitleClip * self, const gchar * text);

G_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_font_desc (GESTitleClip * self, const gchar * font_desc);

G_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_valignment (GESTitleClip * self, GESTextVAlign valign);

G_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_halignment (GESTitleClip * self, GESTextHAlign halign);

G_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_color (GESTitleClip * self, guint32 color);

G_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_background (GESTitleClip * self, guint32 background);

G_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_xpos (GESTitleClip * self, gdouble position);

G_DEPRECATED_FOR(ges_timeline_element_set_children_properties) void
ges_title_clip_set_ypos (GESTitleClip * self, gdouble position);

G_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const gchar*
ges_title_clip_get_font_desc (GESTitleClip * self);

G_DEPRECATED_FOR(ges_timeline_element_get_children_properties) GESTextVAlign
ges_title_clip_get_valignment (GESTitleClip * self);

G_DEPRECATED_FOR(ges_timeline_element_get_children_properties) GESTextHAlign
ges_title_clip_get_halignment (GESTitleClip * self);

G_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const guint32
ges_title_clip_get_text_color (GESTitleClip * self);

G_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const guint32
ges_title_clip_get_background_color (GESTitleClip * self);

G_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const gdouble
ges_title_clip_get_xpos (GESTitleClip * self);

G_DEPRECATED_FOR(ges_timeline_element_get_children_properties) const gdouble
ges_title_clip_get_ypos (GESTitleClip * self);

G_DEPRECATED_FOR(ges_timeline_element_get_children_properties)
const gchar* ges_title_clip_get_text (GESTitleClip * self);

GES_API
GESTitleClip* ges_title_clip_new (void);

G_END_DECLS

#endif /* _GES_TIMELINE_TITLESOURCE */

