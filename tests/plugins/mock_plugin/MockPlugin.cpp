#include "MockPlugin.h"

// C-linkage entry points required by the plugin loader

extern "C" {

int ea_plugin_api_version() {
    return EA_PLUGIN_API_VERSION;
}

ea::plugin::IPlugin* ea_plugin_create() {
    return new ea::plugin::mock::MockPlugin();
}

void ea_plugin_destroy(ea::plugin::IPlugin* plugin) {
    delete plugin;
}

}  // extern "C"
