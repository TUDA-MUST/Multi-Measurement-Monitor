#ifndef GUI_H
#define GUI_H


#include "client.h"
#include "network.h"
#include <gtk/gtk.h>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

#define PLOT_HISTORY 5   // last N seconds displayed in live plot


extern GQueue *waiting_tabs;
extern GMutex queue_lock;

// --------------------
// GUI Building
// --------------------
void build_client_ui(ClientInfo *info, GtkWidget *win);
void assign_client_to_waiting_tab(GSocketConnection *conn, GuiConfig* client_config ,GtkWidget *notebook);

// --------------------
// Event Handlers
// --------------------
void on_remove_client_clicked(GtkButton *btn, gpointer user_data);
void on_close_waiting_tab_clicked(GtkButton *btn, gpointer user_data);
static void on_load_json_clicked(GtkButton *btn, ClientInfo *info);
static void on_send_btn_clicked(GtkButton *button, gpointer user_data);
static void on_stop_btn_clicked(GtkButton *button, gpointer user_data);
gboolean on_client_incoming(GSocketService *service, GSocketConnection *conn, GObject *src, gpointer user_data);
static void on_export_csv_clicked(GtkButton *button, gpointer user_data);
static gboolean on_plot_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data);

// --------------------
// Misc helpers
// --------------------
static void set_preamp_combo(GtkWidget *combo, int value);
static gpointer measurement_thread_func(gpointer user_data);

// --------------------
// Global Controls
// --------------------
void on_start_all_clicked(GtkButton *btn, gpointer user_data);
void on_stop_all_clicked(GtkButton *btn, gpointer user_data);
void on_global_duration_changed(GtkEditable *editable, gpointer user_data);
//void on_add_client_clicked(GtkButton *btn, gpointer user_data);                     //currently not in use and not supported

#endif // GUI_H
