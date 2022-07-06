/*
 * gitbrowser - A geany plugin to work with Git repositories.
 *
 * Copyright (C) 2011-2014 by Emil Brink <emil@obsession.se>.
 *
 * This file is part of gitbrowser.
 *
 * gitbrowser is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gitbrowser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gitbrowser.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <ctype.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>

#include "geanyplugin.h"

#include "levenshtein.h"

#define	MNEMONIC_NAME			"gitbrowser"
#define	CFG_REPOSITORIES		"repositories"
#define	CFG_EXPANDED			"expanded"
#define	CFG_QUICK_OPEN_FILTER_MAX_TIME	"quick_open_filter_max_time"
#define	CFG_QUICK_OPEN_HIDE_SRC		"quick_open_hide_re"
#define	CFG_TERMINAL_CMD		"terminal_cmd"
#define	PATH_SEPARATOR_CHAR		':'
#define	REPO_IS_SEPARATOR		"-"

GeanyPlugin         *geany_plugin;
GeanyData           *geany_data;

PLUGIN_VERSION_CHECK(147)

PLUGIN_SET_INFO("Git Browser",
		"A minimalistic browser for Git repositories. Implements a 'Quick Open' command to quickly jump to any file in a repository.",
		"1.5.2",
		"Emil Brink <emil@obsession.se>")

enum
{
	CMD_REPOSITORY_ADD = 0,
	CMD_REPOSITORY_ADD_MULTIPLE,
	CMD_REPOSITORY_ADD_FROM_DOCUMENT,
	CMD_REPOSITORY_ADD_SEPARATOR,

	CMD_REPOSITORY_REMOVE,
	CMD_REPOSITORY_REMOVE_ALL,
	CMD_REPOSITORY_OPEN_QUICK,
	CMD_REPOSITORY_OPEN_QUICK_FROM_DOCUMENT,
	CMD_REPOSITORY_GREP,
	CMD_REPOSITORY_REFRESH,
	CMD_REPOSITORY_MOVE_UP,
	CMD_REPOSITORY_MOVE_DOWN,

	CMD_DIR_EXPAND,
	CMD_DIR_COLLAPSE,
	CMD_DIR_EXPLORE,
	CMD_DIR_TERMINAL,

	CMD_FILE_OPEN,
	CMD_FILE_COPY_NAME,

	NUM_COMMANDS
};

enum {
	KEY_REPOSITORY_OPEN_QUICK_FROM_DOCUMENT,
	KEY_REPOSITORY_GREP,
	NUM_KEYS
};

enum {
	QO_NAME = 0,
	QO_NAME_LOWER,
	QO_PATH,
	QO_VISIBLE,
	QO_DISTANCE,
	QO_NUM_COLUMNS
} QuickOpenColumns;

/* Data held in the 'names' array of QuickOpenInfo. Faster than storing GString, keeps data
 * const in the list store, reducing dynamic memory management overhead when reading from it.
*/
typedef struct {
	gpointer	name;
	gpointer	name_lower;		/* For case-insensitive searching. */
	gpointer	path;
	guint16		distance;		/* Levenshtein distance to typed string. */
} QuickOpenRow;

typedef struct
{
	GtkWidget		*dialog;
	GtkWidget		*view;
	GtkWidget		*entry;
	GtkWidget		*spinner;
	GtkWidget		*label;
	GtkTreeSelection	*selection;
	gulong			files_total;
	gulong			files_filtered;
	GtkListStore		*store;			/* Only pointers into 'names' in here. */
	GString			*names;			/* All names (files and paths), concatenated with '\0's in-between. */
	GHashTable		*dedup;			/* Used during construction to de-duplicate names. Saves tons of memory. */
	GArray			*array;			/* Used during construction to sort quickly. */
	GtkTreeModel		*filter;		/* Filtered view of the quick open model. */
	GtkTreeModel		*sort;			/* Sorted view of the filtered model. */
	gchar			filter_text[128];	/* Cached so we don't need to query GtkEntry on each filter callback. */
	gchar			filter_lower[128];	/* Lower-case version of the filter text. */
	guint			filter_idle;
	guint			filter_row;		/* For the idle function. */
	LDState			filter_ld;
} QuickOpenInfo;

typedef struct
{
	gchar		root_path[1024];		/* Root path, this is where the ".git/" subdirectory is. */
	QuickOpenInfo	quick_open;			/* State tracking for the "Quick Open" command's dialog. */
} Repository;

static struct
{
	gint		page;
	GtkTreeModel	*model;
	GtkWidget	*view;
	GtkAction	*actions[NUM_COMMANDS];
	GtkWidget	*action_menu_items[NUM_COMMANDS];
	GtkWidget	*main_menu;
	GtkTreePath	*click_path;
	GRegex		*quick_open_hide;

	GtkWidget	*add_dialog;

	GHashTable	*repositories;			/* Hashed on root path. */

	GeanyKeyGroup	*key_group;

	gchar		*config_filename;
	StashGroup	*prefs;

	gchar		*quick_open_hide_src;
	gint		quick_open_filter_max_time;	/* In milliseconds. */
	gchar		*terminal_cmd;
} gitbrowser;

typedef struct
{
	GtkWidget	*filter_re;
	GtkWidget	*filter_time;
	GtkWidget	*terminal_cmd;
} PrefsWidgets;

/* -------------------------------------------------------------------------------------------------------------- */

Repository *	repository_new(const gchar *root_path);
Repository *	repository_find_by_path(const gchar *path);
void		repository_open_quick(Repository *repo);

static void	open_quick_reset_filter(void);

gboolean	tree_model_find_repository(GtkTreeModel *model, const gchar *root_path, GtkTreeIter *iter);
void		tree_model_build_repository(GtkTreeModel *model, GtkTreeIter *root, const gchar *root_path);
void		tree_model_build_separator(GtkTreeModel *model);
gboolean	tree_model_open_document(GtkTreeModel *model, GtkTreePath *path);
gboolean	tree_model_get_document_path(GtkTreeModel *model, const GtkTreeIter *iter, gchar *buf, gsize buf_max);
void		tree_model_foreach(GtkTreeModel *model, GtkTreeIter *root, void (*node_callback)(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user), gpointer user);

GString *	tree_view_get_expanded(GtkTreeView *view);
void		tree_view_set_expanded(GtkTreeView *view, const gchar *paths);

gchar *		tok_tokenize_next(gchar *text, gchar **endptr, gchar separator);

static gboolean	cb_treeview_separator(GtkTreeModel *model, GtkTreeIter *iter, gpointer data);

/* -------------------------------------------------------------------------------------------------------------- */

/* Trivial convenience wrapper for g_spawn_sync(); returns command output. */
gboolean subprocess_run(const gchar* working_dir, gchar **argv, gchar **env, gchar **output, gchar **error)
{
	return g_spawn_sync(working_dir, argv, env, G_SPAWN_SEARCH_PATH, NULL, NULL, output, error, NULL, NULL);
}

/* -------------------------------------------------------------------------------------------------------------- */

/* Trickery to make a single function both register/create an action, and implement that action's action. */
#define	CMD_INIT(n, l, tt, s)	if(action == NULL) { GtkAction **me = (GtkAction **) user; *me = gtk_action_new(n, _(l), _(tt), s); return; }

static void add_dialog_open(const gchar *title)
{
	if(gitbrowser.add_dialog == NULL)
	{
		gitbrowser.add_dialog = gtk_file_chooser_dialog_new("", NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
	}
	gtk_window_set_title(GTK_WINDOW(gitbrowser.add_dialog), _(title));
}

static void cmd_repository_add(GtkAction *action, gpointer user)
{
	gint	response;

	CMD_INIT("repository-add", _("Add..."), _("Add a new repository based on a filesystem location."), GTK_STOCK_ADD)
	add_dialog_open("Add Repository");
	response = gtk_dialog_run(GTK_DIALOG(gitbrowser.add_dialog));
	gtk_widget_hide(gitbrowser.add_dialog);
	if(response == GTK_RESPONSE_OK)
	{
		gchar	*path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(gitbrowser.add_dialog));

		if(path != NULL)
		{
			gchar	*git;

			/* Not already loaded? */
			if(repository_find_by_path(path) == NULL)
			{
				/* Does it even have a ".git" directory in it? */
				git = g_build_filename(path, ".git", NULL);
				if(g_file_test(git, G_FILE_TEST_IS_DIR))
				{
					Repository	*repo = repository_new(path);

					tree_model_build_repository(gitbrowser.model, NULL, repo->root_path);
				}
				g_free(git);
			}
			g_free(path);
		}
	}
}

/* Another recursive file tree walker, that adds repos along the way. Repos never nest. */
static void add_multiple(const char *root)
{
	GDir	*dir = g_dir_open(root, 0, NULL);
	const gchar	*fn;

	if(dir == NULL)
		return;

	while((fn = g_dir_read_name(dir)) != NULL)
	{
		gchar	*full = g_build_filename(root, fn, NULL);
		if(full == NULL)
			break;
		/* Is it a directory? */
		if(g_file_test(full, G_FILE_TEST_IS_DIR))
		{
			/* Not already added? If so, don't recurse. Warning: this is O(n) but we expect human scale. */
			if(repository_find_by_path(full) == NULL)
			{
				/* Check for repo here. */
				gchar	*git = g_build_filename(full, ".git", NULL);
				if(git == NULL)
					break;
				if(g_file_test(git, G_FILE_TEST_IS_DIR))
				{
					Repository	*repo = repository_new(full);
					tree_model_build_repository(gitbrowser.model, NULL, repo->root_path);
				}
				else	/* This is the recursive step. */
					add_multiple(full);
				g_free(git);
			 }
		}
		g_free(full);
	}
	g_dir_close(dir);
}

static void cmd_repository_add_multiple(GtkAction *action, gpointer user)
{
	gint	response;

	CMD_INIT("repository-add-multiple", _("Add Multiple..."), _("Add multiple repositories by searching a filesystem location recursively."), GTK_STOCK_ADD)
	add_dialog_open("Add Multiple Repositories");
	response = gtk_dialog_run(GTK_DIALOG(gitbrowser.add_dialog));
	gtk_widget_hide(gitbrowser.add_dialog);
	if(response == GTK_RESPONSE_OK)
	{
		gchar	*path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(gitbrowser.add_dialog));
		add_multiple(path);
		g_free(path);
	}
}

static void cmd_repository_add_from_document(GtkAction *action, gpointer user)
{
	GeanyDocument	*doc;

	CMD_INIT("repository-add-from-document", _("Add from Document"), _("Add a new repository from the current document's location."), GTK_STOCK_ADD)

	if((doc = document_get_current()) != NULL)
	{
		GString	*tmp = g_string_new(doc->real_path);

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

						if(!tree_model_find_repository(gitbrowser.model, tmp->str, &iter))
						{
							Repository	*repo = repository_new(tmp->str);

							tree_model_build_repository(gitbrowser.model, NULL, repo->root_path);
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

static void cmd_repository_add_separator(GtkAction *action, gpointer user)
{
	CMD_INIT("repository-add-separator", _("Add Separator"), _("Add a separator line between repositories."), NULL)

	tree_model_build_separator(gitbrowser.model);
}

static void cmd_repository_remove(GtkAction *action, gpointer user)
{
	GtkTreeIter	iter;

	CMD_INIT("repository-remove", _("Remove"), _("Removes this repository from the tree view, forgetting all about it."), GTK_STOCK_DELETE);

	if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.click_path))
	{
		gtk_tree_store_remove(GTK_TREE_STORE(gitbrowser.model), &iter);
	}
}

static void cmd_repository_remove_all(GtkAction *action, gpointer user)
{
	GtkTreeIter	iter, child;

	CMD_INIT("repository-remove-all", _("Remove All"), _("Removes all known repositories from the plugin's browser tree."), GTK_STOCK_CLEAR);

	if(gtk_tree_model_get_iter_first(gitbrowser.model, &iter))
	{
		while(gtk_tree_model_iter_children(gitbrowser.model, &child, &iter))
		{
			gtk_tree_store_remove(GTK_TREE_STORE(gitbrowser.model), &child);
		}
	}
}

static void cmd_repository_open_quick(GtkAction *action, gpointer user)
{
	GtkTreeIter	iter;
	Repository	*repo = NULL;

	CMD_INIT("repository-open-quick", _("Quick Open ..."), _("Opens a document anywhere in the repository, with filtering."), GTK_STOCK_OPEN);

	if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.click_path))
	{
		gchar	*path = NULL;

		gtk_tree_model_get(gitbrowser.model, &iter, 1, &path, -1);
		if(path != NULL)
		{
			repo = repository_find_by_path(path);
			g_free(path);
		}
	}
	repository_open_quick(repo);
}

static void cmd_repository_open_quick_from_document(GtkAction *action, gpointer user)
{
	GeanyDocument	*doc = document_get_current();
	Repository	*repo;

	CMD_INIT("repository-open-quick-from-document", _("Quick Open from Document ..."), _("Opens the Quick Open dialog for the current docuḿent's repository"), GTK_STOCK_FIND);

	if(doc == NULL)
		return;
	if((repo = repository_find_by_path(doc->real_path)) != NULL)
		repository_open_quick(repo);
}

/* Helper function to either get a repository from a click in the browser, or from the current document. */
static const Repository * get_repository(void)
{
	const GeanyDocument	*doc = document_get_current();
	const Repository	*repo = NULL;

	if(gitbrowser.click_path != NULL)
	{
		GtkTreeIter	iter;
	
		if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.click_path))
		{
			gchar	*path = NULL;

			gtk_tree_model_get(gitbrowser.model, &iter, 1, &path, -1);
			if(path != NULL)
			{
				repo = repository_find_by_path(path);
				g_free(path);
			}
		}
	}
	if(doc != NULL && repo == NULL)
		repo = repository_find_by_path(doc->real_path);
	return repo;
}

/* Put some reasonable word in the entry; taken from either selection or word under cursor. */
static void grep_get_word(GtkWidget *entry)
{
	const GeanyDocument	*doc = document_get_current();

	if(doc != NULL)
	{
		gchar	*text;

		if(sci_has_selection(doc->editor->sci))
			text = sci_get_selection_contents(doc->editor->sci);
		else
			text = editor_get_word_at_pos(doc->editor, -1, NULL);

		if(text != NULL)
		{
			guint16	len;

			gtk_entry_set_text(GTK_ENTRY(entry), text);
			len = gtk_entry_get_text_length(GTK_ENTRY(entry));
			gtk_editable_set_position(GTK_EDITABLE(entry), len);
			gtk_editable_select_region(GTK_EDITABLE(entry), 0, len);
			g_free(text);
		}
	}
}

static void cmd_repository_grep(GtkAction *action, gpointer user)
{
	const Repository	*repo;

	CMD_INIT("grep", _("Grep ..."), _("Opens a dialog accepting an expression which is sent to 'git grep', to search the repo's files."), GTK_STOCK_FIND);

	repo = get_repository();
	if(repo != NULL)
	{
		static GtkWidget	*grep_dialog = NULL;
		static GtkWidget	*grep_entry = NULL;
		static GtkWidget	*grep_opt_re = NULL;
		static GtkWidget	*grep_opt_case = NULL;
		static GtkWidget	*grep_opt_invert = NULL;
		static GtkWidget	*grep_opt_word = NULL;
		static GtkWidget	*grep_opt_clear = NULL;
		gchar			tbuf[128];
		const gchar		*name;
		gint			response;

		if(grep_dialog == NULL)
		{
			GtkWidget	*body, *vbox, *hbox, *grid;

			grep_dialog = gtk_dialog_new_with_buttons("", NULL,
					GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					("_Ok"),
					GTK_RESPONSE_ACCEPT,
					_("_Cancel"),
					GTK_RESPONSE_REJECT,
					NULL);
			gtk_dialog_set_default_response(GTK_DIALOG(grep_dialog), GTK_RESPONSE_ACCEPT);
			body = gtk_dialog_get_content_area(GTK_DIALOG(grep_dialog));
			vbox = gtk_vbox_new(FALSE, 0);
			hbox = gtk_hbox_new(FALSE, 0);
			gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Grep for:"), FALSE, FALSE, 0);
			grep_entry = gtk_entry_new();
			gtk_entry_set_activates_default(GTK_ENTRY(grep_entry), TRUE);
			gtk_box_pack_start(GTK_BOX(hbox), grep_entry, TRUE, TRUE, 5);
			gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);
			grid = gtk_grid_new();
			grep_opt_re = gtk_check_button_new_with_mnemonic("_Use regular expressions");
			gtk_grid_attach(GTK_GRID(grid), grep_opt_re, 0, 0, 1, 1);
			grep_opt_case = gtk_check_button_new_with_mnemonic("C_ase sensitive");
			gtk_grid_attach(GTK_GRID(grid), grep_opt_case, 1, 0, 1, 1);
			grep_opt_invert = gtk_check_button_new_with_mnemonic("_Invert");
			gtk_grid_attach(GTK_GRID(grid), grep_opt_invert, 0, 1, 1, 1);
			grep_opt_word = gtk_check_button_new_with_mnemonic("Match only a whole _word");
			gtk_grid_attach(GTK_GRID(grid), grep_opt_word, 1, 1, 1, 1);
			grep_opt_clear = gtk_check_button_new_with_mnemonic("C_lear Messages");
			gtk_grid_attach(GTK_GRID(grid), grep_opt_clear, 0, 2, 1, 1);
			gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 5);
			gtk_box_pack_start(GTK_BOX(body), vbox, FALSE, FALSE, 0);
			gtk_widget_show_all(body);
			gtk_window_set_default_size(GTK_WINDOW(grep_dialog), 384, -1);
		}
		if((name = strrchr(repo->root_path, G_DIR_SEPARATOR)) != NULL)
			name++;
		else
			name = repo->root_path;
		g_snprintf(tbuf, sizeof tbuf, _("Grep in Git Repository \"%s\""), name);
		gtk_window_set_title(GTK_WINDOW(grep_dialog), tbuf);
		grep_get_word(grep_entry);
		gtk_widget_grab_focus(grep_entry);

		response = gtk_dialog_run(GTK_DIALOG(grep_dialog));
		gtk_widget_hide(grep_dialog);
		if(response == GTK_RESPONSE_ACCEPT)
		{
			const gchar	*pattern = gtk_entry_get_text(GTK_ENTRY(grep_entry));
			const gboolean	clear = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grep_opt_clear));
			gchar		*git_grep[16], *git_stdout = NULL;
			gsize		narg = 0;

			git_grep[narg++] = "git";
			git_grep[narg++] = "grep";
			git_grep[narg++] = "--line-number";
			if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grep_opt_re)))
				git_grep[narg++] = "--fixed-strings";
			if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grep_opt_case)))
				git_grep[narg++] = "--ignore-case";
			if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grep_opt_invert)))
				git_grep[narg++] = "--invert-match";
			if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grep_opt_word)))
				git_grep[narg++] = "--word-regexp";
			git_grep[narg++] = (gchar *) pattern;
			git_grep[narg] = NULL;

			if(subprocess_run(repo->root_path, git_grep, NULL, &git_stdout, NULL))
			{
				gchar	*lines = git_stdout, *line, *nextline;
				gsize	hits = 0;

				if(clear)
					msgwin_clear_tab(MSG_MESSAGE);
				msgwin_msg_add(COLOR_BLUE, -1, NULL, _("Searching repository \"%s\" for \"%s\":"), name, pattern);
				msgwin_switch_tab(MSG_MESSAGE, TRUE);
				while((line = tok_tokenize_next(lines, &nextline, '\n')) != NULL)
				{
					/* No GeanyDocument reference; Geany still parses text when clicked and loads the file if necessary. */
					msgwin_msg_add(COLOR_BLUE, -1, NULL,  "%s%s%s", repo->root_path, G_DIR_SEPARATOR_S, line);
					++hits;
					lines = nextline;
				}
				g_free(git_stdout);
				msgwin_msg_add(COLOR_BLUE, -1, NULL, _("Found %lu occurances."), (unsigned long) hits);
			}
		}
	}
}

static void cmd_repository_refresh(GtkAction *action, gpointer user)
{
	GtkTreeIter	iter, child;
	Repository	*repo = NULL;

	CMD_INIT("repository-refresh", _("Refresh"), _("Reloads the list of files contained in the repository"), GTK_STOCK_REFRESH);

	if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.click_path))
	{
		gchar	*path = NULL;

		gtk_tree_model_get(gitbrowser.model, &iter, 1, &path, -1);
		if(path != NULL)
		{
			repo = repository_find_by_path(path);
			if(repo != NULL)
			{
				/* First, clear away all the (top-level) child nodes of the repo, since we're about to re-build them. */
				if(gtk_tree_model_iter_children(gitbrowser.model, &child, &iter))
				{
					while(gtk_tree_store_remove(GTK_TREE_STORE(gitbrowser.model), &child))
						;
					/* Then simply build it again. */
					tree_model_build_repository(gitbrowser.model, &iter, repo->root_path);
				}
			}
			g_free(path);
		}
	}
}

static void cmd_repository_move_up(GtkAction *action, gpointer user)
{
	GtkTreeIter	here;

	CMD_INIT("repository-move-up", _("Move Up"), _("Moves a repository up in the list."), GTK_STOCK_GO_UP);

	if(gtk_tree_model_get_iter(GTK_TREE_MODEL(gitbrowser.model), &here, gitbrowser.click_path))
	{
		GtkTreePath	*path_prev;
		GtkTreeIter	prev;

		path_prev = gtk_tree_path_copy(gitbrowser.click_path);
		if(gtk_tree_path_prev(path_prev))
		{
			if(gtk_tree_model_get_iter(GTK_TREE_MODEL(gitbrowser.model), &prev, path_prev))
				gtk_tree_store_move_before(GTK_TREE_STORE(gitbrowser.model), &here, &prev);
		}
		gtk_tree_path_free(path_prev);
	}
}

static void cmd_repository_move_down(GtkAction *action, gpointer user)
{
	GtkTreeIter	here;

	CMD_INIT("repository-move-down", _("Move Down"), _("Moves a repository down in the list."), GTK_STOCK_GO_DOWN);

	if(gtk_tree_model_get_iter(GTK_TREE_MODEL(gitbrowser.model), &here, gitbrowser.click_path))
	{
		GtkTreePath	*path_next;
		GtkTreeIter	next;

		path_next = gtk_tree_path_copy(gitbrowser.click_path);
		gtk_tree_path_next(path_next);
		if(gtk_tree_model_get_iter(GTK_TREE_MODEL(gitbrowser.model), &next, path_next))
			gtk_tree_store_move_after(GTK_TREE_STORE(gitbrowser.model), &here, &next);
		gtk_tree_path_free(path_next);
	}
}

static void cmd_dir_expand(GtkAction *action, gpointer user)
{
	CMD_INIT("dir-expand", _("Expand All"), _("Expands a directory node."), GTK_STOCK_GO_FORWARD);

	gtk_tree_view_expand_row(GTK_TREE_VIEW(gitbrowser.view), gitbrowser.click_path, TRUE);
}

static void cmd_dir_collapse(GtkAction *action, gpointer user)
{
	CMD_INIT("dir-collapse", _("Collapse All"), _("Collapses a directory node."), GTK_STOCK_GO_BACK);

	gtk_tree_view_collapse_row(GTK_TREE_VIEW(gitbrowser.view), gitbrowser.click_path);
}

static void cmd_dir_explore(GtkAction *action, gpointer user)
{
	GtkTreeIter	iter;

	CMD_INIT("file-explore", _("Explore ..."), _("Opens the directory containing this item, using the system's default file browser."), GTK_STOCK_DIRECTORY);

	if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.click_path))
	{
		char	buf[1024] = "file://";

		tree_model_get_document_path(gitbrowser.model, &iter, buf + 7, (sizeof buf) - 7);
		if(buf[0] != '\0')
		{
			msgwin_msg_add(COLOR_BLACK, -1, NULL, " showing '%s'", buf);
			gtk_show_uri(NULL, buf, GDK_CURRENT_TIME, NULL);
		}
	}
}

static void cmd_dir_terminal(GtkAction *action, gpointer user)
{
	GtkTreeIter	iter;
	gchar		*argv[2] = { NULL, NULL };

	CMD_INIT("dir-terminal", _("Terminal ..."), _("Opens a terminal (command line) window here."), NULL);

	/* If the configured terminal command starts with a dollar, treat it as an environment variable reference. */
	argv[0] = gitbrowser.terminal_cmd;
	if(argv[0] != NULL && argv[0][0] == '$')
	{
		if((argv[0] = (gchar *) g_getenv(argv[0] + 1)) == NULL)
		{
			msgwin_msg_add(COLOR_BLACK, -1, NULL, "Gitbrowser: Failed to look up terminal command from %s", gitbrowser.terminal_cmd);
			return;
		}
	}

	if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.click_path))
	{
		char	buf[1024];

		tree_model_get_document_path(gitbrowser.model, &iter, buf, sizeof buf);
		if(buf[0] != '\0' && argv[0] != NULL && argv[0][0] != '\0')
			g_spawn_async(buf, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
	}
}

static void cmd_file_open(GtkAction *action, gpointer user)
{
	CMD_INIT("file-open", _("Open"), _("Opens a file as a new document, or focuses the document if already opened."), GTK_STOCK_OPEN);

	tree_model_open_document(gitbrowser.model, gitbrowser.click_path);
}

static void cmd_file_copy_name(GtkAction *action, gpointer user)
{
	GtkTreeIter	iter;

	CMD_INIT("file-copy-name", _("Copy Name"), _("Copies the full name (with path) of this file to the clipboard."), NULL);

	if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.click_path))
	{
		char		buf[1024];
		GtkClipboard	*cb;

		tree_model_get_document_path(gitbrowser.model, &iter, buf, sizeof buf);
		if((cb = gtk_clipboard_get(GDK_SELECTION_PRIMARY)) != NULL)
		{
			gtk_clipboard_set_text(cb, buf, -1);
		}
	}
}

void init_commands(GtkAction **actions, GtkWidget **menu_items)
{
	typedef void (*ActivateOrCreate)(GtkAction *action, gpointer user);
	const ActivateOrCreate funcs[] = {
		cmd_repository_add,
		cmd_repository_add_multiple,
		cmd_repository_add_from_document,
		cmd_repository_add_separator,
		cmd_repository_remove,
		cmd_repository_remove_all,
		cmd_repository_open_quick,
		cmd_repository_open_quick_from_document,
		cmd_repository_grep,
		cmd_repository_refresh,
		cmd_repository_move_up,
		cmd_repository_move_down,
		cmd_dir_expand,
		cmd_dir_collapse,
		cmd_dir_explore,
		cmd_dir_terminal,
		cmd_file_open,
		cmd_file_copy_name,
	};
	size_t	i;

	for(i = 0; i < sizeof funcs / sizeof *funcs; i++)
	{
		funcs[i](NULL, &actions[i]);
		g_signal_connect(G_OBJECT(actions[i]), "activate", G_CALLBACK(funcs[i]), NULL);
		menu_items[i] = gtk_action_create_menu_item(actions[i]);
		gtk_widget_show(menu_items[i]);
	}
}

/* -------------------------------------------------------------------------------------------------------------- */

/* Split the given multi-line string into individual lines, copying and returning each one.
 * Too long lines will be silently truncated, but properly skipped.
 * Returns pointer to start of next line, or NULL when no more lines are found.
*/
const gchar * tok_tokenize_next_line(const gchar *lines, gchar *buffer, size_t buf_size)
{
	if(lines == NULL || *lines == '\0' || buffer == NULL || buf_size < 2)
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

Repository * repository_new(const gchar *root_path)
{
	Repository	*r = g_malloc(sizeof *r);

	g_strlcpy(r->root_path, root_path, sizeof r->root_path);

	r->quick_open.dialog = NULL;
	r->quick_open.filter = NULL;
	r->quick_open.sort = NULL;
	r->quick_open.selection = NULL;
	r->quick_open.files_total = 0;
	r->quick_open.files_filtered = 0;
	r->quick_open.store = NULL;
	r->quick_open.names = NULL;
	r->quick_open.view = NULL;
	r->quick_open.filter_text[0] = '\0';
	r->quick_open.filter_lower[0] = '\0';
	r->quick_open.filter_idle = 0;

	g_hash_table_insert(gitbrowser.repositories, r->root_path, r);

	return r;
}

/* Returns the repository to which the given path belongs, or NULL if the path is not part of a repository. */
Repository * repository_find_by_path(const gchar *path)
{
	GHashTableIter	iter;
	gpointer	key, value;

	if(path == NULL)
		return NULL;

	g_hash_table_iter_init(&iter, gitbrowser.repositories);
	while(g_hash_table_iter_next(&iter, &key, &value))
	{
		Repository	*repo = value;

		if(strstr(path, repo->root_path) == path)
			return repo;
	}
	return NULL;
}

static guint string_store(QuickOpenInfo *qoi, const gchar *text)
{
	gpointer	offset = g_hash_table_lookup(qoi->dedup, text);

	/* The first name is stored starting at 1, which makes NULL really represent "unknown name". */
	if(offset == NULL)
	{
		offset = GUINT_TO_POINTER(qoi->names->len + 1);

		g_string_append_c(qoi->names, '\0');	/* Terminate previous name. */
		g_string_append(qoi->names, text);
		/* Remember the offset at which this name starts, for de-duplicating. */
		g_hash_table_insert(qoi->dedup, (gpointer) text, offset);
	}
	return GPOINTER_TO_UINT(offset);
}

static void recurse_repository_to_list(GtkTreeModel *model, GtkTreeIter *iter, gchar *path, gsize path_length, QuickOpenInfo *qoi)
{
	gchar		*dname, *fname, *get, *put;
	const gsize	old_length = path_length;
	GtkTreeIter	child;
	
	/* Loop over all nodes at this level. */
	do
	{
		gtk_tree_model_get(model, iter, 0, &dname, 1, &fname, -1);
		/* Append the local filename to the path. Ignore buffer overflow, for now. */
		for(get = fname, put = path + path_length; *get != '\0'; get++, put++, path_length++)
			*put = *get;
		*put = '\0';

		/* Do we need to recurse, here? */
		if(gtk_tree_model_iter_children(model, &child, iter))
		{
			/* Yes: time to iterate children, so append a separator. */
			strcpy(put, G_DIR_SEPARATOR_S);
			path_length += strlen(G_DIR_SEPARATOR_S);

			recurse_repository_to_list(model, &child, path, path_length, qoi);
		}
		else
		{
			if(gitbrowser.quick_open_hide == NULL || !g_regex_match(gitbrowser.quick_open_hide, dname, 0, NULL))
			{
				gchar		*dname_lower, *dpath, *slash;
				QuickOpenRow	row;

				/* Append name and path to the big string buffer, putting "naked" offsets in the pointers. */
				row.name = GSIZE_TO_POINTER(string_store(qoi, dname));
				/* Convert to lower-case, and store that version too, for filtering. */
				dname_lower = g_utf8_strdown(dname, -1);
				row.name_lower = GSIZE_TO_POINTER(string_store(qoi, dname_lower));
				/* Remove the last component, which is dname itself, and we don't want it in the Location column. */
				dpath = g_filename_display_name(path);
				if((slash = g_utf8_strrchr(dpath, -1, G_DIR_SEPARATOR)) != NULL)
					*slash = '\0';
				row.path = GSIZE_TO_POINTER(string_store(qoi, dpath));
				g_free(dpath);
				g_array_append_val(qoi->array, row);
				qoi->files_total++;
			}
		}
		/* Undo our modifications to the global path. */
		path[old_length] = '\0';
		path_length = old_length;
	} while(gtk_tree_model_iter_next(model, iter));
}

static void repository_to_list(const Repository *repo, GtkTreeModel *model, QuickOpenInfo *qoi)
{
	GtkTreeIter	root, iter;
	gboolean	found = FALSE;
	gchar		buf[2048];
	gsize		len;

	if(!gtk_tree_model_get_iter_first(model, &root) || !gtk_tree_model_iter_children(model, &iter, &root))
		return;
	/* Walk the toplevel nodes, which should be very very few, looking for the given repository.
	 * This seems a bit clumsy, but it's a trade-off between i.e. maintaining an iter to the repo
	 * at all times (which is annoying when repos are added/moved/deleted) and this. This won.
	*/
	do
	{
		gchar	*path;

		gtk_tree_model_get(model, &iter, 1, &path, -1);
		if(path != NULL)	/* Separators have a NULL root path. That's what they are. */
			found = strcmp(path, repo->root_path) == 0;
		g_free(path);
	} while(!found && gtk_tree_model_iter_next(model, &iter));
	if(!found)
		return;
	/* Now 'iter' is the root of the repository we want to linearize. */
	root = iter;
	if(!gtk_tree_model_iter_children(model, &iter, &root))
		return;
	/* Now 'iter' is finally pointing at the repository's first file. */
	len = g_snprintf(buf, sizeof buf, "%s%s", repo->root_path, G_DIR_SEPARATOR_S);
	if(len < sizeof buf)
	{
		GTimer		*tmr = g_timer_new();
		gsize		i;
		LDState	lstate;

		/* Be prepared for being re-run on the same repository, so clear data first. */
		qoi->files_total = qoi->files_filtered = 0;
		g_string_truncate(qoi->names, 0);
		gtk_list_store_clear(qoi->store);
		qoi->dedup = g_hash_table_new(g_str_hash, g_str_equal);
		qoi->array = g_array_new(FALSE, FALSE, sizeof (QuickOpenRow));
		recurse_repository_to_list(model, &iter, buf, len, qoi);
		levenshtein_begin_half(&lstate, qoi->filter_text);
		/* Now we need to fixup; convert stored offsets into actual absolute memory addresses. */
		for(i = 0; i < qoi->files_total; i++)
		{
			QuickOpenRow	*row = &g_array_index(qoi->array, QuickOpenRow, i);

			row->name = qoi->names->str + GPOINTER_TO_SIZE(row->name);
			row->name_lower = qoi->names->str + GPOINTER_TO_SIZE(row->name_lower);
			row->path = qoi->names->str + GPOINTER_TO_SIZE(row->path);
			row->distance = levenshtein_compute_half(&lstate, row->name);
		}
		levenshtein_end(&lstate);
		/* Finally, use the array to populate the list store. */
		for(i = 0; i < qoi->files_total; i++)
		{
			const QuickOpenRow	*row = &g_array_index(qoi->array, QuickOpenRow, i);
			gtk_list_store_insert_with_values(qoi->store, &iter, INT_MAX, QO_NAME, row->name, QO_NAME_LOWER, row->name_lower, QO_PATH, row->path, QO_VISIBLE, TRUE, -1);
		}
		/* We no longer need the array, so throw it away. */
		g_array_free(qoi->array, TRUE);
		g_hash_table_destroy(qoi->dedup);
		g_timer_destroy(tmr);
	}
}

void repository_save_all(GtkTreeModel *model)
{
	GtkTreeIter	root, iter;
	GString		*repos = NULL;
	GKeyFile	*out;
	gchar		*data;

	if(gtk_tree_model_get_iter_first(model, &root) && gtk_tree_model_iter_children(model, &iter, &root))
	{
		repos = g_string_new("");
		do
		{
			gchar	*dpath, *fn;

			gtk_tree_model_get(model, &iter, 1, &dpath, -1);
			if(dpath == NULL)
			{
				if(repos->len > 0)
					g_string_append_c(repos, PATH_SEPARATOR_CHAR);
				g_string_append(repos, REPO_IS_SEPARATOR);
			}
			else
			{
				if((fn = g_filename_from_utf8(dpath, -1, NULL, NULL, NULL)) != NULL)
				{
					if(repos->len > 0)
						g_string_append_c(repos, PATH_SEPARATOR_CHAR);
					g_string_append(repos, fn);
					g_free(fn);
				}
			}
			g_free(dpath);
		} while(gtk_tree_model_iter_next(model, &iter));
	}

	out = g_key_file_new();

	if(repos != NULL)
	{
		GString	*exp = tree_view_get_expanded(GTK_TREE_VIEW(gitbrowser.view));

		g_key_file_set_string(out, MNEMONIC_NAME, CFG_REPOSITORIES, repos->str);
		g_string_free(repos, TRUE);
		if(exp != NULL)
		{
			if(exp->len > 0)
				g_key_file_set_string(out, MNEMONIC_NAME, CFG_EXPANDED, exp->str);
			g_string_free(exp, TRUE);
		}
	}
	stash_group_save_to_key_file(gitbrowser.prefs, out);

	if((data = g_key_file_to_data(out, NULL, NULL)) != NULL)
	{
		utils_write_file(gitbrowser.config_filename, data);
		g_free(data);
	}
	g_key_file_free(out);
}

void repository_load_all(void)
{
	GKeyFile	*in;

	in = g_key_file_new();
	if(g_key_file_load_from_file(in, gitbrowser.config_filename, G_KEY_FILE_NONE, NULL))
	{
		gchar	*str;

		if((str = g_key_file_get_string(in, MNEMONIC_NAME, CFG_REPOSITORIES, NULL)) != NULL)
		{
			gchar	separator[] = { PATH_SEPARATOR_CHAR, '\0' };
			gchar	**repo_vector = g_strsplit(str, separator, 0);
			gsize	i;
			gchar	*exp;

			for(i = 0; repo_vector[i] != NULL; i++)
			{
				if(strcmp(repo_vector[i], REPO_IS_SEPARATOR) == 0)
					tree_model_build_separator(gitbrowser.model);
				else
				{
					Repository	*repo = repository_new(repo_vector[i]);
					tree_model_build_repository(gitbrowser.model, NULL, repo->root_path);
				}
			}
			g_free(str);
			exp = g_key_file_get_string(in, MNEMONIC_NAME, CFG_EXPANDED, NULL);
			/* Note: Both of these calls do the right thing even if exp == NULL. */
			tree_view_set_expanded(GTK_TREE_VIEW(gitbrowser.view), exp);
			g_free(exp);
		}
	}
	stash_group_load_from_key_file(gitbrowser.prefs, in);
	open_quick_reset_filter();

	g_key_file_free(in);
}

static void evt_open_quick_selection_changed(GtkTreeSelection *sel, gpointer user)
{
	QuickOpenInfo	*qoi = user;

	gtk_dialog_set_response_sensitive(GTK_DIALOG(qoi->dialog), GTK_RESPONSE_OK, gtk_tree_selection_count_selected_rows(sel) > 0);
}

static void evt_open_quick_view_row_activated(GtkWidget *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user)
{
	QuickOpenInfo	*qoi = user;

	gtk_dialog_response(GTK_DIALOG(qoi->dialog), GTK_RESPONSE_OK);
}

static void open_quick_update_label(QuickOpenInfo *qoi)
{
	gchar	buf[64];

	if(qoi->files_filtered == 0)
		g_snprintf(buf, sizeof buf, _("Showing all %lu files."), qoi->files_total);
	else
		g_snprintf(buf, sizeof buf, _("Showing %lu/%lu files."), qoi->files_total - qoi->files_filtered, qoi->files_total);
	gtk_label_set_text(GTK_LABEL(qoi->label), buf);
}

static gboolean cb_open_quick_filter_idle(gpointer user)
{
	QuickOpenInfo	*qoi = user;
	guint		i;
	GtkTreeIter	iter;
	GtkTreePath	*first;
	gboolean	valid = TRUE;
	GTimer		*tmr;
	const gdouble	max_time = 1e-3 * gitbrowser.quick_open_filter_max_time;

	tmr = g_timer_new();
	for(i = 0; valid && g_timer_elapsed(tmr, NULL) < max_time; i++)
	{
		gchar		*name, *name_lower, path[16];
		gboolean	old_visible, new_visible;

		g_snprintf(path, sizeof path, "%u", qoi->filter_row);
		valid = gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(qoi->store), &iter, path);
		if(!valid)
			break;
		gtk_tree_model_get(GTK_TREE_MODEL(qoi->store), &iter, QO_NAME, &name, QO_NAME_LOWER, &name_lower, QO_VISIBLE, &old_visible, -1);
		new_visible = strstr(name_lower, qoi->filter_lower) != NULL;
		if(new_visible != old_visible)
			gtk_list_store_set(qoi->store, &iter, QO_VISIBLE, new_visible, -1);
		if(!new_visible)
			qoi->files_filtered++;
		else
		{
			const gint16 dist = levenshtein_compute_half(&qoi->filter_ld, name);
			gtk_list_store_set(qoi->store, &iter, QO_DISTANCE, dist, -1);
			printf("updated distance to '%s' to %d\n", name, dist);
		}
		++qoi->filter_row;
	}
	g_timer_destroy(tmr);
	open_quick_update_label(qoi);
	if(!valid)
	{
		/* Done! */
		first = gtk_tree_path_new_first();
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(qoi->view), first, NULL, FALSE);
		gtk_tree_path_free(first);
		qoi->filter_idle = 0;
		levenshtein_end(&qoi->filter_ld);
		gtk_spinner_stop(GTK_SPINNER(qoi->spinner));
		gtk_widget_hide(qoi->spinner);
		return FALSE;
	}
	return TRUE;
}

static void evt_open_quick_entry_changed(GtkWidget *wid, gpointer user)
{
	QuickOpenInfo	*qoi = user;
	const gchar	*filter = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(wid)));
	gchar		*filter_lower;

	/* Extract search string, convert to lower-case for filtering. */
	g_strlcpy(qoi->filter_text, filter, sizeof qoi->filter_text);
	filter_lower = g_utf8_strdown(filter, -1);
	g_strlcpy(qoi->filter_lower, filter_lower, sizeof qoi->filter_lower);
	g_free(filter_lower);

	printf("set Levenshtein reference to '%s'\n", qoi->filter_text);
	levenshtein_begin_half(&qoi->filter_ld, qoi->filter_text);
	qoi->files_filtered = 0;
	gtk_spinner_start(GTK_SPINNER(qoi->spinner));
	gtk_widget_show(qoi->spinner);

	gtk_entry_set_icon_sensitive(GTK_ENTRY(wid), GTK_ENTRY_ICON_SECONDARY, qoi->filter_text[0] != '\0');

	qoi->filter_row = 0;
	if(qoi->filter_idle == 0)
	{
		qoi->filter_idle = g_idle_add(cb_open_quick_filter_idle, qoi);
	}
}

static void evt_open_quick_entry_icon_release(GtkWidget *wid, GtkEntryIconPosition position, GdkEvent *evt, gpointer user)
{
	gtk_entry_set_text(GTK_ENTRY(wid), "");	/* There's only one icon, so no need to figure out which was clicked. */
}

static gboolean evt_open_quick_entry_key_press(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	QuickOpenInfo	*qoi = user;

	if(evt->type == GDK_KEY_PRESS)
	{
		/* This is, unfortunately, something of a hack: to make cursor up/down keys
		 * in the entry affect the selection in the tree view, we copy the event,
		 * poke the window member, and re-emit it. The best thing is that it works.
		 *
		 * FIXME: There should be a better way of implementing this. Find it.
		*/
		if(evt->keyval == GDK_KEY_Up || evt->keyval == GDK_KEY_Down ||
		   evt->keyval == GDK_KEY_Page_Up || evt->keyval == GDK_KEY_Page_Down)
		{
			GdkEventKey	copy = *evt;

			copy.window = gtk_widget_get_window(qoi->view);
			copy.send_event = TRUE;
			gtk_widget_grab_focus(qoi->view);
			gtk_main_do_event((GdkEvent *) &copy);
			gtk_widget_grab_focus(wid);

			return TRUE;
		}
	}
	return FALSE;
}

static void cdf_open_quick_filename(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	*filename;

	gtk_tree_model_get(model, iter, QO_NAME, &filename, -1);
	g_object_set(G_OBJECT(cell), "text", filename, NULL);
}

static void cdf_open_quick_location(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	*location;

	gtk_tree_model_get(model, iter, QO_PATH, &location, -1);
	g_object_set(G_OBJECT(cell), "text", location, NULL);
}

static gint cb_open_quick_sort(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	guint		da, db;
	gint		ret;
	const gchar	*na, *nb;

	gtk_tree_model_get(model, a, QO_DISTANCE, &da, -1);
	gtk_tree_model_get(model, b, QO_DISTANCE, &db, -1);

	if(da < db)
		return -1;
	if(da > db)
		return 1;

	gtk_tree_model_get(model, a, QO_NAME_LOWER, &na, -1);
	gtk_tree_model_get(model, b, QO_NAME_LOWER, &nb, -1);
	ret = g_utf8_collate(na, nb);
	/* Note: QO_NAME_LOWER is G_TYPE_POINTER, not string so: no g_free()! */

	return ret;
}

void repository_open_quick(Repository *repo)
{
	QuickOpenInfo	*qoi;

	if(!repo)
	{
		msgwin_status_add(_("Current document is not part of a known repository. Use Add to add a repository."));
		return;
	}
	qoi = &repo->quick_open;

	if(qoi->dialog == NULL)
	{
		GtkWidget		*vbox, *label, *scwin, *title, *hbox, *aa;
		GtkCellRenderer         *cr;
		GtkTreeViewColumn       *vc;
		gchar			tbuf[64], *name;

		qoi->store = gtk_list_store_new(QO_NUM_COLUMNS, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_BOOLEAN, G_TYPE_UINT);
		qoi->names = g_string_sized_new(32 << 10);
		repository_to_list(repo, gitbrowser.model, qoi);

		if((name = strrchr(repo->root_path, G_DIR_SEPARATOR)) != NULL)
			name++;
		else
			name = repo->root_path;
		g_snprintf(tbuf, sizeof tbuf, _("Quick Open in Git Repository \"%s\""), name);

		qoi->dialog = gtk_dialog_new_with_buttons(tbuf, NULL, GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
		aa = gtk_dialog_get_action_area(GTK_DIALOG(qoi->dialog));
		gtk_dialog_set_default_response(GTK_DIALOG(qoi->dialog), GTK_RESPONSE_OK);
		gtk_window_set_default_size(GTK_WINDOW(qoi->dialog), 600, 600);

		/* Pack some custom stuff into the action area, but first into a hbox for tidyness. */
		hbox = gtk_hbox_new(FALSE, 0);
		qoi->spinner = gtk_spinner_new();
		gtk_box_pack_start(GTK_BOX(hbox), qoi->spinner, FALSE, FALSE, 0);
		qoi->label = gtk_label_new("");
		gtk_box_pack_start(GTK_BOX(hbox), qoi->label, TRUE, TRUE, 0);
		open_quick_update_label(qoi);
		gtk_box_pack_start(GTK_BOX(aa), hbox, TRUE, TRUE, 0);
		gtk_box_reorder_child(GTK_BOX(aa), hbox, 0);
		gtk_widget_show_all(hbox);
		gtk_widget_hide(qoi->spinner);

		vbox = ui_dialog_vbox_new(GTK_DIALOG(qoi->dialog));
		label = gtk_label_new(_("Select one or more document(s) to open. Type to filter filenames."));
		gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
		qoi->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(qoi->store), NULL);
		gtk_tree_model_filter_set_visible_column(GTK_TREE_MODEL_FILTER(qoi->filter), QO_VISIBLE);	/* Filter on the boolean column. */
		qoi->sort = gtk_tree_model_sort_new_with_model(qoi->filter);
		gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(qoi->sort), cb_open_quick_sort, qoi, NULL);
		qoi->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(qoi->sort));

		vc = gtk_tree_view_column_new();
		cr = gtk_cell_renderer_text_new();
		title = gtk_label_new(_("Filename"));
		gtk_widget_show(title);
		gtk_tree_view_column_set_widget(vc, title);
		gtk_tree_view_append_column(GTK_TREE_VIEW(qoi->view), vc);
		gtk_tree_view_column_pack_start(vc, cr, TRUE);
		gtk_tree_view_column_set_cell_data_func(vc, cr, cdf_open_quick_filename, qoi, NULL);

		vc = gtk_tree_view_column_new();
		cr = gtk_cell_renderer_text_new();
		title = gtk_label_new(_("Location"));
		gtk_widget_show(title);
		gtk_tree_view_column_set_widget(vc, title);
		gtk_tree_view_append_column(GTK_TREE_VIEW(qoi->view), vc);
		gtk_tree_view_column_pack_start(vc, cr, TRUE);
		gtk_tree_view_column_set_cell_data_func(vc, cr, cdf_open_quick_location, qoi, NULL);
		gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(qoi->view), FALSE);

		scwin = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		g_signal_connect(G_OBJECT(qoi->view), "row_activated", G_CALLBACK(evt_open_quick_view_row_activated), qoi);
		gtk_container_add(GTK_CONTAINER(scwin), qoi->view);
		gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);
		qoi->entry = gtk_entry_new();
		gtk_entry_set_activates_default(GTK_ENTRY(qoi->entry), TRUE);
		gtk_entry_set_icon_from_icon_name(GTK_ENTRY(qoi->entry), GTK_ENTRY_ICON_SECONDARY, "edit-clear");
		gtk_entry_set_icon_sensitive(GTK_ENTRY(qoi->entry), GTK_ENTRY_ICON_SECONDARY, FALSE);
		g_signal_connect(G_OBJECT(qoi->entry), "changed", G_CALLBACK(evt_open_quick_entry_changed), qoi);
		g_signal_connect(G_OBJECT(qoi->entry), "key-press-event", G_CALLBACK(evt_open_quick_entry_key_press), qoi);
		g_signal_connect(G_OBJECT(qoi->entry), "icon-release", G_CALLBACK(evt_open_quick_entry_icon_release), qoi);
		gtk_box_pack_start(GTK_BOX(vbox), qoi->entry, FALSE, FALSE, 0);

		gtk_dialog_set_response_sensitive(GTK_DIALOG(qoi->dialog), GTK_RESPONSE_OK, FALSE);

		gtk_widget_show_all(vbox);

		qoi->selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(qoi->view));
		gtk_tree_selection_set_mode(qoi->selection, GTK_SELECTION_MULTIPLE);
		g_signal_connect(G_OBJECT(qoi->selection), "changed", G_CALLBACK(evt_open_quick_selection_changed), qoi);
	}
	gtk_editable_select_region(GTK_EDITABLE(qoi->entry), 0, -1);
	gtk_widget_grab_focus(qoi->entry);
	if(gtk_dialog_run(GTK_DIALOG(qoi->dialog)) == GTK_RESPONSE_OK)
	{
		GList	*selection = gtk_tree_selection_get_selected_rows(qoi->selection, NULL), *iter;

		for(iter = selection; iter != NULL; iter = g_list_next(iter))
		{
			GtkTreePath	*path_filter;

			if((path_filter = gtk_tree_model_sort_convert_path_to_child_path(GTK_TREE_MODEL_SORT(qoi->sort), iter->data)) != NULL)
			{
				GtkTreePath	*path_root;

				if((path_root = gtk_tree_model_filter_convert_path_to_child_path(GTK_TREE_MODEL_FILTER(qoi->filter), path_filter)) != NULL)
				{
					GtkTreeIter	here;

					if(gtk_tree_model_get_iter(GTK_TREE_MODEL(qoi->store), &here, path_root))
					{
						gchar	buf[2048], *dpath, *dname, *fn;
						gint	len;

						gtk_tree_model_get(GTK_TREE_MODEL(qoi->store), &here, QO_NAME, &dname, QO_PATH, &dpath, -1);
						if((len = g_snprintf(buf, sizeof buf, "%s%s%s", dpath, G_DIR_SEPARATOR_S, dname)) < sizeof buf)
						{
							if((fn = g_filename_from_utf8(buf, (gssize) len, NULL, NULL, NULL)) != NULL)
							{
								document_open_file(buf, FALSE, NULL, NULL);
								g_free(fn);
							}
						}
					}
					gtk_tree_path_free(path_root);
				}
				gtk_tree_path_free(path_filter);
			}
		}
		g_list_foreach(selection, (GFunc) gtk_tree_path_free, NULL);
		g_list_free(selection);
	}
	gtk_widget_hide(qoi->dialog);
}

/* -------------------------------------------------------------------------------------------------------------- */

GtkTreeModel * tree_model_new(void)
{
	GtkTreeStore	*ts;
	GtkTreeIter	iter;

	/* First column is display text, second is corresponding path (or path part). All are NULL for separators. */
	ts = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_store_append(ts, &iter, NULL);
	gtk_tree_store_set(ts, &iter, 0, _("Repositories (Right-click to add)"), 1, NULL, -1);

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

static guint	tree_model_build_populate(GtkTreeModel *model, gchar *lines, GtkTreeIter *parent);
static guint	tree_model_build_traverse(GtkTreeModel *model, GNode *root, GtkTreeIter *parent);

/* Run "git branch" to figure out which branch <root_path> is on. */
static gboolean get_branch(gchar *branch, gsize branch_max, const gchar *root_path)
{
	gchar		*git_branch[] = { "git", "branch", "--no-color", NULL }, *git_stdout = NULL, *git_stderr = NULL;
	gboolean	ret = FALSE;

	if(subprocess_run(root_path, git_branch, NULL, &git_stdout, &git_stderr))
	{
		gchar	*lines = git_stdout, *line, *nextline;

		while((line = tok_tokenize_next(lines, &nextline, '\n')) != NULL)
		{
			if(line[0] == '*')
			{
				const gchar	*ptr = line + 1;

				while(isspace((unsigned int) *ptr))
					ptr++;

				ret = g_snprintf(branch, branch_max, "%s", ptr) < branch_max;
				break;
			}
			lines = nextline;
		}
		g_free(git_stdout);
		g_free(git_stderr);
	}
	return ret;
}

void tree_model_build_repository(GtkTreeModel *model, GtkTreeIter *repo, const gchar *root_path)
{
	GtkTreeIter	new;
	const gchar	*slash;
	gchar		*git_ls_files[] = { "git", "ls-files", NULL }, *git_stdout = NULL, *git_stderr = NULL;
	gchar		branch[256];
	GTimer		*timer;

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
	if(get_branch(branch, sizeof branch, root_path))
	{
		gchar	disp[1024];

		g_snprintf(disp, sizeof disp, "%s [%s]", slash, branch);
		gtk_tree_store_set(GTK_TREE_STORE(model), repo,  0, disp,  1, root_path,  -1);
	}
	else
		gtk_tree_store_set(GTK_TREE_STORE(model), repo,  0, slash,  1, root_path,  -1);

	/* Now list the repository, and build a tree representation. Easy-peasy, right? */
	timer = g_timer_new();
	if(subprocess_run(root_path, git_ls_files, NULL, &git_stdout, &git_stderr))
	{
		GtkTreePath	*path;
		const guint	counter = tree_model_build_populate(model, git_stdout, repo);

		g_free(git_stdout);
		g_free(git_stderr);

		path = gtk_tree_model_get_path(model, repo);
		gtk_tree_view_expand_to_path(GTK_TREE_VIEW(gitbrowser.view), path);
		gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(gitbrowser.view), path, NULL, NULL, FALSE);
		gtk_tree_path_free(path);
		msgwin_status_add(_("Built repository \"%s\"; %lu files added in %.1f ms."), slash, (unsigned long) counter, 1e3 * g_timer_elapsed(timer, NULL));
	}
	g_timer_destroy(timer);
}

void tree_model_build_separator(GtkTreeModel *model)
{
	GtkTreeIter	root, sep;

	if(gtk_tree_model_get_iter_first(model, &root))
	{
		gtk_tree_store_append(GTK_TREE_STORE(model), &sep, &root);
		gtk_tree_store_set(GTK_TREE_STORE(model), &sep, 0, NULL,  1, NULL,  -1);
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

static guint tree_model_build_populate(GtkTreeModel *model, gchar *lines, GtkTreeIter *parent)
{
	gchar	*line, *nextline, *dir, *endptr;
	GNode	*root = g_node_new(""), *prev;
	guint	count;

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
	count = tree_model_build_traverse(model, root, parent);
	g_node_destroy(root);
	return count;
}

/* Traverse the children of the given GNode tree, and build a corresponding GtkTreeModel.
 * The traversal order is special: inner nodes first, to group directories on top.
*/
static guint tree_model_build_traverse(GtkTreeModel *model, GNode *root, GtkTreeIter *parent)
{
	GNode		*child;
	GtkTreeIter	iter;
	gchar		*dname;
	guint		count = 0;

	/* Inner nodes. */
	for(child = g_node_first_child(root); child != NULL; child = g_node_next_sibling(child))
	{
		if(g_node_first_child(child) == NULL)
			continue;
		/* We now know this is an inner node; add tree node and recurse. */
		gtk_tree_store_append(GTK_TREE_STORE(model), &iter, parent);
		dname = g_filename_display_name(child->data);
		gtk_tree_store_set(GTK_TREE_STORE(model), &iter, 0, dname, 1, child->data, -1);
		g_free(dname);
		count += tree_model_build_traverse(model, child, &iter);	/* Don't count inner node itself. */
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
		count += 1;
	}
	return count;
}

gboolean tree_model_open_document(GtkTreeModel *model, GtkTreePath *path)
{
	GtkTreeIter	iter, child;

	if(gtk_tree_model_get_iter(model, &iter, path))
	{
		GString	*path = g_string_sized_new(1024);
		gchar	*component;

		/* Walk towards the root, building the filename as we go. */
		do
		{
			gtk_tree_model_get(model, &iter, 1, &component, -1);
			if(component != NULL)
			{
				if(path->len > 0)
					g_string_prepend(path, G_DIR_SEPARATOR_S);
				g_string_prepend(path, component);
				g_free(component);
			}
			child = iter;
		} while(gtk_tree_model_iter_parent(model, &iter, &child));
		document_open_file(path->str, FALSE, NULL, NULL);
		g_string_free(path, TRUE);
		return TRUE;
	}
	return FALSE;
}

/* Gets the full path, in the local system's encoding, for the indicated document. Returns FALSE if given an inner node. */
gboolean tree_model_get_document_path(GtkTreeModel *model, const GtkTreeIter *iter, gchar *buf, gsize buf_max)
{
	const gboolean	inner = gtk_tree_model_iter_has_child(model, (GtkTreeIter *) iter);
	GString		*path = g_string_sized_new(1024);
	GtkTreeIter	here = *iter, child;

	/* Walk towards the root, building the filename as we go. */
	do
	{
		gchar	*component = NULL;

		gtk_tree_model_get(model, &here, 1, &component, -1);
		if(component != NULL)
		{
			if(path->len > 0)
				g_string_prepend(path, G_DIR_SEPARATOR_S);
			g_string_prepend(path, component);
			g_free(component);
		}
		child = here;
	} while(gtk_tree_model_iter_parent(model, &here, &child));

	if(g_strlcpy(buf, path->str, buf_max) >= buf_max)
		*buf = '\0';
	g_string_free(path, TRUE);

	return inner;
}

void tree_model_foreach(GtkTreeModel *model, GtkTreeIter *root, void (*node_callback)(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user), gpointer user)
{
	do
	{
		GtkTreeIter	child;

		if(gtk_tree_model_iter_children(model, &child, root))
		{
			tree_model_foreach(model, &child, node_callback, user);
		}
		else
		{
			GtkTreePath	*here;

			here = gtk_tree_model_get_path(model, root);
			node_callback(model, here, root, user);
			gtk_tree_path_free(here);
		}
	} while(gtk_tree_model_iter_next(model, root));
}

static gboolean evt_menu_selection_done(GtkWidget *wid, gpointer user)
{
	if(gitbrowser.click_path != NULL)
	{
		gtk_tree_path_free(gitbrowser.click_path);
		gitbrowser.click_path = NULL;
	}
	return FALSE;
}

/* On deactivation, remove all widgets that are not separators, so they aren't destroyed. */
static void evt_menu_deactivate(GtkWidget *wid, gpointer user)
{
	GList	*children = gtk_container_get_children(GTK_CONTAINER(wid)), *iter;

	for(iter = children; iter != NULL; iter = g_list_next(iter))
	{
		if(G_OBJECT_TYPE(G_OBJECT(iter->data)) != GTK_TYPE_SEPARATOR_MENU_ITEM)
			gtk_container_remove(GTK_CONTAINER(wid), iter->data);
	}
	g_list_free(children);
}

/* Creates a new popup menu suitable for use in our tree, and connects the selection-done signal. */
static GtkWidget * menu_popup_create(void)
{
	GtkWidget	*menu;

	menu = gtk_menu_new();
	g_signal_connect(G_OBJECT(menu), "selection_done", G_CALLBACK(evt_menu_selection_done), NULL);
	g_signal_connect(G_OBJECT(menu), "deactivate", G_CALLBACK(evt_menu_deactivate), NULL);

	return menu;
}

static void menu_popup_repositories(GdkEventButton *evt)
{
	gitbrowser.main_menu = menu_popup_create();
	gtk_menu_shell_append(GTK_MENU_SHELL(gitbrowser.main_menu), gitbrowser.action_menu_items[CMD_REPOSITORY_OPEN_QUICK_FROM_DOCUMENT]);
	gtk_menu_shell_append(GTK_MENU_SHELL(gitbrowser.main_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(gitbrowser.main_menu), gitbrowser.action_menu_items[CMD_REPOSITORY_ADD]);
	gtk_menu_shell_append(GTK_MENU_SHELL(gitbrowser.main_menu), gitbrowser.action_menu_items[CMD_REPOSITORY_ADD_MULTIPLE]);
	gtk_menu_shell_append(GTK_MENU_SHELL(gitbrowser.main_menu), gitbrowser.action_menu_items[CMD_REPOSITORY_ADD_FROM_DOCUMENT]);
	gtk_menu_shell_append(GTK_MENU_SHELL(gitbrowser.main_menu), gitbrowser.action_menu_items[CMD_REPOSITORY_ADD_SEPARATOR]);
	gtk_menu_shell_append(GTK_MENU_SHELL(gitbrowser.main_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(gitbrowser.main_menu), gitbrowser.action_menu_items[CMD_REPOSITORY_REMOVE_ALL]);
	gtk_widget_show_all(gitbrowser.main_menu);
	gtk_menu_popup_at_pointer(GTK_MENU(gitbrowser.main_menu), (GdkEvent *) evt);
}

static void menu_popup_repository(GdkEventButton *evt, gboolean is_separator)
{
	GtkWidget	*menu = menu_popup_create();

	if(!is_separator)
	{
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_REPOSITORY_OPEN_QUICK]);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_REPOSITORY_GREP]);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_DIR_EXPLORE]);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_DIR_TERMINAL]);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_REPOSITORY_REFRESH]);
	}
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_REPOSITORY_MOVE_UP]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_REPOSITORY_MOVE_DOWN]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_REPOSITORY_REMOVE]);
	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) evt);
}

static void menu_popup_directory(GdkEventButton *evt)
{
	GtkWidget	*menu = menu_popup_create();

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_DIR_EXPAND]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_DIR_COLLAPSE]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_DIR_EXPLORE]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_DIR_TERMINAL]);
	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) evt);
}

static void menu_popup_file(GdkEventButton *evt)
{
	GtkWidget	*menu = menu_popup_create();

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_FILE_OPEN]);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gitbrowser.action_menu_items[CMD_FILE_COPY_NAME]);
	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) evt);
}

static gboolean evt_tree_button_press(GtkWidget *wid, GdkEventButton *evt, gpointer user)
{
	if(gitbrowser.click_path != NULL)
	{
		gtk_tree_path_free(gitbrowser.click_path);
		gitbrowser.click_path = NULL;
	}

	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(wid), evt->x, evt->y, &gitbrowser.click_path, NULL, NULL, NULL);
	if(gitbrowser.click_path != NULL)
	{
		gint		depth;
		const gint	*indices = gtk_tree_path_get_indices_with_depth(gitbrowser.click_path, &depth);
		gboolean	is_separator = FALSE, is_dir = FALSE;
		GtkTreeIter	iter;

		if(indices == NULL)
			return FALSE;

		if(depth >= 2)			/* Need to determine if clicked path is separator, file or directory. */
		{
			if(gtk_tree_model_get_iter(gitbrowser.model, &iter, gitbrowser.click_path))
			{
				if(depth == 2)
					is_separator = cb_treeview_separator(GTK_TREE_MODEL(gitbrowser.model), &iter, NULL);
				else if(depth >= 3)
					is_dir = gtk_tree_model_iter_has_child(gitbrowser.model, &iter);
			}
		}

		if(evt->type == GDK_2BUTTON_PRESS && evt->button == 1 && depth >= 3)
		{
			if(is_dir)
			{
				if(!gtk_tree_view_collapse_row(GTK_TREE_VIEW(gitbrowser.view), gitbrowser.click_path))
					gtk_tree_view_expand_row(GTK_TREE_VIEW(gitbrowser.view), gitbrowser.click_path, TRUE);
			}
			else
				gtk_action_activate(gitbrowser.actions[CMD_FILE_OPEN]);
		}
		else if(evt->type == GDK_BUTTON_PRESS && evt->button == 3)
		{
			if(depth == 1 && indices[0] == 0)
				menu_popup_repositories(evt);
			else if(depth == 2)
				menu_popup_repository(evt, is_separator);
			else if(depth >= 3)
			{
				if(is_dir)
					menu_popup_directory(evt);
				else
					menu_popup_file(evt);
			}
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean cb_treeview_separator(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	gpointer	repo;
	gboolean	is_separator;

	gtk_tree_model_get(model, iter, 0, &repo, -1);
	is_separator = (repo == NULL);
	g_free(repo);

	return is_separator;
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

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
	gtk_tree_view_set_row_separator_func(GTK_TREE_VIEW(view), cb_treeview_separator, NULL, NULL);

	g_signal_connect(G_OBJECT(view), "button_press_event", G_CALLBACK(evt_tree_button_press), NULL);

	return view;
}

struct expansion_state {
	GtkTreeView	*view;
	GString		*expanded;
};

/* Check the expansion state of given tree node, and append its path to huge string, if it's expanded. */
static gboolean cb_check_expansion(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user)
{
	struct expansion_state	*state = user;
	const gint		*indices = gtk_tree_path_get_indices(path);
	const gint		depth = gtk_tree_path_get_depth(path);
	gint			i;

	if(!gtk_tree_model_iter_has_child(model, iter))
		return FALSE;
	if(!gtk_tree_view_row_expanded(state->view, path))
		return FALSE;

	if(state->expanded->len > 0)
		g_string_append_c(state->expanded, ',');

	for(i = 0; i < depth; i++)
	{
		if(i > 0)
			g_string_append_c(state->expanded, ':');
		g_string_append_printf(state->expanded, "%d", indices[i]);
	}
	return FALSE;
}

GString * tree_view_get_expanded(GtkTreeView *view)
{
	struct expansion_state	state;

	state.view = view;
	state.expanded = g_string_sized_new(256);
	/* Traverse the entire repository tree, and note which nodes are expanded. */
	gtk_tree_model_foreach(gtk_tree_view_get_model(view), cb_check_expansion, &state);

	return state.expanded;
}

void tree_view_set_expanded(GtkTreeView *view, const gchar *paths)
{
	gchar	**tokens;

	gtk_tree_view_collapse_all(view);
	if(paths == NULL || *paths == '\0')	/* If told to expand nothing, don't. */
		return;

	if((tokens = g_strsplit(paths, ",", -1)) != NULL)
	{
		gint	i;

		for(i = 0; tokens[i] != NULL; i++)
		{
			GtkTreePath	*path;

			if((path = gtk_tree_path_new_from_string(tokens[i])) != NULL)
			{
				gtk_tree_view_expand_row(view, path, FALSE);
			}
		}
		g_free(tokens);
	}
}

/* -------------------------------------------------------------------------------------------------------------- */

static void open_quick_reset_filter(void)
{
	GList	*repos, *iter;

	if(gitbrowser.quick_open_hide != NULL)
		g_regex_unref(gitbrowser.quick_open_hide);
	if(gitbrowser.quick_open_hide_src != NULL && gitbrowser.quick_open_hide_src[0] != '\0')
		gitbrowser.quick_open_hide = g_regex_new(gitbrowser.quick_open_hide_src, 0, 0, NULL);
	else
		gitbrowser.quick_open_hide = NULL;

	/* Because the filter might have changed, we need to go through and re-populate all the lists. */
	if((repos = g_hash_table_get_values(gitbrowser.repositories)) != NULL)
	{
		for(iter = repos; iter != NULL; iter = g_list_next(iter))
		{
			Repository	*repo = iter->data;

			if(repo->quick_open.dialog != NULL)
			{
				repository_to_list(repo, gitbrowser.model, &repo->quick_open);
				open_quick_update_label(&repo->quick_open);
			}
		}
		g_list_free(repos);
	}
}

/* -------------------------------------------------------------------------------------------------------------- */

static gboolean cb_key_group_callback(guint key_id)
{
	switch(key_id)
	{
	case KEY_REPOSITORY_OPEN_QUICK_FROM_DOCUMENT:
		gtk_action_activate(gitbrowser.actions[CMD_REPOSITORY_OPEN_QUICK_FROM_DOCUMENT]);
		return TRUE;
	case KEY_REPOSITORY_GREP:
		gtk_action_activate(gitbrowser.actions[CMD_REPOSITORY_GREP]);
		break;
	}
	return FALSE;
}

void plugin_init(GeanyData *geany_data)
{
	GtkWidget	*scwin;
	gchar		*dir;

	init_commands(gitbrowser.actions, gitbrowser.action_menu_items);

	gitbrowser.model = tree_model_new();
	gitbrowser.view = tree_view_new(gitbrowser.model);
	gitbrowser.repositories = g_hash_table_new(g_str_hash, g_str_equal);
	gitbrowser.quick_open_filter_max_time = 50;
	gitbrowser.quick_open_hide = NULL;
	gitbrowser.terminal_cmd = "gnome-terminal";
	gitbrowser.add_dialog = NULL;

	gitbrowser.key_group = plugin_set_key_group(geany_plugin, MNEMONIC_NAME, NUM_KEYS, cb_key_group_callback);
	keybindings_set_item(gitbrowser.key_group, KEY_REPOSITORY_OPEN_QUICK_FROM_DOCUMENT, NULL, GDK_KEY_o, GDK_MOD1_MASK | GDK_SHIFT_MASK, "repository-open-quick-from-document", _("Quick Open from Document"), gitbrowser.action_menu_items[CMD_REPOSITORY_OPEN_QUICK_FROM_DOCUMENT]);
	keybindings_set_item(gitbrowser.key_group, KEY_REPOSITORY_GREP, NULL, GDK_KEY_g, GDK_MOD1_MASK | GDK_SHIFT_MASK, "repository-grep", _("Grep Repository"), gitbrowser.action_menu_items[CMD_REPOSITORY_GREP]);

	dir = g_strconcat(geany->app->configdir, G_DIR_SEPARATOR_S, "plugins", G_DIR_SEPARATOR_S, MNEMONIC_NAME, NULL);
	utils_mkdir(dir, TRUE);
	gitbrowser.config_filename = g_strconcat(dir, G_DIR_SEPARATOR_S, MNEMONIC_NAME ".conf", NULL);
	g_free(dir);

	gitbrowser.prefs = stash_group_new(MNEMONIC_NAME);
	stash_group_add_entry(gitbrowser.prefs, &gitbrowser.quick_open_hide_src, CFG_QUICK_OPEN_HIDE_SRC, NULL, CFG_QUICK_OPEN_HIDE_SRC);
	stash_group_add_spin_button_integer(gitbrowser.prefs, &gitbrowser.quick_open_filter_max_time, CFG_QUICK_OPEN_FILTER_MAX_TIME, 50, CFG_QUICK_OPEN_FILTER_MAX_TIME);
	stash_group_add_entry(gitbrowser.prefs, &gitbrowser.terminal_cmd, CFG_TERMINAL_CMD, "gnome-terminal", CFG_TERMINAL_CMD);

	repository_load_all();

	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), gitbrowser.view);
	gtk_widget_show_all(scwin);
	gitbrowser.page = gtk_notebook_append_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), scwin, gtk_label_new("Git Browser"));
}

static void cb_configure_response(GtkDialog *dialog, gint response, gpointer user)
{
	if(response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY)
	{
		stash_group_update(gitbrowser.prefs, GTK_WIDGET(dialog));
		open_quick_reset_filter();
	}
}

GtkWidget * plugin_configure(GtkDialog *dlg)
{
	GtkWidget		*vbox, *frame, *grid, *label, *hbox;
	static PrefsWidgets	prefs_widgets;

	vbox = gtk_vbox_new(FALSE, 0);

	frame = gtk_frame_new(_("Quick Open Filtering"));
	grid = gtk_grid_new();
	label = gtk_label_new(_("Always hide files matching (RE)"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
	prefs_widgets.filter_re = gtk_entry_new();
	gtk_grid_attach(GTK_GRID(grid), prefs_widgets.filter_re, 1, 0, 1, 1);
	ui_hookup_widget(GTK_WIDGET(dlg), prefs_widgets.filter_re, CFG_QUICK_OPEN_HIDE_SRC);
	label = gtk_label_new(_("Keyboard filter for max (ms)"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
	prefs_widgets.filter_time = gtk_spin_button_new_with_range(10, 400, 5);
	gtk_grid_attach(GTK_GRID(grid), prefs_widgets.filter_time, 1, 1, 1, 1);
	ui_hookup_widget(GTK_WIDGET(dlg), prefs_widgets.filter_time, CFG_QUICK_OPEN_FILTER_MAX_TIME);
	gtk_container_add(GTK_CONTAINER(frame), grid);
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 0);
	label = gtk_label_new("Terminal command");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	prefs_widgets.terminal_cmd = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), prefs_widgets.terminal_cmd, TRUE, TRUE, 5);
	ui_hookup_widget(GTK_WIDGET(dlg), prefs_widgets.terminal_cmd, CFG_TERMINAL_CMD);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

	stash_group_display(gitbrowser.prefs, GTK_WIDGET(dlg));

	gtk_widget_show_all(vbox);

	g_signal_connect(G_OBJECT(dlg), "response", G_CALLBACK(cb_configure_response), &prefs_widgets);

	return vbox;
}

void plugin_cleanup(void)
{
	repository_save_all(gitbrowser.model);
	gtk_notebook_remove_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), gitbrowser.page);
	stash_group_free(gitbrowser.prefs);
	g_free(gitbrowser.config_filename);
	g_hash_table_destroy(gitbrowser.repositories);
}
