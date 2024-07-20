#include <gtk/gtk.h>
#include "gtktest.h"
#include "gtktestwindow.h"

struct _GtkTestApp {
  GtkApplication parent;
};

G_DEFINE_TYPE(GtkTestApp, gtktest_app, GTK_TYPE_APPLICATION);

static void gtktest_app_init(GtkTestApp *app) {
}

static void gtktest_app_activate(GApplication *app) {
  GtkTestAppWindow *window;
  GtkWidget *box, *box2;
  GtkWidget *button;
  GtkWidget *host_text, *resource_text;
  GtkWidget *textarea;

  window = gtktest_app_window_new(GTKTEST_APP(app));
  gtk_window_set_title(GTK_WINDOW(window), "Hello");

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  gtk_container_add(GTK_CONTAINER(window), box);

  box2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_widget_set_halign(box2, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(box2, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(box), box2, FALSE, FALSE, 0);

  host_text = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(box2), host_text, FALSE, FALSE, 0);

  resource_text = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(box2), resource_text, FALSE, FALSE, 0);

  button = gtk_button_new_with_label("Request");
  g_signal_connect(button, "clicked", G_CALLBACK(gtktest_app_reqcb), NULL);
  gtk_box_pack_start(GTK_BOX(box2), button, FALSE, FALSE, 0);

  textarea = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(textarea), FALSE);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(textarea), TRUE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textarea), FALSE);
  gtk_box_pack_start(GTK_BOX(box), textarea, FALSE, FALSE, 0);
  
  gtk_widget_show_all(GTK_WIDGET(window));
}

void gtktest_app_reqcb (GtkWidget *widget, gpointer data) {
  ;
}

static void gtktest_app_class_init(GtkTestAppClass *class) {
  G_APPLICATION_CLASS(class)->activate = gtktest_app_activate;
}

GtkTestApp *gtktest_app_new(void) {
  return g_object_new(GTKTEST_APP_TYPE, "application-id", "club.hanbaobao.gtktest",
		      "flags", "G_APPLICATION_DEFAULT_FLAGS", NULL);
}
