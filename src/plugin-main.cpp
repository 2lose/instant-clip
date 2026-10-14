#include <obs-frontend-api.h>
#include <obs-module.h>

#include "clip-manager.hpp"
#include "feedback.hpp"
#include "ui/clips-dock.hpp"

OBS_DECLARE_MODULE()

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Instant Clip";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Save the last N seconds of the replay buffer with one hotkey per clip length.";
}

static void frontend_event(enum obs_frontend_event event, void *)
{
	if (ClipManager *m = ClipManager::get())
		m->onFrontendEvent(event);
}

bool obs_module_load(void)
{
	ClipManager::create();
	obs_frontend_add_event_callback(frontend_event, nullptr);
	obs_frontend_add_dock_by_id("instant_clip_dock", "Clips", new ClipsDock());
	blog(LOG_INFO, "[instant-clip] loaded version %s", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	feedback_shutdown();
	ClipManager::destroy();
}
