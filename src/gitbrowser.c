
#include <string.h>

#include "geanyplugin.h"

GeanyPlugin         *geany_plugin;
GeanyData           *geany_data;
GeanyFunctions      *geany_functions;

PLUGIN_VERSION_CHECK(147)

PLUGIN_SET_INFO("Git Browser",
		"A minimalistic browser for Git repositories.",
                "1.0",
                "Emil Brink <emil@obsession.se>")

enum
{
	CMD_ADD_REPOSITORY = 0,
	CMD_ADD_REPOSITORY_FROM_DOCUMENT,

	NUM_COMMANDS
};

static struct
{
	GtkTreeModel	*model;
	GtkWidget	*view;
	GtkAction	*actions[NUM_COMMANDS];
} gitbrowser;

/* -------------------------------------------------------------------------------------------------------------- */

/* Trickery to make the same function work both as the signal-handler, and for creating the actual GtkAction. Too weird? */
#define	CMD_INIT(n, l, tt, s)	if(action == NULL) { GtkAction **me = (GtkAction **) user; *me = gtk_action_new(n, _(l), _(tt), s); return; }

static void cmd_repository_add_activate(GtkAction *action, gpointer user)
{
	CMD_INIT("repository-add", _("Add..."), _("Add a new repository based on a filesystem location."), GTK_STOCK_ADD)

	printf("time to add!\n");
}

static void cmd_repository_add_from_document_activate(GtkAction *action, gpointer user)
{
	GeanyDocument	*doc;

	CMD_INIT("repository-add-from-document", _("Add from Document"), _("Add a new repository from the current document's location."), GTK_STOCK_ADD)

	if((doc = document_get_current()) != NULL)
	{
		GString	*tmp = g_string_new(doc->real_path);
		gchar	*slash;

		printf("current document is '%s'\n", doc->real_path);
		/* Step up the directory hierarchy, looking for a ".git" directory that marks the repo's root. */
		while(TRUE)
		{
			if((slash = strrchr(tmp->str, G_DIR_SEPARATOR)) != NULL)
			{
				gchar	*git;
				*slash = '\0';	/* Stamp out the slash, truncating the path. */
				git = g_build_filename(tmp->str, ".git", NULL);
				if(g_file_test(git, G_FILE_TEST_IS_DIR))
				{
					gchar	*name = strrchr(tmp->str, G_DIR_SEPARATOR);
					if(name != NULL)
					{
						printf("found repository (%s) root in '%s'\n", name + 1, git);
						tmp->str[0] = '\0';
					}
				}
				g_free(git);
			}
			else
				break;
		}
	}
}

void init_commands(GtkAction **actions)
{
	typedef void (*ActivateOrCreate)(GtkAction *action, gpointer user);
	const ActivateOrCreate funcs[] = {
		cmd_repository_add_activate,
		cmd_repository_add_from_document_activate,
	};
	size_t	i;

	for(i = 0; i < sizeof funcs / sizeof *funcs; i++)
	{
		funcs[i](NULL, &actions[i]);
		g_signal_connect(G_OBJECT(actions[i]), "activate", G_CALLBACK(funcs[i]), NULL);
	}
}

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

static void menu_popup_repositories(GdkEventButton *evt)
{
	GtkWidget	*menu, *item;

	menu = gtk_menu_new();

	item = gtk_action_create_menu_item(gitbrowser.actions[CMD_ADD_REPOSITORY]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	item = gtk_action_create_menu_item(gitbrowser.actions[CMD_ADD_REPOSITORY_FROM_DOCUMENT]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show_all(menu);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, evt->button, evt->time);
}

static void evt_tree_button_press(GtkWidget *wid, GdkEventButton *evt, gpointer user)
{
	GtkTreePath	*path = NULL;

	if(evt->button != 3)
		return;

	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(wid), evt->x, evt->y, &path, NULL, NULL, NULL);
	if(path != NULL)
	{
		gint		depth;
		const gint	*indices = gtk_tree_path_get_indices_with_depth(path, &depth);

		if(indices != NULL)
		{
			if(depth == 1 && indices[0] == 0)
				menu_popup_repositories(evt);
		}
		gtk_tree_path_free(path);
	}
}

GtkWidget * tree_view_new(GtkTreeModel *model)
{
	GtkWidget		*view;
        GtkCellRenderer         *cr;
        GtkTreeViewColumn       *vc;

	view = gtk_tree_view_new_with_model(model);

	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(string)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gitbrowser.view), FALSE);

	g_signal_connect(G_OBJECT(view), "button_press_event", G_CALLBACK(evt_tree_button_press), NULL);

	return view;
}

/* -------------------------------------------------------------------------------------------------------------- */

void plugin_init(GeanyData *geany_data)
{
	init_commands(gitbrowser.actions);

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
