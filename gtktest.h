#ifndef __GTKTEST_H
#define __GTKTEST_H

#include <gtk/gtk.h>

#define GTKTEST_APP_TYPE (gtktest_app_get_type())
G_DECLARE_FINAL_TYPE(GtkTestApp, gtktest_app, GTKTEST, APP, GtkApplication);

GtkTestApp *gtktest_app_new(void);
void gtktest_app_reqcb (GtkWidget *widget, gpointer data);

#endif /* __GTKTEST_H */
