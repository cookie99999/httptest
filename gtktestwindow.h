#ifndef __GTKTESTWINDOW_H
#define __GTKTESTWINDOW_H

#include <gtk/gtk.h>
#include "gtktest.h"

#define GTKTEST_APP_WINDOW_TYPE (gtktest_app_window_get_type())
G_DECLARE_FINAL_TYPE(GtkTestAppWindow, gtktest_app_window, GTKTEST, APP_WINDOW, GtkApplicationWindow);

GtkTestAppWindow *gtktest_app_window_new(GtkTestApp *app);

#endif /* __GTKTESTWINDOW_H */
