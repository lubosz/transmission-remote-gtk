/*
 * transmission-remote-gtk - Transmission RPC client for GTK
 * Copyright (C) 2011  Alan Fitton

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef TRG_CELL_RENDERER_SIZE_H_
#define TRG_CELL_RENDERER_SIZE_H_

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS
#define TRG_TYPE_CELL_RENDERER_SIZE trg_cell_renderer_size_get_type()
#define TRG_CELL_RENDERER_SIZE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TRG_TYPE_CELL_RENDERER_SIZE, TrgCellRendererSize))
#define TRG_CELL_RENDERER_SIZE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TRG_TYPE_CELL_RENDERER_SIZE, TrgCellRendererSizeClass))
#define TRG_IS_CELL_RENDERER_SIZE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TRG_TYPE_CELL_RENDERER_SIZE))
#define TRG_IS_CELL_RENDERER_SIZE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TRG_TYPE_CELL_RENDERER_SIZE))
#define TRG_CELL_RENDERER_SIZE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TRG_TYPE_CELL_RENDERER_SIZE, TrgCellRendererSizeClass))
    typedef struct {
    GtkCellRendererText parent;
} TrgCellRendererSize;

typedef struct {
    GtkCellRendererTextClass parent_class;
} TrgCellRendererSizeClass;

GType trg_cell_renderer_size_get_type(void);

GtkCellRenderer *trg_cell_renderer_size_new(void);

G_END_DECLS
#endif				/* TRG_CELL_RENDERER_SIZE_H_ */
