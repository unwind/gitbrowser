
#include "geanyplugin.h"

PLUGIN_VERSION_CHECK(147)

PLUGIN_SET_INFO("Git Browser", "A minimalistic browser for Git repositories",
                "1.0", "Emil Brink <emil@obsession.se>");

static struct
{
	GtkListStore	*store;
} gitbrowser;

/* Trivial convenience wrapper for g_spawn_sync(); returns command output. */
gboolean subprocess_run(const gchar* working_dir, gchar **argv, gchar **env, gchar **output, gchar **error)
{
	return g_spawn_sync(working_dir, argv, env, G_SPAWN_SEARCH_PATH, NULL, NULL, output, error, NULL, NULL);
}

/* Split the given collection of lines into individual lines, and return each one. Silently truncates. */
const gchar * string_next_line(const gchar *lines, gchar *buffer, size_t buf_size)
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

void plugin_init(GeanyData *geany_data)
{
	GtkWidget	*button;
	gchar		*get_files[] = { "git", "ls-files", NULL };
	gchar		*output = NULL, *error = NULL;

	button = gtk_button_new_with_label("Hit me!");
	gtk_widget_show(button);
	gtk_notebook_append_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), button, gtk_label_new("Git Browser"));

	gitbrowser.store = gtk_list_store_new(1, G_TYPE_STRING);

	if(subprocess_run("/home/emil/data/workspace/gitbrowser", get_files, NULL, &output, &error) >= 0)
	{
		const gchar	*iter = output;
		gchar		line[1024];

		while(iter = string_next_line(iter, line, sizeof line))
			printf("'%s'\n", line);
		g_free(output);
		g_free(error);
	}
}

void plugin_cleanup(void)
{
}
