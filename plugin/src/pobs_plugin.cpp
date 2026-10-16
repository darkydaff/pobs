#include <obs-module.h>
#include <obs-frontend-api.h>
#include "source/pobs_source.h"
#include "ui/pobs_dock.hpp"
#include "pobs_service.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("pobs", "en-US")

static void on_frontend_event(enum obs_frontend_event event, void *private_data) {
    (void)private_data;
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        blog(LOG_INFO, "[POBS] Frontend loading finished, registering POBS dock...");
        pobs::register_pobs_dock();
    }
}

bool obs_module_load(void) {
    blog(LOG_INFO, "[POBS] Loading POBS Pixel Camera plugin (linux-v4l2 style direct H.264 engine)...");

    // Register native OBS camera source (based on linux-v4l2 architecture)
    obs_register_source(&pobs_source_info);

    // Register event callback to safely initialize Qt dock when frontend is loaded
    obs_frontend_add_event_callback(on_frontend_event, nullptr);

    blog(LOG_INFO, "[POBS] Plugin loaded successfully. Source 'pobs_source' registered.");
    return true;
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);
    blog(LOG_INFO, "[POBS] Unloading POBS plugin...");
}
