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

#include <glib-object.h>
#include <json-glib/json-glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "torrent.h"
#include "trg-state-selector.h"
#include "trg-torrent-model.h"
#include "util.h"
#include "trg-prefs.h"
#include "trg-client.h"

enum {
    SELECTOR_STATE_CHANGED,
    SELECTOR_SIGNAL_COUNT
};

enum {
    PROP_0,
    PROP_CLIENT
};

static guint signals[SELECTOR_SIGNAL_COUNT] = { 0 };

G_DEFINE_TYPE(TrgStateSelector, trg_state_selector, GTK_TYPE_TREE_VIEW)
#define TRG_STATE_SELECTOR_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRG_TYPE_STATE_SELECTOR, TrgStateSelectorPrivate))
typedef struct _TrgStateSelectorPrivate TrgStateSelectorPrivate;

struct _TrgStateSelectorPrivate {
    guint flag;
    gboolean showDirs;
    gboolean showTrackers;
    TrgClient *client;
    TrgPrefs *prefs;
    GHashTable *trackers;
    GHashTable *directories;
    GRegex *urlHostRegex;
    gint n_categories;
    GtkListStore *store;
    GtkTreeRowReference *error_rr;
};

GRegex *trg_state_selector_get_url_host_regex(TrgStateSelector * s)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(s);
    return priv->urlHostRegex;
}

guint32 trg_state_selector_get_flag(TrgStateSelector * s)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(s);
    return priv->flag;
}

static void state_selection_changed(GtkTreeSelection * selection,
				    gpointer data)
{
    TrgStateSelectorPrivate *priv;
    GtkTreeIter iter;
    GtkTreeModel *stateModel;
    guint index = 0;

    priv = TRG_STATE_SELECTOR_GET_PRIVATE(data);

    if (gtk_tree_selection_get_selected(selection, &stateModel, &iter))
	gtk_tree_model_get(stateModel, &iter, STATE_SELECTOR_BIT,
			   &priv->flag, STATE_SELECTOR_INDEX, &index, -1);
    else
	priv->flag = 0;

    trg_prefs_set_int(priv->prefs, TRG_PREFS_STATE_SELECTOR_LAST, index,
		      TRG_PREFS_GLOBAL);

    g_signal_emit(TRG_STATE_SELECTOR(data),
		  signals[SELECTOR_STATE_CHANGED], 0, priv->flag);
}

static GtkTreeRowReference *quick_tree_ref_new(GtkTreeModel * model,
					       GtkTreeIter * iter)
{
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
    GtkTreeRowReference *rr = gtk_tree_row_reference_new(model, path);
    gtk_tree_path_free(path);
    return rr;
}

struct cruft_remove_args {
    GHashTable *table;
    gint64 serial;
};

static gboolean trg_state_selector_remove_cruft(gpointer key,
						gpointer value,
						gpointer data)
{
    struct cruft_remove_args *args = (struct cruft_remove_args *) data;
    GtkTreeRowReference *rr = (GtkTreeRowReference *) value;
    GtkTreeModel *model = gtk_tree_row_reference_get_model(rr);
    GtkTreePath *path = gtk_tree_row_reference_get_path(rr);
    gboolean remove;

    GtkTreeIter iter;
    gint64 currentSerial;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, STATE_SELECTOR_SERIAL, &currentSerial,
		       -1);

    remove = (args->serial != currentSerial);

    gtk_tree_path_free(path);

    return remove;
}

gchar *trg_state_selector_get_selected_text(TrgStateSelector * s)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(s));
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *name = NULL;

    if (gtk_tree_selection_get_selected(sel, &model, &iter))
	gtk_tree_model_get(model, &iter, STATE_SELECTOR_NAME, &name, -1);

    return name;
}

static void trg_state_selector_update_serial(GtkTreeModel * model,
					     GtkTreeRowReference * rr,
					     gint64 serial)
{
    GtkTreeIter iter;
    GtkTreePath *path = gtk_tree_row_reference_get_path(rr);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, STATE_SELECTOR_SERIAL,
		       serial, -1);
    gtk_tree_path_free(path);
}

static void refresh_statelist_cb(GtkWidget * w, gpointer data)
{
    trg_state_selector_update(TRG_STATE_SELECTOR(data));
}

static void view_popup_menu(GtkWidget * treeview, GdkEventButton * event,
			    gpointer data G_GNUC_UNUSED)
{
    GtkWidget *menu, *item;

    menu = gtk_menu_new();

    item = gtk_image_menu_item_new_with_label(GTK_STOCK_REFRESH);
    gtk_image_menu_item_set_use_stock(GTK_IMAGE_MENU_ITEM(item), TRUE);
    gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM
					      (item), TRUE);
    g_signal_connect(item, "activate", G_CALLBACK(refresh_statelist_cb),
		     treeview);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_widget_show_all(menu);

    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
		   (event != NULL) ? event->button : 0,
		   gdk_event_get_time((GdkEvent *) event));
}

static gboolean view_onPopupMenu(GtkWidget * treeview, gpointer userdata)
{
    view_popup_menu(treeview, NULL, userdata);
    return TRUE;
}

static gboolean view_onButtonPressed(GtkWidget * treeview,
				     GdkEventButton * event,
				     gpointer userdata)
{
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
	view_popup_menu(treeview, event, userdata);
	return TRUE;
    }

    return FALSE;
}

struct state_find_pos {
    int offset;
    int range;
    int pos;
    const gchar *name;
};

static gboolean trg_state_selector_find_pos_foreach(GtkTreeModel * model,
						    GtkTreePath * path,
						    GtkTreeIter * iter,
						    gpointer data)
{
    struct state_find_pos *args = (struct state_find_pos *) data;
    gchar *name;
    gboolean res;

    if (args->pos < args->offset) {
	args->pos++;
	return FALSE;
    } else if (args->range >= 0
	       && args->pos > args->offset + args->range - 1) {
	return TRUE;
    }

    gtk_tree_model_get(model, iter, STATE_SELECTOR_NAME, &name, -1);
    res = g_strcmp0(name, args->name) >= 0;
    g_free(name);

    if (!res)
	args->pos++;

    return res;
}

static void trg_state_selector_insert(TrgStateSelector * s, int offset,
				      gint range, const gchar * name,
				      GtkTreeIter * iter)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(s));

    struct state_find_pos args;
    args.offset = offset;
    args.pos = 0;
    args.range = range;
    args.name = name;

    gtk_tree_model_foreach(model, trg_state_selector_find_pos_foreach,
			   &args);
    gtk_list_store_insert(GTK_LIST_STORE(model), iter, args.pos);
}

void trg_state_selector_update(TrgStateSelector * s)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(s);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(s));
    TrgClient *client = priv->client;
    gint64 updateSerial = trg_client_get_serial(client);
    GList *torrentItemRefs;
    GtkTreeIter torrentIter, iter;
    GList *trackersList, *trackerItem, *li;
    GtkTreeRowReference *rr;
    GtkTreePath *path;
    GtkTreeModel *torrentModel;
    gpointer result;
    struct cruft_remove_args cruft;

    if (!trg_client_is_connected(client))
	return;

    torrentItemRefs =
	g_hash_table_get_values(trg_client_get_torrent_table(client));

    for (li = torrentItemRefs; li; li = g_list_next(li)) {
	JsonObject *t = NULL;
	rr = (GtkTreeRowReference *) li->data;
	path = gtk_tree_row_reference_get_path(rr);
	torrentModel = gtk_tree_row_reference_get_model(rr);

	if (path) {
	    if (gtk_tree_model_get_iter(torrentModel, &torrentIter, path)) {
		gtk_tree_model_get(torrentModel, &torrentIter,
				   TORRENT_COLUMN_JSON, &t, -1);
	    }
	    gtk_tree_path_free(path);
	}

	if (!t)
	    continue;

	if (priv->showTrackers) {
	    trackersList
		= json_array_get_elements(torrent_get_tracker_stats(t));
	    for (trackerItem = trackersList; trackerItem; trackerItem
		 = g_list_next(trackerItem)) {
		JsonObject *tracker = json_node_get_object((JsonNode *)
							   trackerItem->
							   data);
		const gchar *announceUrl =
		    tracker_stats_get_announce(tracker);
		gchar *announceHost =
		    trg_gregex_get_first(priv->urlHostRegex,
					 announceUrl);

		if (!announceHost)
		    continue;

		result = g_hash_table_lookup(priv->trackers, announceHost);

		if (result) {
		    trg_state_selector_update_serial(model,
						     (GtkTreeRowReference
						      *) result,
						     updateSerial);
		    g_free(announceHost);
		} else {
		    trg_state_selector_insert(s, priv->n_categories,
					      g_hash_table_size(priv->
								trackers),
					      announceHost, &iter);
		    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
				       STATE_SELECTOR_ICON,
				       GTK_STOCK_NETWORK,
				       STATE_SELECTOR_NAME, announceHost,
				       STATE_SELECTOR_SERIAL, updateSerial,
				       STATE_SELECTOR_BIT,
				       FILTER_FLAG_TRACKER,
				       STATE_SELECTOR_INDEX, 0, -1);
		    g_hash_table_insert(priv->trackers, announceHost,
					quick_tree_ref_new(model, &iter));
		}
	    }
	    g_list_free(trackersList);
	}

	if (priv->showDirs) {
	    gchar *dir;
	    gtk_tree_model_get(torrentModel, &torrentIter,
			       TORRENT_COLUMN_DOWNLOADDIR_SHORT, &dir, -1);

	    result = g_hash_table_lookup(priv->directories, dir);
	    if (result) {
		trg_state_selector_update_serial(model,
						 (GtkTreeRowReference *)
						 result, updateSerial);
	    } else {
		trg_state_selector_insert(s,
					  priv->n_categories +
					  g_hash_table_size(priv->
							    trackers), -1,
					  dir, &iter);
		gtk_list_store_set(GTK_LIST_STORE(model), &iter,
				   STATE_SELECTOR_ICON,
				   GTK_STOCK_DIRECTORY,
				   STATE_SELECTOR_NAME, dir,
				   STATE_SELECTOR_SERIAL, updateSerial,
				   STATE_SELECTOR_BIT, FILTER_FLAG_DIR,
				   STATE_SELECTOR_INDEX, 0, -1);
		g_hash_table_insert(priv->directories, g_strdup(dir),
				    quick_tree_ref_new(model, &iter));
	    }

	    g_free(dir);
	}
    }

    g_list_free(torrentItemRefs);

    cruft.serial = trg_client_get_serial(client);

    if (priv->showTrackers) {
	cruft.table = priv->trackers;
	g_hash_table_foreach_remove(priv->trackers,
				    trg_state_selector_remove_cruft,
				    &cruft);
    }

    if (priv->showDirs) {
	cruft.table = priv->directories;
	g_hash_table_foreach_remove(priv->directories,
				    trg_state_selector_remove_cruft,
				    &cruft);
    }
}

void trg_state_selector_set_show_dirs(TrgStateSelector * s, gboolean show)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(s);
    priv->showDirs = show;
    if (!show)
	g_hash_table_remove_all(priv->directories);
    else
	trg_state_selector_update(s);
}

void trg_state_selector_set_show_trackers(TrgStateSelector * s,
					  gboolean show)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(s);
    priv->showTrackers = show;
    if (!show)
	g_hash_table_remove_all(priv->trackers);
    else
	trg_state_selector_update(s);
}

static void trg_state_selector_add_state(TrgStateSelector * selector,
					 GtkTreeIter * iter, gint pos,
					 gchar * icon, gchar * name,
					 guint32 flag)
{
    TrgStateSelectorPrivate *priv =
	TRG_STATE_SELECTOR_GET_PRIVATE(selector);
    GtkListStore *model =
	GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(selector)));

    if (pos < 0)
	gtk_list_store_append(priv->store, iter);
    else
	gtk_list_store_insert(priv->store, iter, pos);

    gtk_list_store_set(model, iter, STATE_SELECTOR_ICON, icon,
		       STATE_SELECTOR_NAME, name, STATE_SELECTOR_BIT, flag,
		       STATE_SELECTOR_INDEX,
		       gtk_tree_model_iter_n_children(GTK_TREE_MODEL
						      (model), NULL) - 1,
		       -1);

    priv->n_categories++;
}

static void remove_row_ref_and_free(GtkTreeRowReference * rr)
{
    GtkTreeModel *model = gtk_tree_row_reference_get_model(rr);
    GtkTreePath *path = gtk_tree_row_reference_get_path(rr);
    GtkTreeIter iter;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
    gtk_tree_path_free(path);
    gtk_tree_row_reference_free(rr);
}

void trg_state_selector_stats_update(TrgStateSelector * s,
				     trg_torrent_model_update_stats *
				     stats)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(s);
    GtkTreeIter iter;
    if (stats->error > 0 && !priv->error_rr) {
	trg_state_selector_add_state(s, &iter, priv->n_categories - 1,
				     GTK_STOCK_DIALOG_WARNING, _("Error"),
				     TORRENT_FLAG_ERROR);
	priv->error_rr =
	    quick_tree_ref_new(GTK_TREE_MODEL(priv->store), &iter);
    } else if (stats->error < 1 && priv->error_rr) {
	remove_row_ref_and_free(priv->error_rr);
	priv->error_rr = NULL;
	priv->n_categories--;
    }
}

void trg_state_selector_disconnect(TrgStateSelector * s)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(s);

    if (priv->error_rr) {
	remove_row_ref_and_free(priv->error_rr);
	priv->error_rr = NULL;
	priv->n_categories--;
    }

    g_hash_table_remove_all(priv->trackers);
    g_hash_table_remove_all(priv->directories);
}

static void trg_state_selector_init(TrgStateSelector * self)
{
}

TrgStateSelector *trg_state_selector_new(TrgClient * client)
{
    return g_object_new(TRG_TYPE_STATE_SELECTOR, "client", client, NULL);
}

static GObject *trg_state_selector_constructor(GType type,
					       guint
					       n_construct_properties,
					       GObjectConstructParam *
					       construct_params)
{
    GObject *object;
    TrgStateSelector *selector;
    TrgStateSelectorPrivate *priv;
    GtkListStore *store;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkTreeIter iter;
    gint index;
    GtkTreeSelection *selection;

    object = G_OBJECT_CLASS
	(trg_state_selector_parent_class)->constructor(type,
						       n_construct_properties,
						       construct_params);

    selector = TRG_STATE_SELECTOR(object);
    priv = TRG_STATE_SELECTOR_GET_PRIVATE(object);

    priv->urlHostRegex = trg_uri_host_regex_new();
    priv->trackers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
					   (GDestroyNotify)
					   remove_row_ref_and_free);
    priv->directories =
	g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
			      (GDestroyNotify) remove_row_ref_and_free);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(object), FALSE);

    column = gtk_tree_view_column_new();

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    g_object_set(renderer, "stock-size", 4, NULL);
    gtk_tree_view_column_set_attributes(column, renderer, "stock-id", 0,
					NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_attributes(column, renderer, "text", 1, NULL);

    gtk_tree_view_append_column(GTK_TREE_VIEW(object), column);

    store = priv->store =
	gtk_list_store_new(STATE_SELECTOR_COLUMNS, G_TYPE_STRING,
			   G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INT64,
			   G_TYPE_UINT);
    gtk_tree_view_set_model(GTK_TREE_VIEW(object), GTK_TREE_MODEL(store));

    trg_state_selector_add_state(selector, &iter, -1, GTK_STOCK_ABOUT,
				 _("All"), 0);
    trg_state_selector_add_state(selector, &iter, -1, GTK_STOCK_GO_DOWN,
				 _("Downloading"),
				 TORRENT_FLAG_DOWNLOADING);
    trg_state_selector_add_state(selector, &iter, -1,
				 GTK_STOCK_MEDIA_REWIND, _("Queue Down"),
				 TORRENT_FLAG_DOWNLOADING_WAIT);
    trg_state_selector_add_state(selector, &iter, -1, GTK_STOCK_GO_UP,
				 _("Seeding"), TORRENT_FLAG_SEEDING);
    trg_state_selector_add_state(selector, &iter, -1,
				 GTK_STOCK_MEDIA_FORWARD, _("Queue Up"),
				 TORRENT_FLAG_SEEDING_WAIT);
    trg_state_selector_add_state(selector, &iter, -1,
				 GTK_STOCK_MEDIA_PAUSE, _("Paused"),
				 TORRENT_FLAG_PAUSED);
    trg_state_selector_add_state(selector, &iter, -1, GTK_STOCK_APPLY,
				 _("Complete"), TORRENT_FLAG_COMPLETE);
    trg_state_selector_add_state(selector, &iter, -1, GTK_STOCK_SELECT_ALL,
				 _("Incomplete"), TORRENT_FLAG_INCOMPLETE);
    trg_state_selector_add_state(selector, &iter, -1, GTK_STOCK_NETWORK,
				 _("Active"), TORRENT_FLAG_ACTIVE);
    trg_state_selector_add_state(selector, &iter, -1, GTK_STOCK_REFRESH,
				 _("Checking"), TORRENT_FLAG_CHECKING_ANY);
    trg_state_selector_add_state(selector, &iter, -1, NULL, NULL, 0);

    gtk_tree_view_set_rubber_banding(GTK_TREE_VIEW(object), TRUE);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(object));

    g_signal_connect(G_OBJECT(selection), "changed",
		     G_CALLBACK(state_selection_changed), object);
    g_signal_connect(object, "button-press-event",
		     G_CALLBACK(view_onButtonPressed), NULL);
    g_signal_connect(object, "popup-menu", G_CALLBACK(view_onPopupMenu),
		     NULL);

    gtk_tree_view_set_search_column(GTK_TREE_VIEW(object),
				    STATE_SELECTOR_NAME);

    index = trg_prefs_get_int(priv->prefs, TRG_PREFS_STATE_SELECTOR_LAST,
			      TRG_PREFS_GLOBAL);
    if (index > 0 && gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store),
						   &iter, NULL, index)) {
	GtkTreeSelection *selection =
	    gtk_tree_view_get_selection(GTK_TREE_VIEW(object));
	gtk_tree_selection_select_iter(selection, &iter);
    }

    priv->showDirs =
	trg_prefs_get_bool(priv->prefs, TRG_PREFS_KEY_FILTER_DIRS,
			   TRG_PREFS_GLOBAL);
    priv->showTrackers =
	trg_prefs_get_bool(priv->prefs, TRG_PREFS_KEY_FILTER_TRACKERS,
			   TRG_PREFS_GLOBAL);

    return object;
}

void trg_state_selector_set_queues_enabled(TrgStateSelector * s,
					   gboolean enabled)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(s);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(s));
    GtkTreeIter iter;

    if (enabled) {
	trg_state_selector_add_state(s, &iter, 2, GTK_STOCK_MEDIA_REWIND,
				     _("Queue Down"),
				     TORRENT_FLAG_DOWNLOADING_WAIT);
	trg_state_selector_add_state(s, &iter, 4, GTK_STOCK_MEDIA_FORWARD,
				     _("Queue Up"),
				     TORRENT_FLAG_SEEDING_WAIT);
    } else {
	gtk_tree_model_iter_nth_child(model, &iter, NULL, 4);
	gtk_list_store_remove(GTK_LIST_STORE(model), &iter);

	gtk_tree_model_iter_nth_child(model, &iter, NULL, 2);
	gtk_list_store_remove(GTK_LIST_STORE(model), &iter);

	priv->n_categories -= 2;
    }
}

static void trg_state_selector_get_property(GObject * object,
					    guint property_id,
					    GValue * value,
					    GParamSpec * pspec)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(object);
    switch (property_id) {
    case PROP_CLIENT:
	g_value_set_object(value, priv->client);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	break;
    }
}

static void trg_state_selector_set_property(GObject * object,
					    guint prop_id,
					    const GValue * value,
					    GParamSpec *
					    pspec G_GNUC_UNUSED)
{
    TrgStateSelectorPrivate *priv = TRG_STATE_SELECTOR_GET_PRIVATE(object);

    switch (prop_id) {
    case PROP_CLIENT:
	priv->client = g_value_get_object(value);
	priv->prefs = trg_client_get_prefs(priv->client);
	break;
    }
}

static void trg_state_selector_class_init(TrgStateSelectorClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->constructor = trg_state_selector_constructor;
    object_class->set_property = trg_state_selector_set_property;
    object_class->get_property = trg_state_selector_get_property;

    signals[SELECTOR_STATE_CHANGED] = g_signal_new("torrent-state-changed",
						   G_TYPE_FROM_CLASS
						   (object_class),
						   G_SIGNAL_RUN_LAST |
						   G_SIGNAL_ACTION,
						   G_STRUCT_OFFSET
						   (TrgStateSelectorClass,
						    torrent_state_changed),
						   NULL, NULL,
						   g_cclosure_marshal_VOID__UINT,
						   G_TYPE_NONE, 1,
						   G_TYPE_UINT);

    g_object_class_install_property(object_class,
				    PROP_CLIENT,
				    g_param_spec_object("client",
							"Client",
							"Client",
							TRG_TYPE_CLIENT,
							G_PARAM_READWRITE |
							G_PARAM_CONSTRUCT_ONLY
							|
							G_PARAM_STATIC_NAME
							|
							G_PARAM_STATIC_NICK
							|
							G_PARAM_STATIC_BLURB));

    g_type_class_add_private(klass, sizeof(TrgStateSelectorPrivate));
}
