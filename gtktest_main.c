#include <gtk/gtk.h>
#include "gtktest.h"

int main(int argc, char **argv) {
  return g_application_run(G_APPLICATION(gtktest_app_new()), argc, argv);
}
