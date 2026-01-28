//gcc C_GUI_Host_main.c gui.c client.c network.c settings.c -o MulitMeasurementMonitor -DLINUX `pkg-config --cflags --libs gtk+-3.0 json-glib-1.0` -lm
// gcc C_GUI_Host_main.c gui.c client.c network.c settings.c -o ./WinApp/bin/MulitMeasurementMonitor.exe -D_WIN32 -lws2_32 -liphlpapi $(pkg-config --cflags --libs gtk+-3.0 json-glib-1.0)

#ifdef _WIN32
    #define _WINSOCK_DEPRECATED_NO_WARNINGS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    // mingw does not provide unistd.h nor close()
    #define close closesocket
#else   // ----------- LINUX -----------
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <ifaddrs.h>
    #include <netinet/in.h>
#endif

#include "gui.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>




GSocketService *global_service = NULL;

typedef struct {
    int sock;
    struct sockaddr_in addr;
} BroadcastInfo;
BroadcastInfo *broadcast_info = NULL;






// Helper: check if an IP is private (typical hotspot IP ranges)
static int is_private_ip(uint32_t ip)
{
    // 10.0.0.0/8
    if ((ip & 0xFF000000) == 0x0A000000) return 1;
    // 172.16.0.0/12
    if ((ip & 0xFFF00000) == 0xAC100000) return 1;
    // 192.168.0.0/16
    if ((ip & 0xFFFF0000) == 0xC0A80000) return 1;
    return 0;
}







// Detect a valid hotspot IP 
static uint32_t detect_hotspot_ip(){
    #ifdef _WIN32
        IP_ADAPTER_ADDRESSES *addr_buf = NULL, *adapter;
        DWORD buf_len = 15000;
        DWORD ret;

        addr_buf = (IP_ADAPTER_ADDRESSES *)malloc(buf_len);
        if (!addr_buf) {
            fprintf(stderr, "Memory allocation failed\n");
            return 0xFFFFFFFF;
        }

        ret = GetAdaptersAddresses(AF_INET, 0, NULL, addr_buf, &buf_len);
        if (ret != NO_ERROR) {
            fprintf(stderr, "GetAdaptersAddresses() failed: %lu\n", ret);
            free(addr_buf);
            return 0xFFFFFFFF;
        }

        uint32_t hotspot_ip = 0;

        char descA[512];

        /* ============================================================
        PASS 1 — Explicitly search for Windows Hotspot subnet:
                    192.168.137.0/24 (0xC0A88900)
        ============================================================ */
        for (adapter = addr_buf; adapter; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp) continue;

            for (IP_ADAPTER_UNICAST_ADDRESS *ua = adapter->FirstUnicastAddress;
                ua; ua = ua->Next)
            {
                struct sockaddr_in *sa = (struct sockaddr_in *)ua->Address.lpSockaddr;
                uint32_t ip = ntohl(sa->sin_addr.s_addr);

                if ((ip & 0xFF000000) == 0x7F000000) continue; // skip loopback

                if ((ip & 0xFFFFFF00) == 0xC0A88900)  // 192.168.137.x
                {
                    char buf[32];
                    inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
                    printf("Found standard Windows Mobile Hotspot IP: %s\n", buf);

                    hotspot_ip = ip;
                    free(addr_buf);
                    return hotspot_ip;
                }
            }
        }


        /* ============================================================
        PASS 2 — Adapter description contains:
                    "Wi-Fi Direct", "Virtual Adapter", "WLAN"
        ============================================================ */
        for (adapter = addr_buf; adapter; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp) continue;

            // Convert wide string description to UTF-8/ASCII
            if (wcstombs(descA, adapter->Description, sizeof(descA)) == (size_t)-1)
                continue;

            if (strstr(descA, "Wi-Fi Direct") ||
                strstr(descA, "WiFi Direct") ||
                strstr(descA, "Virtual Adapter") ||
                strstr(descA, "WLAN"))
            {
                for (IP_ADAPTER_UNICAST_ADDRESS *ua = adapter->FirstUnicastAddress;
                    ua; ua = ua->Next)
                {
                    struct sockaddr_in *sa = (struct sockaddr_in *)ua->Address.lpSockaddr;
                    uint32_t ip = ntohl(sa->sin_addr.s_addr);

                    if (!is_private_ip(ip)) continue;

                    char buf[32];
                    inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
                    printf("Found Hotspot-like interface (desc='%s') → %s\n", descA, buf);

                    hotspot_ip = ip;
                    free(addr_buf);
                    return hotspot_ip;
                }
            }
        }


        /* ============================================================
        PASS 3 — Fallback: first private IPv4 (but skip VirtualBox)
        ============================================================ */
        for (adapter = addr_buf; adapter; adapter = adapter->Next) {

            if (adapter->OperStatus != IfOperStatusUp) continue;

            for (IP_ADAPTER_UNICAST_ADDRESS *ua = adapter->FirstUnicastAddress;
                ua; ua = ua->Next)
            {
                struct sockaddr_in *sa = (struct sockaddr_in *)ua->Address.lpSockaddr;
                uint32_t ip = ntohl(sa->sin_addr.s_addr);

                if ((ip & 0xFF000000) == 0x7F000000) continue; // skip loopback

                if (!is_private_ip(ip)) continue;

                // Skip VirtualBox 192.168.56.x
                if ((ip & 0xFFFF0000) == 0xC0A83800) continue; // 192.168.56.0/24

                char buf[32];
                inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
                printf("Fallback to private IP: %s\n", buf);

                hotspot_ip = ip;
                free(addr_buf);
                return hotspot_ip;
            }
        }

        free(addr_buf);
        fprintf(stderr, "No hotspot/private IP found — using 255.255.255.255\n");
        return 0xFFFFFFFF;
    
    #else
        // ---------- LINUX ----------
        struct ifaddrs *ifaddr, *ifa;
        char buf[INET_ADDRSTRLEN];

        if (getifaddrs(&ifaddr) == -1) {
            perror("getifaddrs");
            exit(1);
        }

        uint32_t hotspot_ip = 0;

        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;

            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            uint32_t ip = ntohl(sa->sin_addr.s_addr);

            if ((ip & 0xFF000000) == 0x7F000000) continue; // skip loopback

            if (is_private_ip(ip)) {
                hotspot_ip = ip;
                inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
                printf("Detected hotspot IP: %s on interface %s\n", buf, ifa->ifa_name);
                break;
            }
        }

        freeifaddrs(ifaddr);

        if (hotspot_ip == 0) {
            fprintf(stderr, "No private hotspot IP detected, using 255.255.255.255\n");
            hotspot_ip = 0xFFFFFFFF;
        }

        return hotspot_ip;
    #endif
}









//helper for cumputing broadcast adress
static uint32_t compute_broadcast(uint32_t ip)
{
    uint32_t netmask = 0xFFFFFF00; // assume /24 for hotspot
    return (ip & netmask) | (~netmask & 0xFF);
}

//callback for doing broadcast
gboolean broadcast_callback(gpointer data)
{
    BroadcastInfo *info = (BroadcastInfo *)data;
    const char *msg = "SERVER_ALIVE";

    ssize_t sent = sendto(info->sock, msg, strlen(msg), 0,
                          (struct sockaddr *)&info->addr,
                          sizeof(info->addr));

    if (sent < 0) g_warning("Broadcast send failed: %m");
    // else g_message("Broadcast sent");

    return TRUE; // repeat
}










// Shutdown handler
static void on_close_clicked(GtkButton *button, gpointer user_data)
{
    g_print("Shutting down Multi Measurement Monitor...\n");

    // Stop socket service
    if (global_service) {
        g_socket_service_stop(global_service);
        g_object_unref(global_service);
        global_service = NULL;
    }

    // Close broadcast socket
    if (broadcast_info) {
        close(broadcast_info->sock);
        g_free(broadcast_info);
        broadcast_info = NULL;
    }

    // Free queue
    if (waiting_tabs) {
        g_queue_free(waiting_tabs);
        waiting_tabs = NULL;
    }

    // Free mutex
    g_mutex_clear(&queue_lock);

    gtk_main_quit();
}



















int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    // Init queue + mutex
    waiting_tabs = g_queue_new();
    g_mutex_init(&queue_lock);

    #ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
    #endif

    // Main window
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Multi Measurement Monitor");
    gtk_window_set_default_size(GTK_WINDOW(win), 900, 600);

    // Notebook container
    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(win), notebook);

    //
    // MAIN TAB
    //
    GtkWidget *main_overlay = gtk_overlay_new();
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *main_label = gtk_label_new("Main");

    // Place main_box inside the overlay
    gtk_overlay_add_overlay(GTK_OVERLAY(main_overlay), main_box);
    gtk_widget_set_halign(main_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign(main_box, GTK_ALIGN_FILL);

    // Create bottom-right label ONLY for the main tab
    GtkWidget *corner_label =
        gtk_label_new("Author: Thomas Schreck - written for:\nMeasurement and Sensor Technology Group\nTU Darmstadt");

    gtk_widget_set_halign(corner_label, GTK_ALIGN_END);   // right aligned
    gtk_widget_set_valign(corner_label, GTK_ALIGN_END);   // bottom aligned
    gtk_widget_set_margin_end(corner_label, 10);
    gtk_widget_set_margin_bottom(corner_label, 10);

    // Add the label to the overlay
    gtk_overlay_add_overlay(GTK_OVERLAY(main_overlay), corner_label);
    gtk_widget_set_opacity(corner_label, 0.75); // optional fade


    // Add Client button
    //GtkWidget *btn_add = gtk_button_new_with_label("Add Client");
    //gtk_box_pack_start(GTK_BOX(main_box), btn_add, FALSE, FALSE, 5);
    //g_signal_connect(btn_add, "clicked",
    //                 G_CALLBACK(on_add_client_clicked), notebook);

    // --- START ALL ---
    GtkWidget *btn_start_all = gtk_button_new_with_label("Start all Clients");
    gtk_box_pack_start(GTK_BOX(main_box), btn_start_all, FALSE, FALSE, 5);
    g_signal_connect(btn_start_all, "clicked",
                     G_CALLBACK(on_start_all_clicked), notebook);

    // --- STOP ALL ---
    GtkWidget *btn_stop_all = gtk_button_new_with_label("Stop all Clients");
    gtk_box_pack_start(GTK_BOX(main_box), btn_stop_all, FALSE, FALSE, 5);
    g_signal_connect(btn_stop_all, "clicked",
                     G_CALLBACK(on_stop_all_clicked), notebook);


    // --- GLOBAL DURATION ---
    GtkWidget *global_dur_label = gtk_label_new("Global Duration (seconds)");
    GtkWidget *global_dur_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(global_dur_entry), "3600");

    // store it on the main window for retrieval if needed
    g_object_set_data(G_OBJECT(win), "global_dur_entry", global_dur_entry);

    // add to GUI
    gtk_box_pack_start(GTK_BOX(main_box), global_dur_label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_box), global_dur_entry, FALSE, FALSE, 5);
    g_signal_connect(global_dur_entry, "changed",
                 G_CALLBACK(on_global_duration_changed), notebook);


    // Close Client Monitor button
    GtkWidget *btn_close = gtk_button_new_with_label("Close Client Monitor");
    gtk_box_pack_start(GTK_BOX(main_box), btn_close, FALSE, FALSE, 5);
    g_signal_connect(btn_close, "clicked",
                     G_CALLBACK(on_close_clicked), NULL);

    // Add main tab to notebook
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), main_overlay, main_label);

    // Close window event
    g_signal_connect(win, "destroy", G_CALLBACK(on_close_clicked), NULL);

    //
    // TCP SOCKET SERVICE ON PORT 8080
    //
    global_service = g_socket_service_new();
    GInetAddress *addr = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
    GSocketAddress *sockaddr = g_inet_socket_address_new(addr, 8080);

    gboolean ok = g_socket_listener_add_address(
        G_SOCKET_LISTENER(global_service),
        sockaddr,
        G_SOCKET_TYPE_STREAM,
        G_SOCKET_PROTOCOL_TCP,
        NULL, NULL, NULL
    );

    if (!ok) {
        g_printerr("Failed to bind to port 8080\n");
        return 1;
    }

    g_object_unref(addr);
    g_object_unref(sockaddr);

    // Connect incoming clients
    g_signal_connect(global_service, "incoming",
                     G_CALLBACK(on_client_incoming), notebook);

    g_socket_service_start(global_service);


    // UDP BROADCAST INITIALIZATION

    // Initialize broadcast_info
    // -----------------------------
    uint32_t host_ip = detect_hotspot_ip();
    uint32_t broadcast_ip = compute_broadcast(host_ip);

    broadcast_info = g_new0(BroadcastInfo, 1);

    broadcast_info->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (broadcast_info->sock < 0)
        g_error("Broadcast socket() failed: %m");

    int broadcastEnable = 1;
    #ifdef _WIN32
        if (setsockopt(broadcast_info->sock, SOL_SOCKET, SO_BROADCAST,
                    (const char *)&broadcastEnable,
                    sizeof(broadcastEnable)) < 0)
    #else
        if (setsockopt(broadcast_info->sock, SOL_SOCKET, SO_BROADCAST,
                    &broadcastEnable,
                    sizeof(broadcastEnable)) < 0)
    #endif
    memset(&broadcast_info->addr, 0, sizeof(broadcast_info->addr));
    broadcast_info->addr.sin_family = AF_INET;
    broadcast_info->addr.sin_port = htons(5000);
    broadcast_info->addr.sin_addr.s_addr = htonl(broadcast_ip);

    // Send broadcast every 5000 ms
    g_timeout_add(5000, broadcast_callback, broadcast_info);


    // Run GUI
    gtk_widget_show_all(win);
    gtk_main();

    #ifdef _WIN32
    WSACleanup();
    #endif

    return 0;
}

