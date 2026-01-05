#include "client.h"
#include <stdlib.h>



//---------------CREATE A WAITING TAB TO BE ASSIGNED-----------------------
// button depreacated, but funcion used to assign clients
ClientInfo* create_waiting_tab(GtkWidget *notebook) {
    GtkWidget *label = gtk_label_new("Waiting...");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, label);

    ClientInfo *info = g_new0(ClientInfo,1);
    info->tab_label = label;
    info->tab_content = box;
    info->notebook = notebook;

    /*                                                                      deprecated, as long as there a now waiting tabs in action
    GtkWidget *btn_close = gtk_button_new_with_label("Close Tab");
    gtk_box_pack_start(GTK_BOX(box), btn_close, FALSE, FALSE, 4);
    info->close_button = btn_close;
    g_signal_connect(btn_close, "clicked", G_CALLBACK(on_close_waiting_tab_clicked), info);
    */

    gtk_widget_show_all(box);
    return info;
}








// ----------------- MEMORY-FRIENDLY REMOVE CLIENT -------------------
void free_client_info(ClientInfo *info){
    if (!info) return;

    // Close socket if connected
    if (info->conn) {
        GSocket *sock = g_socket_connection_get_socket(info->conn);
        if (sock) g_socket_close(sock, NULL);
        g_object_unref(info->conn);
    }

    // Free ClientSettings
    if (info->settings) {
        free_gui_config(info->settings);
        info->settings = NULL;
    }

    // Free measurement buffer
    if (info->measurement_data)
        g_free(info->measurement_data);

    // Free TimeSync buffer
    if (info->client_time_sync)
        g_free(info->client_time_sync);

    //remove connection statur monitor and syncronize measurement thread
    info->destroying = 1;
    if (info->monitor_id > 0) {
      g_source_remove(info->monitor_id);
      info->monitor_id = 0;
    }
    if (info->measurement_thread) g_thread_join(info->measurement_thread);

    // Remove notebook page (destroys all children, including widgets holding PreampData)
    if (info->notebook && info->tab_content) {
        gint page_num = gtk_notebook_page_num(GTK_NOTEBOOK(info->notebook),
                                              info->tab_content);
        if (page_num != -1)
            gtk_notebook_remove_page(GTK_NOTEBOOK(info->notebook), page_num);
    }

    // Destroy close button if it’s outside tab_content
    if (info->close_button) {
        gtk_widget_destroy(info->close_button);
        info->close_button = NULL;
    }

    // Free ClientInfo itself
    g_free(info);
}

