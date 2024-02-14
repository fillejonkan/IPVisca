#ifndef INCLUSION_GUARD_PARAM_H
#define INCLUSION_GUARD_PARAM_H

#include <axsdk/axparameter.h>

typedef void (*param_callback) (const gchar *value);


void param_init();
int  param_register_callback(const char *param_name, param_callback callback);
const char* param_get(const char* name, char *return_value, int max_size); //Returns the pointer to return_value or NULL if paramter does not exist
int  param_set(const char* name,const char* value);
int  param_set_sys(const char* name,const char* value);
void param_cleanup();

#endif // INCLUSION_GUARD_PARAM_H
