#include <gtk/gtk.h>

#include "gtktest.h"
#include "gtktestwindow.h"
#include "httpoop/get.h"
#include "httpoop/util.h"
#include "htmlatrine/parser.h"

struct _GtkTestAppWindow {
  GtkApplicationWindow parent;
};

typedef struct _GtkTestAppWindowPrivate GtkTestAppWindowPrivate;
struct _GtkTestAppWindowPrivate {
  GtkWidget *uri_entry;
  GtkWidget *textarea;
};

G_DEFINE_TYPE_WITH_PRIVATE(GtkTestAppWindow, gtktest_app_window, GTK_TYPE_APPLICATION_WINDOW);

static void gtktest_app_window_init(GtkTestAppWindow *win) {
  GtkWidget *box, *box2;
  GtkWidget *button;
  GtkWidget *scrollwin;
  
  GtkTestAppWindowPrivate *priv;
  priv = gtktest_app_window_get_instance_private(win);
  
  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_halign(box, GTK_ALIGN_FILL);
  gtk_widget_set_valign(box, GTK_ALIGN_FILL);
  gtk_container_add(GTK_CONTAINER(win), box);

  box2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_widget_set_halign(box2, GTK_ALIGN_FILL);
  gtk_widget_set_valign(box2, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(box), box2, FALSE, FALSE, 0);

  priv->uri_entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(box2), priv->uri_entry, TRUE, TRUE, 0);

  button = gtk_button_new_with_label("Request");
  g_signal_connect(button, "clicked", G_CALLBACK(gtktest_app_window_reqcb), win);
  gtk_box_pack_start(GTK_BOX(box2), button, FALSE, FALSE, 0);

  priv->textarea = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(priv->textarea), FALSE);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(priv->textarea), TRUE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(priv->textarea), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(priv->textarea), GTK_WRAP_WORD_CHAR);

  scrollwin = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(scrollwin), priv->textarea);
  gtk_box_pack_start(GTK_BOX(box), scrollwin, TRUE, TRUE, 0);
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

  const gchar *uri = gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(priv->uri_entry)));
  char *h, *r;
  split_uri((char *)uri, NULL, &h, &r);
  httpoop_response resp = httpoop_get(h, r);
  free(h);
  free(r);
  if (resp.buffer == NULL) {
    //TODO: show some error dialogue
  }

  htmlatrine_dom dom = htmlatrine_parse(resp.buffer);
  
  gtk_text_buffer_insert(viewbuf, &iter, resp.buffer, -1);
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(priv->textarea), viewbuf);
  
  httpoop_response_delete(resp);
}
