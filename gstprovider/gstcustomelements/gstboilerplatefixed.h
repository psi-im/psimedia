/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2002 Thomas Vander Stichele <thomas@apestaart.org>
 *
 * gstutils.h: Header for various utility functions
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

// here's a replacement for GST_BOILERPLATE_FULL that doesn't have warnings

#ifndef GSTBOILERPLATEFIXED_H
#define GSTBOILERPLATEFIXED_H

#include <gst/gstutils.h>

G_BEGIN_DECLS

#undef GST_BOILERPLATE_FULL
#define GST_BOILERPLATE_FULL(type, type_as_function, parent_type, parent_type_macro, additional_initializations)	\
									\
static void type_as_function ## _base_init     (gpointer      g_class);	\
static void type_as_function ## _class_init    (type ## Class *g_class);\
static void type_as_function ## _init	       (type          *object,	\
                                                type ## Class *g_class);\
static parent_type ## Class *parent_class = NULL;			\
static void								\
type_as_function ## _class_init_trampoline (gpointer g_class,		\
					    gpointer data)		\
{									\
  (void)data;								\
  parent_class = (parent_type ## Class *)				\
      g_type_class_peek_parent (g_class);				\
  type_as_function ## _class_init ((type ## Class *)g_class);		\
}									\
									\
GType type_as_function ## _get_type (void);				\
									\
GType									\
type_as_function ## _get_type (void)					\
{									\
  static GType object_type = 0;						\
  if (G_UNLIKELY (object_type == 0)) {					\
    object_type = gst_type_register_static_full (parent_type_macro, #type,	\
	sizeof (type ## Class),					\
        type_as_function ## _base_init,					\
        NULL,		  /* base_finalize */				\
        type_as_function ## _class_init_trampoline,			\
        NULL,		  /* class_finalize */				\
        NULL,               /* class_data */				\
        sizeof (type),							\
        0,                  /* n_preallocs */				\
        (GInstanceInitFunc) type_as_function ## _init,                  \
        NULL,                                                           \
        (GTypeFlags) 0);				                \
    additional_initializations (object_type);				\
  }									\
  return object_type;							\
}

G_END_DECLS

#endif
