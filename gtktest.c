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

  window = gtktest_app_window_new(GTKTEST_APP(app));
  gtk_window_set_title(GTK_WINDOW(window), "GTK Test");
  gtk_widget_show_all(GTK_WIDGET(window));
}

static void gtktest_app_class_init(GtkTestAppClass *class) {
  G_APPLICATION_CLASS(class)->activate = gtktest_app_activate;
}

GtkTestApp *gtktest_app_new(void) {
  return g_object_new(GTKTEST_APP_TYPE, "application-id", "club.hanbaobao.gtktest",
		      "flags", "G_APPLICATION_DEFAULT_FLAGS", NULL);
}
