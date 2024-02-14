#ifndef INCLUSION_GUARD_PTZ_H
#define INCLUSION_GUARD_PTZ_H

#include <fixmath.h>
#include <axsdk/axptz.h>

struct ptz_status {
	float pan;
	float tilt;
	float zoom;
	float min_zoom;
	float max_zoom;
};

gboolean stop_continous_movement(gboolean stop_pan_tilt,
                                 gboolean stop_zoom);

gboolean get_rotation();

int get_ptz_status(struct ptz_status *pt);

gboolean ptz_init();

int process_command(unsigned char* data, int length_data);

#endif // INCLUSION_GUARD_PTZ_H
