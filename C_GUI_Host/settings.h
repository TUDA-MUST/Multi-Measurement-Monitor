#ifndef SETTINGS_H
#define SETTINGS_H

#include <json-glib/json-glib.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

typedef struct _ClientInfo ClientInfo;       //forward declare for settings.h

typedef struct {
    char *gui_handle;
    char *microcontroller_handle;
    char *type;                     // type of GUI Widget ("combo" or "entry")
    char *datatype;                 // "uint8", "uint8_hex", "uint32", "uint64", "int32", "char[256]", "float" are supported
    GVariant *default_value;        // default value in corresponding datatype, can be altered via GUI/ loading a json
    GPtrArray *options;             // for combo boxes
} GuiSetting;

typedef struct {
    char *gui_handle;          //name of client in GUI
    uint32_t *float_number;    //number of floats send per sample
    char **channel_names;         //optional: names for channesl, number equal to float_number, else fallback to CHAN1,...
    GPtrArray *settings;      // array of GuiSetting*, a "sample_rate_hz" setting is required 
} GuiConfig;

GuiConfig* parse_gui_config_from_json(const uint8_t *json_data, size_t json_len);
void free_gui_config(GuiConfig *config);
void create_dynamic_widget(GtkWidget *parent_box, GuiSetting *setting);
void dynamic_entry_changed(GtkEditable *editable, GuiSetting *setting);
void dynamic_combo_changed(GtkComboBox *combo, GuiSetting *setting);
gboolean parse_settings_from_json(ClientInfo *info, const char *json_path);
uint32_t get_sample_rate(GuiConfig *config); 
JsonNode* parse_json_for_microcontroller(ClientInfo *info);
#endif