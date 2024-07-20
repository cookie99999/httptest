#include <gtk/gtk.h>

#include "gtktest.h"
#include "gtktestwindow.h"

struct _GtkTestAppWindow {
  GtkApplicationWindow parent;
};

G_DEFINE_TYPE(GtkTestAppWindow, gtktest_app_window, GTK_TYPE_APPLICATION_WINDOW);

static void gtktest_app_window_init(GtkTestAppWindow *app) {
}

static void gtktest_app_window_class_init(GtkTestAppWindowClass *class) {
}

GtkTestAppWindow *gtktest_app_window_new(GtkTestApp *app) {
  return g_object_new(GTKTEST_APP_WINDOW_TYPE, "application", app, NULL);
}
