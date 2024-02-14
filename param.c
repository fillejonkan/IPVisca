#include <syslog.h>
#include <stdio.h>
#include "param.h"

#include <glib/gprintf.h>

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERROR(fmt, args...)    { syslog(LOG_CRIT, fmt, ## args); printf(fmt, ## args); }

gchar          *string_application_id = 0;
AXParameter    *handler_application_param = 0;
GHashTable     *table_application_param = 0; 

void
param_init(const char* app_name_ID)
{
  string_application_id = g_strdup( app_name_ID );
  
  if( !handler_application_param )
  {
    handler_application_param = ax_parameter_new( string_application_id, NULL);
  }

  if( !table_application_param ) {
    table_application_param = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  }

}

void
param_cleanup()
{
    if( handler_application_param ) {
        ax_parameter_free( handler_application_param);
    }

    if( table_application_param ) {
        g_hash_table_destroy( table_application_param);
        table_application_param = NULL;
    } 

    handler_application_param = NULL;
}


static void
main_parameter_callback(const gchar *param_name, const gchar *value, gpointer data)
{
  gchar *key;
  gchar *search_key;
  param_callback    user_callback = 0;

  g_printf("Main parameter callback! %s %s\n", param_name, value);

  search_key = g_strrstr_len(param_name,100,".");
  
  if( search_key ) {
	  search_key++;
  } else {
    LOG_ERROR("Camera: Cannot dispatch parameter update %s=%s (internal error)\n", param_name, value);
  }
  
  g_hash_table_lookup_extended(table_application_param,
                               search_key,
                               (gpointer*)&key,
                               (gpointer*)&user_callback);
 
  if( user_callback ) {
     user_callback(value);
  }
  else {
    LOG_ERROR("Camera: Cannot dispatch parameter update %s=%s (internal error)\n", param_name, value);
  }
}

int
param_register_callback(const char* name, param_callback theCallback)
{
    g_printf("Param register callback %s\n", name);


    if( !handler_application_param ) {
        LOG_ERROR("Camera: Cannot register callback for %s (handler not initialized)\n", name);
        return 0;
    }

    if( !ax_parameter_register_callback( handler_application_param, name, main_parameter_callback, NULL, NULL) ) {
        LOG_ERROR("Camera: Cannot register callback for %s (internal error)\n", name);
    }

    g_hash_table_insert(table_application_param, g_strdup(name), (gpointer*)theCallback);

    return 1;
}

const char*
param_get(const char* param_name, char* value, int max_count)
{
  gchar  *param_value = NULL;

  if( !handler_application_param ) {
	  LOG_ERROR("Camera: Cannot get parameter %s (handler not initialized)\n", param_name);
	  return 0;
  }

  if (!ax_parameter_get(handler_application_param, param_name, &param_value, NULL)) {
	  LOG_ERROR("Camera: Cannot get parameter %s (internal errro)\n", param_name);
	  value[0]=0;
	  return 0;
  }
  g_strlcpy(value, param_value, max_count);
  g_free( param_value);
  return value;
}

int
param_set(const char* param_name,const char* value)
{
  gchar *key;
  param_callback    user_callback = 0;
  if( !handler_application_param ) {
    LOG_ERROR("Camera: Cannot set parameter %s=%s (handler not initialized)\n", param_name, value);
    return 0;
  }
  
  if (!ax_parameter_set(handler_application_param, param_name , value, TRUE, NULL)) {
    LOG_ERROR("Camera: Cannot set parameter %s=%s (internal error)\n", param_name, value);
    return 0;
  }

  if( !table_application_param ) {
    LOG_ERROR("Camera: Cannot set parameter %s=%s (internal list)\n", param_name, value);
    return 0;
  }

  g_hash_table_lookup_extended(table_application_param,
                               param_name,
                               (gpointer*)&key,
                               (gpointer*)&user_callback);

  if( user_callback ) {
     user_callback(value);
  }
 
  return 1;
}

int
param_set_sys(const char* name, const char* value)
{
  char fullPath[128];
  sprintf(fullPath,"root.%s",name);
  
  if (!ax_parameter_set(handler_application_param, fullPath , value, TRUE, NULL)) {
    LOG_ERROR("Camera: Cannot set parameter %s=%s (internal error)\n", fullPath, value);
    return 0;
  }
  LOG("Camera parameter %s = %s\n",fullPath, value);
  return 1;
}