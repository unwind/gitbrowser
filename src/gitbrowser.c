
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
	CMD_REMOVE_REPOSITORY,

	CMD_DIR_EXPAND,
	CMD_DIR_COLLAPSE,

	CMD_FILE_OPEN,

	NUM_COMMANDS
};

static struct
{
	GtkTreeModel	*model;
	GtkWidget	*view;
	GtkAction	*actions[NUM_COMMANDS];
	GtkTreePath	*menu_click;
} gitbrowser;

/* -------------------------------------------------------------------------------------------------------------- */

gboolean	tree_model_find_repository(GtkTreeModel *model, const gchar *root_path, GtkTreeIter *iter);
void		tree_model_build_repository(GtkTreeModel *model, GtkTreeIter *root, const gchar *root_path);

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

		printf("current document is '%s'\n", doc->real_path);
		/* Step up through the directory hierarchy, looking for a ".git" directory that marks the repo's root. */
		while(TRUE)
		{
			gchar	*slash, *git, *name;

			if((slash = strrchr(tmp->str, G_DIR_SEPARATOR)) != NULL)
			{
				*slash = '\0';	/* Stamp out the slash, truncating the path. */
				git = g_build_filename(tmp->str, ".git", NULL);
				if(g_file_test(git, G_FILE_TEST_IS_DIR))
				{
					name = strrchr(tmp->str, G_DIR_SEPARATOR);
					if(name != NULL)
					{
						GtkTreeIter	iter;

						printf("found repository (%s) root in '%s'\n", name + 1, git);
						printf(" repo root is '%s'\n", tmp->str);
						if(!tree_model_find_repository(gitbrowser.model, tmp->str, &iter))
						{
							tree_model_build_repository(gitbrowser.model, NULL, tmp->str);
						}
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

static void cmd_repository_remove(GtkAction *action, gpointer user)
{
	GtkTreeIter	iter;

	CMD_INIT("repository-remove", _("Remove"), _("Removes this repository from the tree view, forgetting all about it."), GTK_STOCK_DELETE);

	if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.menu_click))
	{
		gtk_tree_store_remove(GTK_TREE_STORE(gitbrowser.model), &iter);
	}
}

static void cmd_dir_expand(GtkAction *action, gpointer user)
{
	CMD_INIT("dir-expand", _("Expand"), _("Expands a directory node."), NULL);

	gtk_tree_view_expand_row(GTK_TREE_VIEW(gitbrowser.view), gitbrowser.menu_click, TRUE);
}

static void cmd_dir_collapse(GtkAction *action, gpointer user)
{
	CMD_INIT("dir-collapse", _("Collapse"), _("Collapses a directory node."), NULL);

	gtk_tree_view_collapse_row(GTK_TREE_VIEW(gitbrowser.view), gitbrowser.menu_click);
}

static void cmd_file_open(GtkAction *action, gpointer user)
{
	GtkTreeIter	iter, child;

	CMD_INIT("file-open", _("Open"), _("Opens a file as a new document, or focuses the document if already opened."), GTK_STOCK_OPEN);

	if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.menu_click))
	{
		GString	*path = g_string_sized_new(1024);
		gchar	*component;

		/* Walk towards the root, building the filename as we go. */
		do
		{
			gtk_tree_model_get(gitbrowser.model, &iter, 1, &component, -1);
			if(component != NULL)
			{
				if(path->len > 0)
					g_string_prepend(path, G_DIR_SEPARATOR_S);
				g_string_prepend(path, component);
				g_free(component);
			}
			child = iter;
		} while(gtk_tree_model_iter_parent(gitbrowser.model, &iter, &child));
		document_open_file(path->str, FALSE, NULL, NULL);
		g_string_free(path, TRUE);
	}
}

void init_commands(GtkAction **actions)
{
	typedef void (*ActivateOrCreate)(GtkAction *action, gpointer user);
	const ActivateOrCreate funcs[] = {
		cmd_repository_add_activate,
		cmd_repository_add_from_document_activate,
		cmd_repository_remove,
		cmd_dir_expand,
		cmd_dir_collapse,
		cmd_file_open,
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
		return NULL;
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

/* Scans forwards through text, looking for the next separator. Terminates (!) string and returns it, or returns NULL. */
gchar * tok_tokenize_next(gchar *text, gchar **endptr, gchar separator)
{
	gchar	*anchor;

	if(text == NULL || *text == '\0')
		return NULL;
	while(*text == separator)
		text++;
	anchor = text;
	while(*text && *text != separator)
		text++;
	if(*text == separator)
	{
		*text = '\0';
		if(endptr != NULL)
			*endptr = text + 1;
		return anchor;
	}
	else if(*text == '\0')
	{
		if(endptr != NULL)
			*endptr = NULL;
		return anchor;
	}
	return NULL;
}

/* -------------------------------------------------------------------------------------------------------------- */

GtkTreeModel * tree_model_new(void)
{
	GtkTreeStore	*ts;
	GtkTreeIter	iter;

	/* First column is display text, second is corresponding path (or path part). */
	ts = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_store_append(ts, &iter, NULL);
	gtk_tree_store_set(ts, &iter, 0, _("Repositories (Right-click to add)"), 1, NULL,-1);

	return GTK_TREE_MODEL(ts);
}

/* Look up a repository, by searching for a node immediately under the root that has the given path as its data. */
gboolean tree_model_find_repository(GtkTreeModel *model, const gchar *root_path, GtkTreeIter *iter)
{
	GtkTreeIter	root;
	gboolean	found = FALSE;

	if(gtk_tree_model_get_iter_first(model, &root))
	{
		if(gtk_tree_model_iter_children(model, iter, &root))
		{
			gchar	*data;

			do
			{
				gtk_tree_model_get(model, iter, 1, &data, -1);
				if(data != NULL)
				{
					found = strcmp(data, root_path) == 0;
					g_free(data);
				}
			} while(gtk_tree_model_iter_next(model, iter));
		}
	}
	return found;
}

static void	tree_model_build_populate(GtkTreeModel *model, gchar *lines, GtkTreeIter *parent);
static void	tree_model_build_traverse(GtkTreeModel *model, GNode *root, GtkTreeIter *parent);

void tree_model_build_repository(GtkTreeModel *model, GtkTreeIter *repo, const gchar *root_path)
{
	GtkTreeIter	new;
	const gchar	*slash;
	gchar		*git_ls_files[] = { "git", "ls-files", NULL };
	gchar		*git_stdout = NULL, *git_stderr = NULL;

	slash = strrchr(root_path, G_DIR_SEPARATOR);
	if(slash == NULL)
		slash = root_path;
	else
		slash++;

	if(repo == NULL)
	{
		GtkTreeIter	iter;

		if(gtk_tree_model_get_iter_first(model, &iter))
		{
			repo = &new;
			gtk_tree_store_append(GTK_TREE_STORE(model), repo, &iter);
		}
	}
	/* At this point, we have a root iter in the tree, which we need to populate. */
	gtk_tree_store_set(GTK_TREE_STORE(model), repo, 0, slash, 1, root_path,-1);

	/* Now list the repository, and build a tree representation. Easy-peasy, right? */
	if(subprocess_run(root_path, git_ls_files, NULL, &git_stdout, &git_stderr))
	{
		GtkTreePath	*path;

		tree_model_build_populate(model, git_stdout, repo);
		g_free(git_stdout);
		g_free(git_stderr);

		path = gtk_tree_model_get_path(model, repo);
		gtk_tree_view_expand_to_path(GTK_TREE_VIEW(gitbrowser.view), path);
		gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(gitbrowser.view), path, NULL, NULL, FALSE);
		gtk_tree_path_free(path);
	}
}

/* Look for a child with the given text as its data; if not found it's added in the proper location and returned. */
static GNode * get_child(GNode *root, const gchar *text)
{
	GNode	*here;

	if(root == NULL || text == NULL)
		return NULL;

	if((here = g_node_first_child(root)) != NULL)
	{
		while(here != NULL)
		{
			const gint	rel = strcmp(text, here->data);

			if(rel == 0)
				return here;
			if(rel < 0)
				return g_node_insert_data_before(root, here, (gpointer) text);
			here = g_node_next_sibling(here);
		}
	}
	return g_node_append_data(root, (gpointer) text);
}

static void tree_model_build_populate(GtkTreeModel *model, gchar *lines, GtkTreeIter *parent)
{
	gchar	*line, *nextline, *dir, *endptr;
	GNode	*root = g_node_new(""), *prev;

	/* Let's cheat: build a GNode n:ary tree first, then use that to build GtkTreeModel data. */
	while((line = tok_tokenize_next(lines, &nextline, '\n')) != NULL)
	{
		prev = root;
		dir = line;
		while((dir = tok_tokenize_next(dir, &endptr, G_DIR_SEPARATOR)) != NULL)
		{
			prev = get_child(prev, dir);
			dir = endptr;
		}
		if(prev == root)
			get_child(root, line);
		lines = nextline;
	}
	tree_model_build_traverse(model, root, parent);
	g_node_destroy(root);
}

/* Traverse the children of the given GNode tree, and build a corresponding GtkTreeModel.
 * The traversal order is special: inner nodes first, to group directories on top.
*/
static void tree_model_build_traverse(GtkTreeModel *model, GNode *root, GtkTreeIter *parent)
{
	GNode		*child;
	GtkTreeIter	iter;
	gchar		*dname;

	/* Inner nodes. */
	for(child = g_node_first_child(root); child != NULL; child = g_node_next_sibling(child))
	{
		if(g_node_first_child(child) == NULL)
			continue;
		/* We now know this is an inner node; add tree node and recurse. */
		gtk_tree_store_append(GTK_TREE_STORE(model), &iter, parent);
		dname = g_filename_display_name(child->data);
		gtk_tree_store_set(GTK_TREE_STORE(model), &iter, 0, dname, 1, child->data,-1);
		g_free(dname);
		tree_model_build_traverse(model, child, &iter);
	}
	/* Leaves. */
	for(child = g_node_first_child(root); child != NULL; child = g_node_next_sibling(child))
	{
		if(g_node_first_child(child) != NULL)
			continue;
		gtk_tree_store_append(GTK_TREE_STORE(model), &iter, parent);
		dname = g_filename_display_name(child->data);
		gtk_tree_store_set(GTK_TREE_STORE(model), &iter, 0, dname, 1, child->data,-1);
		g_free(dname);
	}
}

static gboolean evt_menu_selection_done(GtkWidget *wid, gpointer user)
{
	if(gitbrowser.menu_click != NULL)
	{
		gtk_tree_path_free(gitbrowser.menu_click);
		gitbrowser.menu_click = NULL;
	}

	return FALSE;
}

/* Creates a new popup menu suitable for use in our tree, and connects the selection-done signal. */
static GtkWidget * menu_popup_create(void)
{
	GtkWidget	*menu;

	menu = gtk_menu_new();
	g_signal_connect(G_OBJECT(menu), "selection_done", G_CALLBACK(evt_menu_selection_done), NULL);
	return menu;
}

static void menu_popup_repositories(GdkEventButton *evt)
{
	GtkWidget	*menu = menu_popup_create(), *item;

	item = gtk_action_create_menu_item(gitbrowser.actions[CMD_ADD_REPOSITORY]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	item = gtk_action_create_menu_item(gitbrowser.actions[CMD_ADD_REPOSITORY_FROM_DOCUMENT]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show_all(menu);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, evt->button, evt->time);
}

static void menu_popup_repository(GdkEventButton *evt)
{
	GtkWidget	*menu = menu_popup_create();

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_action_create_menu_item(gitbrowser.actions[CMD_REMOVE_REPOSITORY]));
	gtk_widget_show_all(menu);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, evt->button, evt->time);
}

static void menu_popup_directory(GdkEventButton *evt)
{
	GtkWidget	*menu = menu_popup_create();

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_action_create_menu_item(gitbrowser.actions[CMD_DIR_EXPAND]));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_action_create_menu_item(gitbrowser.actions[CMD_DIR_COLLAPSE]));
	gtk_widget_show_all(menu);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, evt->button, evt->time);
}

static void menu_popup_file(GdkEventButton *evt)
{
	GtkWidget	*menu = menu_popup_create();

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_action_create_menu_item(gitbrowser.actions[CMD_FILE_OPEN]));
	gtk_widget_show_all(menu);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, evt->button, evt->time);
}

static gboolean evt_tree_button_press(GtkWidget *wid, GdkEventButton *evt, gpointer user)
{
	if(evt->button != 3)
		return FALSE;

	if(gitbrowser.menu_click != NULL)
	{
		gtk_tree_path_free(gitbrowser.menu_click);
		gitbrowser.menu_click = NULL;
	}

	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(wid), evt->x, evt->y, &gitbrowser.menu_click, NULL, NULL, NULL);
	if(gitbrowser.menu_click != NULL)
	{
		gint		depth;
		const gint	*indices = gtk_tree_path_get_indices_with_depth(gitbrowser.menu_click, &depth);

		if(indices != NULL)
		{
			if(depth == 1 && indices[0] == 0)
				menu_popup_repositories(evt);
			else if(depth == 2)
				menu_popup_repository(evt);
			else if(depth >= 3)
			{
				GtkTreeIter	iter;

				/* Need to determine if clicked path is file or directory. */
				if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.menu_click))
				{
					if(gtk_tree_model_iter_has_child(gitbrowser.model, &iter))
						menu_popup_directory(evt);
					else
						menu_popup_file(evt);
				}
			}
		}
		return TRUE;
	}
	return FALSE;
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
	GtkWidget	*scwin;

	init_commands(gitbrowser.actions);

	gitbrowser.model = tree_model_new();
	gitbrowser.view = tree_view_new(gitbrowser.model);

	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), gitbrowser.view);
	gtk_widget_show_all(scwin);
	gtk_notebook_append_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), scwin, gtk_label_new("Git Browser"));
}

void plugin_cleanup(void)
{
}
