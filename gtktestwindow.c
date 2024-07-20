#include <gtk/gtk.h>

#include "gtktest.h"
#include "gtktestwindow.h"
#include "httpoop/get.h"

struct _GtkTestAppWindow {
  GtkApplicationWindow parent;
};

typedef struct _GtkTestAppWindowPrivate GtkTestAppWindowPrivate;
struct _GtkTestAppWindowPrivate {
  GtkWidget *host_text, *resource_text;
  GtkWidget *textarea;
};

G_DEFINE_TYPE_WITH_PRIVATE(GtkTestAppWindow, gtktest_app_window, GTK_TYPE_APPLICATION_WINDOW);

static void gtktest_app_window_init(GtkTestAppWindow *win) {
  GtkWidget *box, *box2;
  GtkWidget *button;
  
  GtkTestAppWindowPrivate *priv;
  priv = gtktest_app_window_get_instance_private(win);
  
  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  gtk_container_add(GTK_CONTAINER(win), box);

  box2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_widget_set_halign(box2, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box2, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(box), box2, FALSE, FALSE, 0);

  priv->host_text = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(box2), priv->host_text, FALSE, FALSE, 0);

  priv->resource_text = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(box2), priv->resource_text, FALSE, FALSE, 0);

  button = gtk_button_new_with_label("Request");
  g_signal_connect(button, "clicked", G_CALLBACK(gtktest_app_window_reqcb), win);
  gtk_box_pack_start(GTK_BOX(box2), button, FALSE, FALSE, 0);

  priv->textarea = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(priv->textarea), FALSE);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(priv->textarea), TRUE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(priv->textarea), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(priv->textarea), GTK_WRAP_WORD_CHAR);
  gtk_box_pack_start(GTK_BOX(box), priv->textarea, FALSE, FALSE, 0);
}

static void gtktest_app_window_class_init(GtkTestAppWindowClass *class) {
}

GtkTestAppWindow *gtktest_app_window_new(GtkTestApp *app) {
  return g_object_new(GTKTEST_APP_WINDOW_TYPE, "application", app, NULL);
}

void gtktest_app_window_reqcb (GtkWidget *widget, GtkTestAppWindow *win) {
  GtkTestAppWindowPrivate *priv;
  priv = gtktest_app_window_get_instance_private(win);

  GtkTextBuffer *viewbuf;
  viewbuf = gtk_text_buffer_new(NULL);
  GtkTextIter iter;
  gtk_text_buffer_get_iter_at_offset(viewbuf, &iter, 0);

  const gchar *host = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(priv->host_text)));
  const gchar *resource = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(priv->resource_text)));

  gtk_text_buffer_insert(viewbuf, &iter, httpoop_get((char *)host, (char *)resource), -1);
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(priv->textarea), viewbuf);
}
