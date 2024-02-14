#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "ptz.h"
#include "param.h"

//#define LOG(fmt, args...)   { syslog(LOG_INFO, fmt, ## args); }
#define LOG(fmt, args...)   { g_printf(fmt, ## args); }

/* The number of fractional bits used in fix-point variables */
#define FIXMATH_FRAC_BITS 16

static AXPTZControlQueueGroup *ax_ptz_control_queue_group = NULL;
static gint video_channel = 1;
static AXPTZStatus *unitless_status = NULL;
static AXPTZStatus *unit_status = NULL;
static AXPTZLimits *unitless_limits = NULL;
static AXPTZLimits *unit_limits = NULL;

static gboolean image_rotated = FALSE;

/*********************** DECLARATION OF STATIC FUNCTIONS **********************/

static void rotation_param_callback(const gchar *value);

static gboolean start_continous_movement(fixed_t pan_speed,
                                  fixed_t tilt_speed,
                                  AXPTZMovementPanTiltSpeedSpace pan_tilt_speed_space,
                                  fixed_t zoom_speed, gfloat timeout);

static gboolean move_to_relative_position(fixed_t pan_value,
                                   fixed_t tilt_value,
                                   AXPTZMovementPanTiltSpace pan_tilt_space,
                                   gfloat speed,
                                   AXPTZMovementPanTiltSpeedSpace pan_tilt_speed_space,
                                   fixed_t zoom_value, AXPTZMovementZoomSpace zoom_space);

static gboolean
move_to_absolute_position(fixed_t pan_value,
                          fixed_t tilt_value,
                          AXPTZMovementPanTiltSpace pan_tilt_space,
                          gfloat speed,
                          AXPTZMovementPanTiltSpeedSpace pan_tilt_speed_space,
                          fixed_t zoom_value, AXPTZMovementZoomSpace zoom_space);

static int move_to_home_position();

static int handle_ptdrive(unsigned char *command,
                          gboolean is_absolute,
                          size_t len);

/****************************** /DECLARATION OF STATIC FUNCTIONS **************/

/*
 * Wait for the camera to reach it's position
 */
static gboolean wait_for_camera_movement_to_finish(float target_pan,
                                                   float target_tilt,
                                                   float target_zoom)
{
  //gboolean is_moving = TRUE;
  guint timer = 0;
  guint timeout = 10e6;
  guint usleep_time = 33333;

  float tol = 0.05;

  //GError *local_error = NULL;

  g_printf("Waiting for camera movement to finish, pan=%f, tilt=%f, zoom=%f ....\n",
    target_pan, target_tilt, target_zoom);

  /* Initial sleep to avoid RC */
  //usleep(250000);

  /* Check if target has been reached */
  do {
    struct ptz_status pt;
    gboolean target_reached = TRUE;

    get_ptz_status(&pt);

    g_printf("---- PTZ position pan=%f, tilt=%f, zoom=%f\n", pt.pan, 
        pt.tilt, 
        pt.zoom);

    /* If there is a pan movement, check if it has reached it's goal */
    if (target_pan != AX_PTZ_MOVEMENT_NO_VALUE) {
        if (fabs(target_pan - pt.pan) <= tol) {
           g_printf("Pan position reached\n"); 
        } else {
            target_reached = FALSE;
        }
    } 

    /* If there is a tilt movement, check if it has reached it's goal */
    if (target_tilt != AX_PTZ_MOVEMENT_NO_VALUE) {
        if (fabs(target_tilt - pt.tilt) <= tol) {
           g_printf("Tilt position reached\n"); 
        } else {
            target_reached = FALSE;
        }
    }

    /* If there is a zoom movement, check if it has reached it's goal */
    if (target_zoom != AX_PTZ_MOVEMENT_NO_VALUE) {
        if (fabs(target_zoom - pt.zoom) <= tol) {
           g_printf("Zoom position reached\n"); 
        } else {
            target_reached = FALSE;
        }
    }

    /* If the goal is reached, break out of the loop */
    if (target_reached) {
        break;
    }

    /* Wait a bit for the PTZ to finish moving */
    usleep(usleep_time);
    timer += usleep_time;

  } while (timer < timeout);

#if 0
  if (!(ax_ptz_movement_handler_is_ptz_moving
       (video_channel, &is_moving, &local_error))) {
    g_error_free(local_error);
    return FALSE;
  }

  /* Wait until camera is in position or until we get a timeout */
  while (is_moving && (timer < timeout)) {
    if (!(ax_ptz_movement_handler_is_ptz_moving
         (video_channel, &is_moving, &local_error))) {
      g_error_free(local_error);
      return FALSE;
    }

    struct ptz_status pt;
    get_ptz_status(&pt);

    g_printf("---- PTZ position pan=%f, tilt=%f, zoom=%f\n", pt.pan, pt.tilt, pt.zoom);

    usleep(usleep_time);
    timer += usleep_time;
  }
#endif

  g_printf("Camera movement finished....\n");

  if (timer >= timeout) {
    /* Camera is still moving */
    return FALSE;
  } else {
    /* Camera is in position */
    return TRUE;
  }
}

gboolean get_rotation()
{
  return image_rotated;
}

int get_ptz_status(struct ptz_status *pt)
{
  g_assert(pt);

  AXPTZStatus *l_unit_status = NULL;

#ifdef VERBOSE
  g_printf("Getting PTZ status\n");
#endif

  /* Get the current status (e.g. the current pan/tilt/zoom value/position) */
  if (!(ax_ptz_movement_handler_get_ptz_status(video_channel,
                                               AX_PTZ_MOVEMENT_PAN_TILT_DEGREE,
                                               AX_PTZ_MOVEMENT_ZOOM_UNITLESS,
                                               &l_unit_status,
                                               NULL))) {
    g_printf("Failed to get PTZ status\n");                                                
    return -1;
  }

#ifdef VERBOSE
  g_printf("Got PTZ status\n");
#endif

  pt->pan  = fx_xtof(l_unit_status->pan_value, FIXMATH_FRAC_BITS);
  pt->tilt = fx_xtof(l_unit_status->tilt_value, FIXMATH_FRAC_BITS);
  pt->zoom = fx_xtof(l_unit_status->zoom_value, FIXMATH_FRAC_BITS);
  pt->min_zoom = fx_xtof(unitless_limits->min_zoom_value, FIXMATH_FRAC_BITS);
  pt->max_zoom = fx_xtof(unitless_limits->max_zoom_value, FIXMATH_FRAC_BITS);

#ifdef VERBOSE
  LOG("Status (Unit)\nP %.2f\n", pt->pan);
  LOG("T %.2f\n", pt->tilt);
  LOG("Z %f\n", pt->zoom);
  LOG("Z min %f\n", pt->min_zoom);
  LOG("Z max %f\n", pt->max_zoom);
#endif

  // TODO: Is this handled correctly?
  g_free(l_unit_status);

  return 0;
}

gboolean ptz_init()
{
  GError *local_error = NULL;
  
  /* Create the axptz library */
  if (!(ax_ptz_create(&local_error))) {
    return FALSE;
  }
  
  /* Get the application group from the PTZ control queue */
  if (!(ax_ptz_control_queue_group =
       ax_ptz_control_queue_get_app_group_instance(&local_error))) {
    return FALSE;
  }
  
  /* Get the current status (e.g. the current pan/tilt/zoom value/position) */
  if (!(ax_ptz_movement_handler_get_ptz_status(video_channel,
                                               AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS,
                                               AX_PTZ_MOVEMENT_ZOOM_UNITLESS,
                                               &unitless_status,
                                               &local_error))) {
                                               
    return FALSE;
  }
  LOG("Status (Unitless)\nP %.2f\n", fx_xtof(unitless_status->pan_value, FIXMATH_FRAC_BITS));
  LOG("T %.2f\n", fx_xtof(unitless_status->tilt_value, FIXMATH_FRAC_BITS));
  LOG("Z %f\n", fx_xtof(unitless_status->zoom_value, FIXMATH_FRAC_BITS));
  
  /* Get the current status (e.g. the current pan/tilt/zoom value/position) */
  if (!(ax_ptz_movement_handler_get_ptz_status(video_channel,
                                               AX_PTZ_MOVEMENT_PAN_TILT_DEGREE,
                                               AX_PTZ_MOVEMENT_ZOOM_UNITLESS,
                                               &unit_status,
                                               &local_error))) {
                                               
    return FALSE;
  }
  LOG("Status (Unit)\nP %.2f\n", fx_xtof(unit_status->pan_value, FIXMATH_FRAC_BITS));
  LOG("T %.2f\n", fx_xtof(unit_status->tilt_value, FIXMATH_FRAC_BITS));
  LOG("Z %f\n", fx_xtof(unit_status->zoom_value, FIXMATH_FRAC_BITS));

  /* Get the pan, tilt and zoom limits for the unitless space */
  if ((ax_ptz_movement_handler_get_ptz_limits(video_channel,
                                              AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS,
                                              AX_PTZ_MOVEMENT_ZOOM_UNITLESS,
                                              &unitless_limits,
                                              &local_error))) {
    LOG("Limits (Unitless)\nP %.2f, %.2f\n", fx_xtof(unitless_limits->min_pan_value, FIXMATH_FRAC_BITS), fx_xtof(unitless_limits->max_pan_value, FIXMATH_FRAC_BITS));
    LOG("T %.2f, %.2f\n", fx_xtof(unitless_limits->min_tilt_value, FIXMATH_FRAC_BITS), fx_xtof(unitless_limits->max_tilt_value, FIXMATH_FRAC_BITS));
    LOG("Z %f, %f\n", fx_xtof(unitless_limits->min_zoom_value, FIXMATH_FRAC_BITS), fx_xtof(unitless_limits->max_zoom_value, FIXMATH_FRAC_BITS));
  } else {
    return FALSE;
  }

  /* Get the pan, tilt and zoom limits for the unit (degrees) space */
  if ((ax_ptz_movement_handler_get_ptz_limits(video_channel,
                                              AX_PTZ_MOVEMENT_PAN_TILT_DEGREE,
                                              AX_PTZ_MOVEMENT_ZOOM_UNITLESS,
                                              &unit_limits, &local_error))) {
    LOG("Limits (Unit)\nP %.2f, %.2f\n", fx_xtof(unit_limits->min_pan_value, FIXMATH_FRAC_BITS), fx_xtof(unit_limits->max_pan_value, FIXMATH_FRAC_BITS));
    LOG("T %.2f, %.2f\n", fx_xtof(unit_limits->min_tilt_value, FIXMATH_FRAC_BITS), fx_xtof(unit_limits->max_tilt_value, FIXMATH_FRAC_BITS));
    LOG("Z %f %f\n", fx_xtof(unit_limits->min_zoom_value, FIXMATH_FRAC_BITS), fx_xtof(unit_limits->max_zoom_value, FIXMATH_FRAC_BITS));
  } else {
    return FALSE;
  }

  /* Get video rotation */
  char rotation[100];
  param_get("ImageSource.I0.Sensor.VideoRotation", rotation, 100);

  /* Perform callback once to initiate rotation value */
  rotation_param_callback(rotation);

  /* Listen to parameter changes for rotation.
     TODO: Doesn't seem like I am getting this callback, only for custom defined
     params?
   */
  param_register_callback("ImageSource.I0.Sensor.VideoRotation",
    rotation_param_callback);

  /* Setup anonymous PTZ for focus and iris VAPIX callbacks to work. */
  param_set("root.PTZ.BoaProtPTZOperator", "anonymous");
  
  return TRUE;
}

int move_to_home_position()
{
  return ax_ptz_preset_handler_goto_home(ax_ptz_control_queue_group,
                                         video_channel,
                                         fx_ftox(1.0f, FIXMATH_FRAC_BITS),
                                         AX_PTZ_PRESET_MOVEMENT_UNITLESS,
                                         AX_PTZ_INVOKE_ASYNC, 
                                         NULL,
                                         NULL, 
                                         NULL);
}

static void rotation_param_callback(const gchar *value)
{
    g_printf("Got image rotation value %s\n", value);

    if (strncmp(value, "180", 3) == 0) {
        g_printf("Image is rotated, flip mode in use\n");
        image_rotated = TRUE;
    } else if (strncmp(value, "0", 1) == 0) {
        g_printf("Image is not rotated, flip not mode in use\n");
        image_rotated = FALSE;
    } else {
        g_printf("Unknown value for video rotation, assume not rotated\n");
    }
}

/*
 * Perform camera movement to absolute position
 */
static gboolean
move_to_absolute_position(fixed_t pan_value,
                          fixed_t tilt_value,
                          AXPTZMovementPanTiltSpace pan_tilt_space,
                          gfloat speed,
                          AXPTZMovementPanTiltSpeedSpace pan_tilt_speed_space,
                          fixed_t zoom_value, AXPTZMovementZoomSpace zoom_space)
{
  AXPTZAbsoluteMovement *abs_movement = NULL;
  GError *local_error = NULL;

  /* Set the unit spaces for an absolute movement */
  if ((ax_ptz_movement_handler_set_absolute_spaces
       (pan_tilt_space, pan_tilt_speed_space, zoom_space, &local_error))) {
    /* Create an absolute movement structure */
    if ((abs_movement = ax_ptz_absolute_movement_create(&local_error))) {

      /* Set the pan, tilt and zoom values for the absolute movement */
      if (!(ax_ptz_absolute_movement_set_pan_tilt_zoom(abs_movement,
                                                       pan_value,
                                                       tilt_value,
                                                       fx_ftox(speed,
                                                               FIXMATH_FRAC_BITS),
                                                       zoom_value,
                                                       AX_PTZ_MOVEMENT_NO_VALUE,
                                                       &local_error))) {
        ax_ptz_absolute_movement_destroy(abs_movement, NULL);
        g_error_free(local_error);
        return FALSE;
      }

      /* Perform the absolute movement */
      if (!(ax_ptz_movement_handler_absolute_move(ax_ptz_control_queue_group,
                                                  video_channel,
                                                  abs_movement,
                                                  AX_PTZ_INVOKE_ASYNC, NULL,
                                                  NULL, &local_error))) {
        ax_ptz_absolute_movement_destroy(abs_movement, NULL);
        g_error_free(local_error);
        return FALSE;
      }

      /* Now we don't need the absolute movement structure anymore, destroy it */
      if (!(ax_ptz_absolute_movement_destroy(abs_movement, &local_error))) {
        g_error_free(local_error);
        return FALSE;
      }
    } else {
      g_error_free(local_error);
      return FALSE;
    }
  } else {
    g_error_free(local_error);
    return FALSE;
  }

  return TRUE;
}


/*
 * Perform camera movement to relative position
 */
static gboolean move_to_relative_position(fixed_t pan_value,
                                          fixed_t tilt_value,
                                          AXPTZMovementPanTiltSpace pan_tilt_space,
                                          gfloat speed,
                                          AXPTZMovementPanTiltSpeedSpace pan_tilt_speed_space,
                                          fixed_t zoom_value, AXPTZMovementZoomSpace zoom_space)
{
  AXPTZRelativeMovement *rel_movement = NULL;
  GError *local_error = NULL;

  /* Set the unit spaces for a relative movement */
  if ((ax_ptz_movement_handler_set_relative_spaces
       (pan_tilt_space, pan_tilt_speed_space, zoom_space, &local_error))) {

    /* Create a relative movement structure */
    if ((rel_movement = ax_ptz_relative_movement_create(&local_error))) {

      /* Set the pan, tilt and zoom values for the relative movement */
      if (!(ax_ptz_relative_movement_set_pan_tilt_zoom(rel_movement,
                                                       pan_value,
                                                       tilt_value,
                                                       fx_ftox(speed, FIXMATH_FRAC_BITS),
                                                       zoom_value,
                                                       AX_PTZ_MOVEMENT_NO_VALUE,
                                                       &local_error))) {
        ax_ptz_relative_movement_destroy(rel_movement, NULL);
        g_error_free(local_error);
        return FALSE;
      }

      /* Perform the relative movement */
      if (!(ax_ptz_movement_handler_relative_move(ax_ptz_control_queue_group,
                                                  video_channel,
                                                  rel_movement,
                                                  AX_PTZ_INVOKE_ASYNC, NULL,
                                                  NULL, &local_error))) {
        ax_ptz_relative_movement_destroy(rel_movement, NULL);
        g_error_free(local_error);
        return FALSE;
      }

      /* Now we don't need the relative movement structure anymore, destroy it */
      if (!(ax_ptz_relative_movement_destroy(rel_movement, &local_error))) {
        g_error_free(local_error);
        return FALSE;
      }
    } else {
      g_error_free(local_error);
      return FALSE;
    }
  } else {
    g_error_free(local_error);
    return FALSE;
  }

  return TRUE;
}

/*
 * Perform continous camera movement
 */
static gboolean
start_continous_movement(fixed_t pan_speed,
                         fixed_t tilt_speed,
                         AXPTZMovementPanTiltSpeedSpace pan_tilt_speed_space,
                         fixed_t zoom_speed, gfloat timeout)
{
  AXPTZContinuousMovement *cont_movement = NULL;
  GError *local_error = NULL;

  /* Set the unit spaces for a continous movement */
  if ((ax_ptz_movement_handler_set_continuous_spaces
       (pan_tilt_speed_space, &local_error))) {

    /* Create a continous movement structure */
    if ((cont_movement = ax_ptz_continuous_movement_create(&local_error))) {

      /* Set the pan, tilt and zoom speeds for the continous movement */
      if (!(ax_ptz_continuous_movement_set_pan_tilt_zoom(cont_movement,
                                                         pan_speed,
                                                         tilt_speed,
                                                         zoom_speed,
                                                         fx_ftox(timeout, FIXMATH_FRAC_BITS),
                                                         &local_error))) {
        ax_ptz_continuous_movement_destroy(cont_movement, NULL);
        g_error_free(local_error);
        return FALSE;
      }

      /* Perform the continous movement */
      if (!(ax_ptz_movement_handler_continuous_start(ax_ptz_control_queue_group,
                                                     video_channel,
                                                     cont_movement,
                                                     AX_PTZ_INVOKE_ASYNC, NULL,
                                                     NULL, &local_error))) {
        ax_ptz_continuous_movement_destroy(cont_movement, NULL);
        g_error_free(local_error);
        return FALSE;
      }

      /* Now we don't need the continous movement structure anymore, destroy it */
      if (!(ax_ptz_continuous_movement_destroy(cont_movement, &local_error))) {
        g_error_free(local_error);
        return FALSE;
      }
    } else {
      g_error_free(local_error);
      return FALSE;
    }
  } else {
    g_error_free(local_error);
    return FALSE;
  }

  return TRUE;
}

/*
 * Stop continous camera movement
 */
gboolean stop_continous_movement(gboolean stop_pan_tilt,
                                        gboolean stop_zoom)
{
  GError *local_error = NULL;

  /* Stop the continous movement */
  if (!(ax_ptz_movement_handler_continuous_stop(ax_ptz_control_queue_group,
                                                video_channel,
                                                stop_pan_tilt,
                                                stop_zoom, AX_PTZ_INVOKE_ASYNC,
                                                NULL, NULL, &local_error))) {
    g_error_free(local_error);
    return FALSE;
  }

  return TRUE;
}

static fixed_t translate_speed_zoom(int speed)
{
  return fx_ftox( ((float) CLAMP(speed, 0, 7)) / 7, FIXMATH_FRAC_BITS);
}
/*
 * Translate joystick speed to Axis PTZ speed
 */
fixed_t translate_speed_pt(int speed)
{
  fixed_t abs_speed = fx_ftox( ((float) CLAMP(speed, 0, 17)) / 17, FIXMATH_FRAC_BITS);

  if (image_rotated) {
    return -abs_speed;
  } else {
    return abs_speed;
  }
} 

/*
 * Process received command and move camera accordingly
 */
int process_command(unsigned char* data, int length_data)
{
  //LOGINFO("Received data: %x\n", data);
  //syslog(LOG_INFO, "%s", data);
  unsigned char *command = data;
  int speed_pan;
  int speed_tilt;
  int speed_zoom;

  /* IMG FLIP COMMAND */
  if (command[2] == 0x04 && command[3] == 0x66) {
    unsigned char p = command[4] & 0x0F;

    if (p == 2) {
        param_set("ImageSource.I0.Sensor.VideoRotation", "180");
        rotation_param_callback("180");
    } else if (p == 3) {
        param_set("ImageSource.I0.Sensor.VideoRotation", "0");
        rotation_param_callback("0");
    } else {
        g_printf("Got unknown IMG FLIP command\n");
    }
    
  }
      
  //if autofocus on/off
  if(command[2] == 0x04 && command[3] == 0x38 && command[4] == 0x02) {
    //autofocus ON
    g_printf("Got Focus AUTO Command\n");
    system("curl http://127.0.0.1/axis-cgi/com/ptz.cgi?autofocus=on &");
  } else if (command[2] == 0x04 && command[3] == 0x38 && command[4] == 0x03) {
    //autofocus OFF
    g_printf("Got Focus MANUAL Command\n");
    system("curl http://127.0.0.1/axis-cgi/com/ptz.cgi?autofocus=off &");
  } else if (command[2] == 0x04 && command[3] == 0x48) {
    unsigned int p = command[4] & 0x0F;
    unsigned int q = command[5] & 0x0F;
    unsigned int r = command[6] & 0x0F;
    unsigned int s = command[7] & 0x0F;

    unsigned int Focus = (p << 12) | (q << 8) | (r << 4) | s;

    Focus = CLAMP(Focus, 0x1000, 0xC000);

    long double focus_remapped = 10000 - (10000 *
      (((float) (Focus - 0x1000)) / (0xC000 - 0x1000)));
    focus_remapped = CLAMP(focus_remapped, 1, 9999);

    //ptz_preset_struct.focus = focus_remapped;

    g_printf("Translated focus value %Lf\n", focus_remapped);

    char cmd[100];
    sprintf(cmd, "curl http://127.0.0.1/axis-cgi/com/ptz.cgi?focus=%d &", (int) focus_remapped);

    g_printf("Executing command %s\n", cmd);                  
    system(cmd);
  }

  //if open/close iris (from Cam_Iris or Cam_AE)
  if(command[2] == 0x04 && (command[3] == 0x0B || command[3] == 0x39) && command[4] == 0x00) {
    g_printf("Got iris AUTO  command\n");
    system("curl http://127.0.0.1/axis-cgi/com/ptz.cgi?autoiris=on &");
  } else if (command[2] == 0x04 && command[3] == 0x39 && command[4] == 0x03) {
    system("curl http://127.0.0.1/axis-cgi/com/ptz.cgi?autoiris=off &");
    g_printf("Got iris MANUAL command\n");
  } else if (command[2] == 0x04 && command[3] == 0x4B && command[4] == 0x00 && command[5] == 0x00) {
    unsigned int p = command[6] & 0x0F;
    unsigned int q = command[7] & 0x0F;

    unsigned int F = (p << 4) | q;

    if (F >= 0x11) {
      F = 0x11;
    }
    
    int iris_value = (int) (((float) 10000) / 0x11 ) * F;
    iris_value = CLAMP(iris_value, 1, 9999);

    char cmd[100];
    sprintf(cmd, "curl http://127.0.0.1/axis-cgi/com/ptz.cgi?iris=%d &", iris_value);

    g_printf("Executing command %s\n", cmd);                  
    system(cmd);
  }
  

  //if zoom
  if(command[2] == 0x04 && command[3] == 0x07)
  {

    speed_zoom = (int) command[4] & 0x0f;
    if((command[4] & 0xf0) == 0x20)
    {
      //syslog(LOG_INFO, "Zoom out var");
      if (!(start_continous_movement(AX_PTZ_MOVEMENT_NO_VALUE, //unitless_pos_speed
                               AX_PTZ_MOVEMENT_NO_VALUE, //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               translate_speed_zoom(speed_zoom), 1000.0f))) //zoom
      {
          syslog(LOG_INFO, "Failure, Zoom");
      }
    }
    if((command[4] & 0xf0) == 0x30)
    {
      //syslog(LOG_INFO, "Zoom in var");
      if (!(start_continous_movement(AX_PTZ_MOVEMENT_NO_VALUE, //unitless_pos_speed
                               AX_PTZ_MOVEMENT_NO_VALUE, //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               -translate_speed_zoom(speed_zoom), 1000.0f))) //zoom
      {
          syslog(LOG_INFO, "Failure, Zoom");
      }
      
    }
    if((command[4]) == 0x02)
    {
      //check current zoom setting, adjust if not max, set to max if current + step > max
      //syslog(LOG_INFO, "Zoom in fix");
      if (!(start_continous_movement(AX_PTZ_MOVEMENT_NO_VALUE, //unitless_pos_speed
                               AX_PTZ_MOVEMENT_NO_VALUE, //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               fx_ftox(1.0f, FIXMATH_FRAC_BITS), 1000.0f))) //zoom
      {
          syslog(LOG_INFO, "Failure, Zoom");
      }
    }
    if((command[4]) == 0x03)
    {
      //syslog(LOG_INFO, "Zoom out fix");
      if (!(start_continous_movement(AX_PTZ_MOVEMENT_NO_VALUE, //unitless_pos_speed
                               AX_PTZ_MOVEMENT_NO_VALUE, //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               -fx_ftox(1.0f, FIXMATH_FRAC_BITS), 1000.0f))) //zoom
      {
          syslog(LOG_INFO, "Failure, Zoom");
      }
      
    }
    if((command[4]) == 0x00) //if((command[4] & 0xf0) == 0x00)
    {
      //syslog(LOG_INFO, "Zoom stop");
      if (!(stop_continous_movement(FALSE, TRUE))) 
      {
        syslog(LOG_INFO, "Failure, Zoom");
      }
    }
  /* Direct Zoom */
  } else if (command[2] == 0x04 && command[3] == 0x47) {
    unsigned int p = command[4] & 0x0F;
    unsigned int q = command[5] & 0x0F;
    unsigned int r = command[6] & 0x0F;
    unsigned int s = command[7] & 0x0F;

    unsigned int Z = (p << 12) | (q << 8) | (r << 4) | s;

    g_printf("Got Direct Zoom value %d\n", Z);

    if (Z > 0x4000) {
      Z = 0x4000;
    }

#ifdef VERBOSE
    g_printf("Data Received: ");
    size_t i = 0;
    for (; i < length_data; i++) {
      g_printf("%d=[0x%02x], ", i, command[i]);
    } 
    g_printf("\n");
#endif

    float zoom_unitless_f = 
      (fx_xtof(unitless_limits->max_zoom_value, FIXMATH_FRAC_BITS) / 0x4000)
      * Z; 

    fixed_t api_zoom_val_unitless = fx_ftox(zoom_unitless_f, FIXMATH_FRAC_BITS);

    g_printf("Calculated zoom value %f\n", fx_xtof(api_zoom_val_unitless, FIXMATH_FRAC_BITS));

    move_to_absolute_position(AX_PTZ_MOVEMENT_NO_VALUE,
                          AX_PTZ_MOVEMENT_NO_VALUE,
                          AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS,
                          1.0f,
                          AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                          api_zoom_val_unitless, 
                          AX_PTZ_MOVEMENT_ZOOM_UNITLESS);

    wait_for_camera_movement_to_finish(AX_PTZ_MOVEMENT_NO_VALUE, 
      AX_PTZ_MOVEMENT_NO_VALUE,
      zoom_unitless_f);
  } 
  
  //if pan/tilt
  if(command[2] == 0x06 && command[3] == 0x01)
  {
    
   
    speed_pan = (int) command[4]; 
    speed_tilt = (int) command[5];
    if(command[6] == 0x03 && command[7] == 0x01)
    {
      //syslog(LOG_INFO, "UP");
      if (!(start_continous_movement(AX_PTZ_MOVEMENT_NO_VALUE, //pan unitless_pos_speed
                               translate_speed_pt(speed_tilt), //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               AX_PTZ_MOVEMENT_NO_VALUE, 1000.0f))) //zoom
      {
          syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
    }
    if(command[6] == 0x03 && command[7] == 0x02)
    {
      //syslog(LOG_INFO, "DOWN");
      if (!(start_continous_movement(AX_PTZ_MOVEMENT_NO_VALUE, //pan unitless_pos_speed
                               -translate_speed_pt(speed_tilt), //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               AX_PTZ_MOVEMENT_NO_VALUE, 1000.0f))) //zoom
      {
          syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
    }
    if(command[6] == 0x01 && command[7] == 0x03)
    {
      //syslog(LOG_INFO, "LEFT");
      if (!(start_continous_movement(-translate_speed_pt(speed_pan), //pan unitless_pos_speed
                               AX_PTZ_MOVEMENT_NO_VALUE, //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               AX_PTZ_MOVEMENT_NO_VALUE, 1000.0f))) //zoom
      {
        syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
    }
    if(command[6] == 0x02 && command[7] == 0x03)
    {
      //syslog(LOG_INFO, "RIGHT");
      if (!(start_continous_movement(translate_speed_pt(speed_pan), //pan unitless_pos_speed translate_speed_pt(speed_pan)
                               AX_PTZ_MOVEMENT_NO_VALUE, //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               AX_PTZ_MOVEMENT_NO_VALUE, 1000.0f))) //zoom
      {
        syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
    }
    if(command[6] == 0x01 && command[7] == 0x01)
    {
      //syslog(LOG_INFO, "UPLEFT");
      if (!(start_continous_movement(-translate_speed_pt(speed_pan), //pan unitless_pos_speed
                               translate_speed_pt(speed_tilt), //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               AX_PTZ_MOVEMENT_NO_VALUE, 1000.0f))) //zoom
      {
        syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
    }
    if(command[6] == 0x02 && command[7] == 0x01)
    {
      //syslog(LOG_INFO, "UPRIGHT");
      if (!(start_continous_movement(translate_speed_pt(speed_pan), //pan unitless_pos_speed
                               translate_speed_pt(speed_tilt), //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               AX_PTZ_MOVEMENT_NO_VALUE, 1000.0f))) //zoom
      {
        syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
    }
    if(command[6] == 0x01 && command[7] == 0x02)
    {
      //syslog(LOG_INFO, "DOWNLEFT");
      if (!(start_continous_movement(-translate_speed_pt(speed_pan), //pan unitless_pos_speed
                               -translate_speed_pt(speed_tilt), //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               AX_PTZ_MOVEMENT_NO_VALUE, 1000.0f))) //zoom
      {
        syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
    }
    if(command[6] == 0x02 && command[7] == 0x02)
    {
      //syslog(LOG_INFO, "DOWNRIGHT");
      if (!(start_continous_movement(translate_speed_pt(speed_pan), //pan unitless_pos_speed
                               -translate_speed_pt(speed_tilt), //tilt
                               AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                               AX_PTZ_MOVEMENT_NO_VALUE, 1000.0f))) //zoom
      {
        syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
    }
    if(command[6] == 0x03 && command[7] == 0x03)
    {
      //syslog(LOG_INFO, "STOP");
      /* Stop the continous pan movement */
      if (!(stop_continous_movement(TRUE, FALSE))) 
      {
        syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
    }
  /* Absolute Pan / Tilt movement */
  } else if (command[2] == 0x06 && command[3] == 0x02) {
    

    handle_ptdrive(command, TRUE, length_data);

  } else if (command[2] == 0x06 && command[3] == 0x03) {
    

    handle_ptdrive(command, FALSE, length_data);
  } else if (command[2] == 0x06 && command[3] == 0x04) {
    move_to_home_position();
  } else if (command[2] == 0x06 && command[3]== 0x05) {

    /* TODO: How to handle Reset? */
    if (!(stop_continous_movement(TRUE, FALSE))) 
      {
        syslog(LOG_INFO, "Failure, Pan/Tilt");
      }
  }

  //Set, Recall and delete presets
  if(command[2] == 0x04 && command[3] == 0x3f)
  {
    //delete preset
    if(command[4] == 0x00)
    {
      if (!(ax_ptz_preset_handler_remove_preset_number(ax_ptz_control_queue_group,
                                               video_channel,
                                               command[5] + 1,
                                               NULL))) {
      }

      syslog(LOG_INFO,"Remove preset %d\n", command[5] + 1);
    }
    //set preset
    if(command[4] == 0x01)
    {
      /* Set PTZ preset X to the current camera position */
      if (!(ax_ptz_preset_handler_set_preset_number(ax_ptz_control_queue_group,
                                            video_channel, command[5] + 1,
                                            FALSE, NULL))) {
      
      }

      syslog(LOG_INFO,"Set preset %d\n", command[5] + 1);
    }
    //recall preset
    if(command[4] == 0x02)
    {
      if (!(ax_ptz_preset_handler_goto_preset_number(ax_ptz_control_queue_group,
                                             video_channel,
                                             command[5] + 1,
                                             fx_ftox(1.0f,
                                                     FIXMATH_FRAC_BITS),
                                             AX_PTZ_PRESET_MOVEMENT_UNITLESS,
                                             AX_PTZ_INVOKE_ASYNC, NULL,
                                             NULL, NULL))) {
      }
      syslog(LOG_INFO,"Goto preset %d\n", command[5] + 1);
    }
  }

  return 1;
}

static int handle_ptdrive(unsigned char *command,
                          gboolean is_absolute,
                          size_t len) 
{
  int speed_pan  = CLAMP(command[4], 0, 17);
  int speed_tilt = CLAMP(command[5], 0, 17);

  /* Only one speed is supported so use the maximum specified */
  float api_speed = ((float) MAX(speed_pan, speed_tilt)) / 17;

#ifdef VERBOSE
  g_printf("Data Received: ");
  size_t i = 0;
  for (; i < len; i++) {
    g_printf("%d=[0x%02x], ", i, command[i]);
  }
  g_printf("\n");
#endif

  unsigned int y1 = command[6] & 0x0F;
  unsigned int y2 = command[7] & 0x0F;
  unsigned int y3 = command[8] & 0x0F;
  unsigned int y4 = command[9] & 0x0F;
  unsigned int y5 = command[10] & 0x0F;

  unsigned int z1 = command[11] & 0x0F;
  unsigned int z2 = command[12] & 0x0F;
  unsigned int z3 = command[13] & 0x0F;
  unsigned int z4 = command[14] & 0x0F;

  unsigned int Pan  = (y1 << 16) | (y2 << 12) | (y3 << 8) | (y4 << 4) | y5;
  unsigned int Tilt = (z1 << 12) | (z2 << 8) | (z3 << 4) | z4;


  g_printf("ABS PAN COMMAND ------\n");
  g_printf("Got Pan value 0x%05X=%d\n", Pan, Pan);
  g_printf("Got Tilt value 0x%04X=%d\n", Tilt, Tilt);

  float Pan_deg_f;
  float Tilt_deg_f;

  if (Pan & 0xF0000) {
    Pan_deg_f = -(170.0f - (170.0f / 0x09BDE ) * (Pan - 0xF6422));
  } else {
    Pan_deg_f = (170.0f / 0x09BDE) * Pan; 
  }

  if (Tilt & 0xA000) {
    Tilt_deg_f = -(90.0f - (90.0f / 0x52F8 ) * (Tilt - 0xAD08));
  } else {
    Tilt_deg_f = (90.0f / 0x52F8) * Tilt; 
  }


  if (image_rotated) {
    Tilt_deg_f = -Tilt_deg_f;
  }

  /* Clamp within limits if absolute movement */
  if (is_absolute) {
    float min_pan_value_f = fx_xtof(unit_limits->min_pan_value,
      FIXMATH_FRAC_BITS);
    float max_pan_value_f = fx_xtof(unit_limits->max_pan_value,
      FIXMATH_FRAC_BITS);

    float min_tilt_value_f = fx_xtof(unit_limits->min_tilt_value,
      FIXMATH_FRAC_BITS);
    float max_tilt_value_f = fx_xtof(unit_limits->max_tilt_value,
      FIXMATH_FRAC_BITS);

    g_printf("Coordinates before clamp pan=%f, tilt=%f\n", Pan_deg_f, Tilt_deg_f);

    Pan_deg_f = CLAMP(Pan_deg_f, min_pan_value_f, max_pan_value_f);
    Tilt_deg_f = CLAMP(Tilt_deg_f, min_tilt_value_f, max_tilt_value_f);
  }
  

  g_printf("Translated pan degree value %f\n", Pan_deg_f);
  g_printf("Translated tilt degree value %f\n", Tilt_deg_f);
  g_printf("/ABS PAN COMMAND ------\n");

  fixed_t api_pan_val_degrees = fx_ftox(Pan_deg_f, FIXMATH_FRAC_BITS);
  fixed_t api_tilt_val_degrees = fx_ftox(Tilt_deg_f, FIXMATH_FRAC_BITS);

  if (is_absolute) {
    move_to_absolute_position(api_pan_val_degrees,
                              api_tilt_val_degrees,
                              AX_PTZ_MOVEMENT_PAN_TILT_DEGREE,
                              api_speed,
                              AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                              AX_PTZ_MOVEMENT_NO_VALUE, 
                              AX_PTZ_MOVEMENT_ZOOM_UNITLESS);

  } else {
    move_to_relative_position(api_pan_val_degrees,
                              api_tilt_val_degrees,
                              AX_PTZ_MOVEMENT_PAN_TILT_DEGREE,
                              api_speed,
                              AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS,
                              AX_PTZ_MOVEMENT_NO_VALUE, 
                              AX_PTZ_MOVEMENT_ZOOM_UNITLESS);
  }

  wait_for_camera_movement_to_finish(Pan_deg_f, 
                                     Tilt_deg_f, 
                                     AX_PTZ_MOVEMENT_NO_VALUE);
  
  return 0;
}
