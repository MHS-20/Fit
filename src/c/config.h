#ifndef FIT_CONFIG_H
#define FIT_CONFIG_H

#include "fit.h"

/* Simple key-value pair for configuration */
typedef struct config_entry {
    char *section;
    char *key;
    char *value;
    struct config_entry *next;
} config_entry_t;

/* Configuration structure */
typedef struct fit_config {
    config_entry_t *entries;
} fit_config_t;

/* Function prototypes */
fit_config_t *fitconfig_read(void);
void fitconfig_free(fit_config_t *config);
const char *fitconfig_get(fit_config_t *config, const char *section, const char *key);
char *fitconfig_user_get(fit_config_t *config);

#endif /* FIT_CONFIG_H */
