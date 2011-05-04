
#include "geanyplugin.h"

PLUGIN_VERSION_CHECK(147)

PLUGIN_SET_INFO("Git Browser",
		"A minimalistic browser for Git repositories.",
                "1.0",
                "Emil Brink <emil@obsession.se>")

static struct
{
	GtkTreeModel	*model;
	GtkWidget	*view;
} gitbrowser;

/* -------------------------------------------------------------------------------------------------------------- */

/* Trivial convenience wrapper for g_spawn_sync(); returns command output. */
gboolean subprocess_run(const gchar* working_dir, gchar **argv, gchar **env, gchar **output, gchar **error)
{
	return g_spawn_sync(working_dir, argv, env, G_SPAWN_SEARCH_PATH, NULL, NULL, output, error, NULL, NULL);
}

/* -------------------------------------------------------------------------------------------------------------- */

/* Split the given multi-line string into individual lines, copying and returning each one.
 * Too long lines will be silently truncated, but properly skipped.
 * Returns pointer to start of next line, or NULL when no more lines are found.
*/
const gchar * tok_tokenize_next_line(const gchar *lines, gchar *buffer, size_t buf_size)
{
	if(lines == NULL || *lines == '\0' || buffer == NULL)
		return FALSE;
	/* Copy characters until linefeed. Don't overflow. */
	buf_size--;
	while(*lines != '\0')
	{
		if(*lines == '\n')
		{
			*buffer = '\0';
			while(*lines == '\n')
				lines++;
			return lines;
		}
		if(buf_size > 0)
		{
			*buffer++ = *lines;
			buf_size--;
		}
		lines++;
	}
	return lines;
}

/* -------------------------------------------------------------------------------------------------------------- */

GtkTreeModel * tree_model_new(void)
{
	GtkTreeStore	*ts;
	GtkTreeIter	iter;

	ts = gtk_tree_store_new(1, G_TYPE_STRING);
	gtk_tree_store_append(ts, &iter, NULL);
	gtk_tree_store_set(ts, &iter, 0, _("Repositories (Right-click to add)"), -1);

	return GTK_TREE_MODEL(ts);
}

static void evt_tree_button_press(GtkWidget *wid, GdkEvent *evt, gpointer user)
{
	printf("button press!\n");
}

GtkWidget * tree_view_new(GtkTreeModel *model)
{
	GtkWidget		*view;
        GtkCellRenderer         *cr;
        GtkTreeViewColumn       *vc;

	view = gtk_tree_view_new_with_model(model);

	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(string)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(gitbrowser.view), vc);

/*	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gitbrowser.view), FALSE);*/

	g_signal_connect(G_OBJECT(view), "button_press_event", G_CALLBACK(evt_tree_button_press), NULL);

	return view;
}

/* -------------------------------------------------------------------------------------------------------------- */

void plugin_init(GeanyData *geany_data)
{
	gitbrowser.model = tree_model_new();
	gitbrowser.view = tree_view_new(gitbrowser.model);

	gtk_widget_show_all(gitbrowser.view);
	gtk_notebook_append_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), gitbrowser.view, gtk_label_new("Git Browser"));

/*	if(subprocess_run("/home/emil/data/workspace/gitbrowser", get_files, NULL, &output, &error) >= 0)
	{
		const gchar	*iter = output;
		gchar		line[1024];

		while(iter = tok_tokenize_next_line(iter, line, sizeof line))
			printf("'%s'\n", line);
		g_free(output);
		g_free(error);
	}
*/
}

void plugin_cleanup(void)
{
}
