
#include "settings.h"
#include "client.h"

#include <json-glib/json-glib.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>






// ******************************************************
// ----------------- HELPER CALLBACKS -------------------
// ******************************************************



static char *variant_to_string(GVariant *v, const char *datatype)
{
    if (!v || !datatype)
        return g_strdup("");

    if (g_strcmp0(datatype, "char[256]") == 0) {
        return g_strdup(g_variant_get_string(v, NULL));
    }
    else if (g_strcmp0(datatype, "uint8") == 0) {
        return g_strdup_printf("%u", g_variant_get_byte(v));
    }
    else if (g_strcmp0(datatype, "uint8_hex") == 0) {
        return g_strdup_printf("%02X", g_variant_get_byte(v));
    }

    else if (g_strcmp0(datatype, "uint32") == 0) {
        return g_strdup_printf("%u", g_variant_get_uint32(v));
    }
    else if (g_strcmp0(datatype, "uint64") == 0) {
        return g_strdup_printf("%" G_GUINT64_FORMAT,
                                g_variant_get_uint64(v));
    }
    else if (g_strcmp0(datatype, "int32") == 0) {
        return g_strdup_printf("%d", g_variant_get_int32(v));
    }
    else if (g_strcmp0(datatype, "float") == 0) {
        return g_strdup_printf("%f", g_variant_get_double(v));
    }

    g_warning("Unsupported datatype in variant_to_string: %s", datatype);
    return g_strdup("");
}




static GVariant *string_to_variant(const char *text,
                                   const char *datatype)
{
    if (!text || !datatype)
        return NULL;

    if (g_strcmp0(datatype, "char[256]") == 0) {
        return g_variant_new_string(text);
    }
    else if (g_strcmp0(datatype, "uint8") == 0) {
        return g_variant_new_byte((guchar)g_ascii_strtoull(text, NULL, 10));
    }
    else if (g_strcmp0(datatype, "uint8_hex") == 0) {
        guint v = 0;
        sscanf(text, "%x", &v);
        return g_variant_new_byte((guchar)v);
    }
    else if (g_strcmp0(datatype, "uint32") == 0) {
        return g_variant_new_uint32((guint32)g_ascii_strtoull(text, NULL, 10));
    }
    else if (g_strcmp0(datatype, "uint64") == 0) {
        return g_variant_new_uint64(g_ascii_strtoull(text, NULL, 10));
    }
    else if (g_strcmp0(datatype, "int32") == 0) {
        return g_variant_new_int32((gint32)g_ascii_strtoll(text, NULL, 10));
    }
    else if (g_strcmp0(datatype, "float") == 0) {
        return g_variant_new_double(g_ascii_strtod(text, NULL));
    }


    g_warning("Unsupported datatype in string_to_variant: %s", datatype);
    return NULL;
}









static gboolean datatype_input_filter(GtkWidget *widget, GdkEventKey *event, gpointer user_data){
    GuiSetting *setting = user_data;

    /* Always allow navigation & editing keys */
    switch (event->keyval) {
        case GDK_KEY_BackSpace:
        case GDK_KEY_Delete:
        case GDK_KEY_Left:
        case GDK_KEY_Right:
        case GDK_KEY_Tab:
        case GDK_KEY_Home:
        case GDK_KEY_End:
            return FALSE;
    }

    /* Non-character keys */
    if (!event->string || event->string[0] == '\0')
        return TRUE;

    gunichar c = g_utf8_get_char(event->string);

    /* -------- DATATYPE RULES -------- */

    /* char[256] → allow anything printable */
    if (g_strcmp0(setting->datatype, "char[256]") == 0) {
        return !g_unichar_isprint(c);
    }

    /* uint8 / uint32 / uint64 → digits only */
    if (g_strcmp0(setting->datatype, "uint8") == 0 ||
        g_strcmp0(setting->datatype, "uint32") == 0 ||
        g_strcmp0(setting->datatype, "uint64") == 0) {
        return !g_unichar_isdigit(c);
    }

    /* int32 → digits and optional leading '-' */
    if (g_strcmp0(setting->datatype, "int32") == 0) {
        if (g_unichar_isdigit(c))
            return FALSE;

        if (c == '-') {
            const char *text = gtk_entry_get_text(GTK_ENTRY(widget));
            return text[0] != '\0';  // only allow '-' as first char
        }
        return TRUE;
    }

    /* float → digits, one '.' OR one ',', optional leading '-' */
    if (g_strcmp0(setting->datatype, "float") == 0) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(widget));

        /* digits always allowed */
        if (g_unichar_isdigit(c))
            return FALSE;

        /* allow one decimal separator: '.' or ',' */
        if ((c == '.' || c == ',') &&
            !strchr(text, '.') &&
            !strchr(text, ',')) {
            return FALSE;
        }

        /* optional leading '-' */
        if (c == '-' && text[0] == '\0')
            return FALSE;

        return TRUE;  /* block everything else */
    }

    /* uint8_hex → hex digits only */
    if (g_strcmp0(setting->datatype, "uint8_hex") == 0) {
        return !g_unichar_isxdigit(c);
    }

    /* Unknown datatype → block input */
    return TRUE;
}











// Helper to parse a JSON array into GPtrArray of ints
static GPtrArray* parse_options_array(JsonArray *jarr)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    guint n = json_array_get_length(jarr);

    for (guint i = 0; i < n; i++) {
        JsonNode *node = json_array_get_element(jarr, i);

        if (JSON_NODE_HOLDS_VALUE(node)) {
            if (json_node_get_value_type(node) == G_TYPE_STRING) {
                g_ptr_array_add(arr,
                    g_strdup(json_node_get_string(node)));
            } else {
                // numeric → string
                char buf[32];
                snprintf(buf, sizeof(buf), "%" G_GINT64_FORMAT,
                         json_node_get_int(node));
                g_ptr_array_add(arr, g_strdup(buf));
            }
        }
    }
    return arr;
}









static gboolean json_to_int64_checked(JsonNode *node, gint64 *out){
    if (!node || !JSON_NODE_HOLDS_VALUE(node))
        return FALSE;

    if (json_node_get_value_type(node) == G_TYPE_INT64) {
        *out = json_node_get_int(node);
        return TRUE;
    }

    if (json_node_get_value_type(node) == G_TYPE_STRING) {
        char *end = NULL;
        const char *s = json_node_get_string(node);
        gint64 v = g_ascii_strtoll(s, &end, 10);
        if (end && *end == '\0') {
            *out = v;
            return TRUE;
        }
    }

    return FALSE;
}









static gboolean json_to_double_checked(JsonNode *node, gdouble *out){
    if (!node || !JSON_NODE_HOLDS_VALUE(node))
        return FALSE;

    if (json_node_get_value_type(node) == G_TYPE_DOUBLE) {
        *out = json_node_get_double(node);
        return TRUE;
    }

    if (json_node_get_value_type(node) == G_TYPE_STRING) {
        char *end = NULL;
        gdouble v = g_ascii_strtod(json_node_get_string(node), &end);
        if (end && *end == '\0' && isfinite(v)) {
            *out = v;
            return TRUE;
        }
    }

    return FALSE;
}









// Helper to create GVariant for default value based on datatype
static GVariant *parse_default_value(JsonNode *def_node, const char *datatype, const char *handle){
    if (!datatype) return NULL;

    /* ---------- Missing default ---------- */
    if (!def_node) {
        g_warning("Missing default value, using 0 for datatype '%s'while loading JSON entry '%s'", datatype, handle);
        goto fallback;
    }

    /* ---------- uint8 ---------- */
    if (g_strcmp0(datatype, "uint8") == 0) {
        gint64 v;
        if (json_to_int64_checked(def_node, &v) && v >= 0 && v <= 255)
            return g_variant_new_byte((guchar)v);

        g_warning("Invalid uint8 default value encountered while loading JSON entry '%s'", handle);
        goto fallback;
    }

    /* ---------- uint8_hex ---------- */
    if (g_strcmp0(datatype, "uint8_hex") == 0) {
        guint v = 0;
        if (json_node_get_value_type(def_node) == G_TYPE_STRING &&
            sscanf(json_node_get_string(def_node), "%x", &v) == 1 &&
            v <= 0xFF)
            return g_variant_new_byte((guchar)v);

        if (json_to_int64_checked(def_node, (gint64 *)&v) && v <= 0xFF)
            return g_variant_new_byte((guchar)v);

        g_warning("Invalid uint8_hex default value encountered while loading JSON entry '%s'", handle);
        goto fallback;
    }

    /* ---------- uint32 ---------- */
    if (g_strcmp0(datatype, "uint32") == 0) {
        gint64 v;
        if (json_to_int64_checked(def_node, &v) && v >= 0 && v <= G_MAXUINT32)
            return g_variant_new_uint32((guint32)v);

        g_warning("Invalid uint32 default value encountered while loading JSON entry '%s'", handle);
        goto fallback;
    }

    /* ---------- uint64 ---------- */
    if (g_strcmp0(datatype, "uint64") == 0) {
        gint64 v;
        if (json_to_int64_checked(def_node, &v) && v >= 0)
            return g_variant_new_uint64((guint64)v);

        g_warning("Invalid uint64 default value encountered while loading JSON entry '%s'", handle);
        goto fallback;
    }

    /* ---------- int32 ---------- */
    if (g_strcmp0(datatype, "int32") == 0) {
        gint64 v;
        if (json_to_int64_checked(def_node, &v) &&
            v >= G_MININT32 && v <= G_MAXINT32)
            return g_variant_new_int32((gint32)v);

        g_warning("Invalid int32 default value encountered while loading JSON entry '%s'", handle);
        goto fallback;
    }

    /* ---------- float ---------- */
    if (g_strcmp0(datatype, "float") == 0) {
        gdouble v;
        if (json_to_double_checked(def_node, &v))
            return g_variant_new_double(v);

        g_warning("Invalid float default value encountered while loading JSON entry '%s'", handle);
        goto fallback;
    }

    /* ---------- char[256] ---------- */
    if (g_strcmp0(datatype, "char[256]") == 0) {
        if (json_node_get_value_type(def_node) == G_TYPE_STRING)
            return g_variant_new_string(json_node_get_string(def_node));

        g_warning("Invalid string default value encountered while loading JSON entry '%s'", handle);
        goto fallback;
    }

    g_warning("Unsupported datatype '%s' encountered while loading JSON entry '%s'", datatype, handle);
    return NULL;

fallback:
    /* Centralized fallback */
    if (g_strcmp0(datatype, "uint8") == 0)
        return g_variant_new_byte(0);
    if (g_strcmp0(datatype, "uint32") == 0)
        return g_variant_new_uint32(0);
    if (g_strcmp0(datatype, "uint64") == 0)
        return g_variant_new_uint64(0);
    if (g_strcmp0(datatype, "int32") == 0)
        return g_variant_new_int32(0);
    if (g_strcmp0(datatype, "float") == 0)
        return g_variant_new_double(0.0);
    if (g_strcmp0(datatype, "char[256]") == 0)
        return g_variant_new_string("");

    return NULL;
}







GuiConfig* parse_gui_config_from_json(const uint8_t *json_data, size_t json_len) {
    if (!json_data || json_len == 0) return NULL;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;
    //printf("%s", json_data);

    if (!json_parser_load_from_data(parser, (const char*)json_data, json_len, &error)) {
        g_printerr("Failed to parse JSON: %s\n", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_printerr("Root is not an object\n");
        g_object_unref(parser);
        return NULL;
    }

    JsonObject *root_obj = json_node_get_object(root);

    GuiConfig *config = g_new0(GuiConfig, 1);
    config->gui_handle = g_strdup(json_object_get_string_member(root_obj, "gui_handle"));
    //config->settings = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
    config->settings = g_ptr_array_new();  // no free func


    // float_number
    const char *float_str = json_object_get_string_member(root_obj, "float_number");
    config->float_number = g_new(uint32_t, 1);
    *config->float_number = (uint32_t)atoi(float_str);

    // Parse settings array
    JsonArray *settings_arr = json_object_get_array_member(root_obj, "settings");
    guint n = json_array_get_length(settings_arr);
    for (guint i = 0; i < n; i++) {
        JsonObject *sobj = json_node_get_object(json_array_get_element(settings_arr, i));
        GuiSetting *s = g_new0(GuiSetting, 1);

        s->gui_handle = g_strdup(json_object_get_string_member(sobj, "gui_handle"));
        s->microcontroller_handle = g_strdup(json_object_get_string_member(sobj, "microcontroller_handle"));
        s->type = g_strdup(json_object_get_string_member(sobj, "type"));
        s->datatype = g_strdup(json_object_get_string_member(sobj, "datatype"));

        // Default value
        JsonNode *def_node = json_object_get_member(sobj, "default");
        s->default_value = parse_default_value(def_node, s->datatype, s->microcontroller_handle);

        // Options array (optional)
        if (json_object_has_member(sobj, "options")) {
            JsonArray *opt_arr = json_object_get_array_member(sobj, "options");
            s->options = parse_options_array(opt_arr);
        } else {
            s->options = NULL;
        }

        g_ptr_array_add(config->settings, s);
    }

    g_object_unref(parser);
    return config;
}









void free_gui_config(GuiConfig *config) {
    if (!config) return;

    // Free float_number pointer
    if (config->float_number)
        g_free(config->float_number);

    // Free each GuiSetting
    if (config->settings) {
        for (guint i = 0; i < config->settings->len; i++) {
            GuiSetting *s = g_ptr_array_index(config->settings, i);
            if (!s) continue;

            // Free strings
            g_free(s->gui_handle);
            g_free(s->microcontroller_handle);
            g_free(s->type);
            g_free(s->datatype);

            // Free default_value
            if (s->default_value)
                g_variant_unref(s->default_value);

            // Free options array if exists
            if (s->options)
                g_ptr_array_free(s->options, TRUE);

            // Free the setting struct
            g_free(s);
        }

        // Free the GPtrArray holding GuiSetting pointers
        g_ptr_array_free(config->settings, TRUE);
    }

    // Free gui_handle string
    g_free(config->gui_handle);

    // Finally free the GuiConfig struct itself
    g_free(config);
}












static gboolean disable_combo_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data){
    /* Returning TRUE stops the combo box from changing on scroll */
    return TRUE;
}











void create_dynamic_widget(GtkWidget *parent_box, GuiSetting *setting)
{
    GtkWidget *label = gtk_label_new(setting->gui_handle);
    gtk_box_pack_start(GTK_BOX(parent_box), label, FALSE, FALSE, 3);

    GtkWidget *widget = NULL;

    /* ---------- ENTRY ---------- */
    if (g_strcmp0(setting->type, "entry") == 0) {
        widget = gtk_entry_new();

        /* Convert GVariant → text safely */
        char *text =
            variant_to_string(setting->default_value, setting->datatype);
        gtk_entry_set_text(GTK_ENTRY(widget), text);
        g_free(text);

        /* Length constraints */
        if (g_strcmp0(setting->datatype, "uint8_hex") == 0) gtk_entry_set_max_length(GTK_ENTRY(widget), 2);

        /* Attach datatype-aware filter */
        g_signal_connect(widget,
                        "key-press-event",
                        G_CALLBACK(datatype_input_filter),
                        setting);

        g_signal_connect(widget,
                         "changed",
                         G_CALLBACK(dynamic_entry_changed),
                         setting);
    }

    /* ---------- COMBO ---------- */
    else if (g_strcmp0(setting->type, "combo") == 0) {
        widget = gtk_combo_box_text_new();

        /* Populate options */
        for (guint i = 0; i < setting->options->len; i++) {
            const char *opt = g_ptr_array_index(setting->options, i);
            gtk_combo_box_text_append_text(
                GTK_COMBO_BOX_TEXT(widget), opt);
        }

        /* Select default */
        char *def_text =
            variant_to_string(setting->default_value, setting->datatype);

        for (guint i = 0; i < setting->options->len; i++) {
            const char *opt = g_ptr_array_index(setting->options, i);
            if (g_strcmp0(opt, def_text) == 0) {
                gtk_combo_box_set_active(GTK_COMBO_BOX(widget), i);
                break;
            }
        }
        g_free(def_text);

        g_signal_connect(widget,
                     "scroll-event",
                     G_CALLBACK(disable_combo_scroll),
                     NULL);

        g_signal_connect(widget,
                         "changed",
                         G_CALLBACK(dynamic_combo_changed),
                         setting);
    }

    /* ---------- PACK ---------- */

    if (widget) {
        gtk_box_pack_start(GTK_BOX(parent_box), widget, FALSE, FALSE, 3);
        g_object_set_data(G_OBJECT(parent_box),
                          setting->microcontroller_handle,
                          widget);
    }
}







void dynamic_entry_changed(GtkEditable *editable, GuiSetting *setting)
{
    const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
    if (!text) return;

    if (setting->default_value)
        g_variant_unref(setting->default_value);

    setting->default_value =
        string_to_variant(text, setting->datatype);
}







void dynamic_combo_changed(GtkComboBox *combo, GuiSetting *setting)
{
    char *text =
        gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    if (!text) return;

    if (setting->default_value)
        g_variant_unref(setting->default_value);

    setting->default_value =
        string_to_variant(text, setting->datatype);

    g_free(text);
}






/**
 * Parses a JSON file and updates the GuiConfig settings in info.
 * Returns TRUE on success, FALSE on failure.
 */

gboolean parse_settings_from_json(ClientInfo *info, const char *json_path)
{
    if (!info || !json_path || !info->left_settings_box)
        return FALSE;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_file(parser, json_path, &error)) {
        g_warning("Failed to parse JSON file '%s': %s", json_path, error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_warning("Root JSON is not an object");
        g_object_unref(parser);
        return FALSE;
    }

    JsonObject *obj = json_node_get_object(root);

    for (guint i = 0; i < info->settings->settings->len; i++) {
        GuiSetting *setting =
            g_ptr_array_index(info->settings->settings, i);

        if (!json_object_has_member(obj, setting->microcontroller_handle))
            continue;

        JsonNode *node = json_object_get_member(obj, setting->microcontroller_handle);

        setting->default_value = parse_default_value(node, setting->datatype, setting->microcontroller_handle);


        /* Update view */
        GtkWidget *widget = g_object_get_data(G_OBJECT(info->left_settings_box), setting->microcontroller_handle);

        if (!widget) continue;

        char *display = variant_to_string(setting->default_value, setting->datatype);

        if (GTK_IS_ENTRY(widget)) {
            gtk_entry_set_text(GTK_ENTRY(widget), display);
        }
        else if (GTK_IS_COMBO_BOX_TEXT(widget)) {
            /* debug
            g_print("Setting '%s'\n", setting->microcontroller_handle);
            g_print("  display = '%s'\n", display);

            for (guint j = 0; j < setting->options->len; j++) {
                const char *opt = g_ptr_array_index(setting->options, j);

                g_print("    opt[%u] = '%s'\n", j, opt);

                if (g_strcmp0(opt, display) == 0) {
                    g_print("    -> MATCH at index %u\n", j);
                    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), j);
                    break;
                }
            }
            */
            for (guint j = 0; j < setting->options->len; j++) {
                const char *opt = g_ptr_array_index(setting->options, j);
                if (g_strcmp0(opt, display) == 0) {
                    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), j);
                    break;
                }
            }
        }

        g_free(display);
    }

    g_object_unref(parser);
    return TRUE;
}


/*
gboolean parse_settings_from_json(ClientInfo *info, const char *json_path) {
    if (!info || !json_path) return FALSE;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_file(parser, json_path, &error)) {
        g_warning("Failed to parse JSON file '%s': %s",
                  json_path,
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_warning("Root JSON node is not an object");
        g_object_unref(parser);
        return FALSE;
    }

    JsonObject *obj = json_node_get_object(root);

    // ---- Sample rate (required) ----
    if (!json_object_has_member(obj, "sample_rate_hz")) {
        g_warning("JSON must contain 'sample_rate_hz'");
        g_object_unref(parser);
        return FALSE;
    }

    // ---- Ensure sample rate exists ----
    if (!json_object_has_member(obj, "sample_rate_hz")) {
        g_warning("JSON must contain 'sample_rate_hz'");
        g_object_unref(parser);
        return FALSE;
    }

    // ---- Update dynamic GuiSettings ----
    for (guint i = 0; i < info->settings->settings->len; i++) {
        GuiSetting *setting = g_ptr_array_index(info->settings->settings, i);

        if (!json_object_has_member(obj, setting->gui_handle))
            continue;

        JsonNode *node = json_object_get_member(obj, setting->gui_handle);

        // Update the GVariant value based on datatype
        if (g_strcmp0(setting->datatype, "uint8") == 0) {
            if (JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING) {
                // Parse hex string to uint8_t
                const char *hex = json_node_get_string(node);
                uint8_t val = (uint8_t)strtoul(hex, NULL, 16);
                g_variant_ref_sink(setting->default_value = g_variant_new_byte((guchar)val));
            } else {
                // JSON contains numeric value
                uint8_t val = (uint8_t)json_node_get_int(node);
                g_variant_ref_sink(setting->default_value = g_variant_new_byte((guchar)val));
            }
        } else if (g_strcmp0(setting->datatype, "uint32") == 0) {
            uint32_t val = (uint32_t)json_node_get_int(node);
            g_variant_ref_sink(setting->default_value = g_variant_new_uint32(val));
        } else if (g_strcmp0(setting->datatype, "uint64") == 0) {
            uint64_t val = (uint64_t)json_node_get_int(node);
            g_variant_ref_sink(setting->default_value = g_variant_new_uint64(val));
        } else if (g_strcmp0(setting->datatype, "int32") == 0) {
            int32_t val = (int32_t)json_node_get_int(node);
            g_variant_ref_sink(setting->default_value = g_variant_new_int32(val));
        } else if (g_strcmp0(setting->datatype, "float") == 0) {
            double val = json_node_get_double(node);
            g_variant_ref_sink(setting->default_value = g_variant_new_double(val));
        } else if (g_strcmp0(setting->datatype, "char[256]") == 0) {
            const char *val = json_node_get_string(node);
            g_variant_ref_sink(setting->default_value = g_variant_new_string(val));
        }
    }

    g_object_unref(parser);
    return TRUE;
}



*/





JsonNode* parse_json_for_microcontroller(ClientInfo *info) {
    if (!info || !info->settings || !info->settings->settings) return NULL;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    // --- Add duration_seconds first ---
    json_builder_set_member_name(builder, "duration_seconds");
    json_builder_add_int_value(builder, info->duration_seconds);

    // --- Loop over dynamic settings ---
    GuiConfig *config = info->settings;
    for (guint i = 0; i < config->settings->len; i++) {
        GuiSetting *setting = g_ptr_array_index(config->settings, i);
        if (!setting->microcontroller_handle) continue;

        const char *key = setting->microcontroller_handle;

        if (g_strcmp0(setting->datatype, "uint8") == 0) {
            uint8_t val = g_variant_get_byte(setting->default_value);
            json_builder_set_member_name(builder, key);
            json_builder_add_int_value(builder, val);
        } 
        else if (g_strcmp0(setting->datatype, "uint32") == 0) {
            uint32_t val = g_variant_get_uint32(setting->default_value);
            json_builder_set_member_name(builder, key);
            json_builder_add_int_value(builder, val);
        } 
        else if (g_strcmp0(setting->datatype, "uint64") == 0) {
            uint64_t val = g_variant_get_uint64(setting->default_value);
            json_builder_set_member_name(builder, key);
            json_builder_add_int_value(builder, (gint64)val);
        } 
        else if (g_strcmp0(setting->datatype, "int32") == 0) {
            int32_t val = g_variant_get_int32(setting->default_value);
            json_builder_set_member_name(builder, key);
            json_builder_add_int_value(builder, val);
        } 
        else if (g_strcmp0(setting->datatype, "float") == 0) {
            double val = g_variant_get_double(setting->default_value);
            json_builder_set_member_name(builder, key);
            json_builder_add_double_value(builder, val);
        } 
        else if (g_strcmp0(setting->datatype, "char[256]") == 0) {
            const char *val = g_variant_get_string(setting->default_value, NULL);
            json_builder_set_member_name(builder, key);
            json_builder_add_string_value(builder, val);
        }
    }

    json_builder_end_object(builder);
    JsonNode *root = json_builder_get_root(builder);
    g_object_unref(builder);
    return root; // caller must free with json_node_unref()
}











uint32_t get_sample_rate(GuiConfig *config) {
    if (!config || !config->settings) return 0;

    for (guint i = 0; i < config->settings->len; i++) {
        GuiSetting *setting = g_ptr_array_index(config->settings, i);
        if (!setting || !setting->gui_handle || !setting->default_value)
            continue;

        if (g_strcmp0(setting->microcontroller_handle, "sample_rate_hz") == 0) {
            // Use helper to convert variant to string, then parse to uint32
            char *val_str = variant_to_string(setting->default_value, setting->datatype);
            uint32_t val = (uint32_t)strtoul(val_str, NULL, 10);
            g_free(val_str);
            return val;
        }
    }

    g_warning("sample_rate_hz setting not found in get_sample_rate()");
    return 0;
}



