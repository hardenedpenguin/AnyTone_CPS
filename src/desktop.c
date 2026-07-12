#include "desktop.h"

#include <stdio.h>

#ifdef ANYTONE_HAS_DESKTOP

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

int at_desktop_available(void)
{
    return 1;
}

int at_desktop_run(const char *url, const char *title)
{
    if (!url || !url[0])
        return 1;

    gtk_init(NULL, NULL);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), title && title[0] ? title : "Anytone");
    gtk_window_set_default_size(GTK_WINDOW(win), 1280, 840);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);

    WebKitSettings *settings = webkit_settings_new();
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_javascript_can_access_clipboard(settings, TRUE);

    WebKitWebView *view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_settings(settings));
    g_object_unref(settings);

    gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(view));
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    webkit_web_view_load_uri(view, url);
    gtk_widget_show_all(win);
    gtk_main();
    return 0;
}

#else /* !ANYTONE_HAS_DESKTOP */

int at_desktop_available(void)
{
    return 0;
}

int at_desktop_run(const char *url, const char *title)
{
    (void)url;
    (void)title;
    fprintf(stderr,
            "Desktop UI not available in this build.\n"
            "Install libwebkit2gtk-4.1-dev and rebuild, or run: anytone serve\n");
    return 1;
}

#endif
