
#include "geanyplugin.h"

PLUGIN_VERSION_CHECK(147)

PLUGIN_SET_INFO("Git Browser", "A minimalistic browser for Git repositories",
                "1.0", "Emil Brink <emil@obsession.se>");

static struct
{
	GtkListStore	*store;
} gitbrowser;


void plugin_init(GeanyData *geany_data)
{
	GtkWidget	*button;

	button = gtk_button_new_with_label("Hit me!");
	gtk_widget_show(button);
	gtk_notebook_append_page(GTK_NOTEBOOK(geany->main_widgets->sidebar_notebook), button, gtk_label_new("Git Browser"));

	gitbrowser.store = gtk_list_store_new(1, G_TYPE_STRING);
}

void plugin_cleanup(void)
{
}
