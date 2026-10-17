#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <obs-module.h>

extern struct obs_source_info pobs_source_info;

void pobs_source_set_rotation(long rotation);
long pobs_source_get_rotation(void);

#ifdef __cplusplus
}
#endif
