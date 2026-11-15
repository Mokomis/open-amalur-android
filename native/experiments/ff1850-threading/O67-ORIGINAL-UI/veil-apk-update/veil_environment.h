#ifndef OA_VEIL_ENVIRONMENT_H
#define OA_VEIL_ENVIRONMENT_H

int veil_environment_read(const char *section, const char *key, char value[8]);
int veil_environment_write(const char *section, const char *key, const char *value);
float veil_night_darkness(void);

#endif
