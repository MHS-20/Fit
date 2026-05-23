#include "config.h"
#include <ctype.h>
#include <pwd.h>

/* For strdup on some systems */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

/* Helper function to trim whitespace from both ends of a string */
static char *trim_whitespace(char *str) {
    char *end;
    
    /* Trim leading space */
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    /* Trim trailing space */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

/* Parse a single config file and add entries to the config */
static int parse_config_file(fit_config_t *config, const char *filepath) {
    FILE *fp;
    char line[MAX_CONFIG_LINE];
    char current_section[256] = "";
    
    fp = fopen(filepath, "r");
    if (!fp) {
        return FIT_ERROR; /* File doesn't exist or can't be read */
    }
    
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim_whitespace(line);
        
        /* Skip empty lines and comments */
        if (!*trimmed || *trimmed == '#' || *trimmed == ';') {
            continue;
        }
        
        /* Check for section header [section] */
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
            }
            continue;
        }
        
        /* Parse key = value */
        char *equals = strchr(trimmed, '=');
        if (equals) {
            *equals = '\0';
            char *key = trim_whitespace(trimmed);
            char *value = trim_whitespace(equals + 1);
            
            /* Create new config entry */
            config_entry_t *entry = malloc(sizeof(config_entry_t));
            if (!entry) {
                fclose(fp);
                return FIT_ERROR;
            }
            
            entry->section = strdup(current_section);
            entry->key = strdup(key);
            entry->value = strdup(value);
            entry->next = config->entries;
            config->entries = entry;
        }
    }
    
    fclose(fp);
    return FIT_OK;
}

/* Read configuration from standard locations */
fit_config_t *fitconfig_read(void) {
    fit_config_t *config = malloc(sizeof(fit_config_t));
    if (!config) {
        return NULL;
    }
    
    config->entries = NULL;
    
    /* Determine config directory */
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    char config_path[MAX_PATH];
    
    if (xdg_config_home) {
        snprintf(config_path, sizeof(config_path), "%s/fit/config", xdg_config_home);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        snprintf(config_path, sizeof(config_path), "%s/.config/fit/config", home);
    }
    
    /* Try to read from XDG config location */
    parse_config_file(config, config_path);
    
    /* Try to read from ~/.fitconfig */
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    snprintf(config_path, sizeof(config_path), "%s/.fitconfig", home);
    parse_config_file(config, config_path);
    
    return config;
}

/* Free configuration memory */
void fitconfig_free(fit_config_t *config) {
    if (!config) return;
    
    config_entry_t *entry = config->entries;
    while (entry) {
        config_entry_t *next = entry->next;
        free(entry->section);
        free(entry->key);
        free(entry->value);
        free(entry);
        entry = next;
    }
    
    free(config);
}

/* Get a configuration value */
const char *fitconfig_get(fit_config_t *config, const char *section, const char *key) {
    if (!config) return NULL;
    
    config_entry_t *entry = config->entries;
    while (entry) {
        if (strcmp(entry->section, section) == 0 && strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/* Get user information from config */
char *fitconfig_user_get(fit_config_t *config) {
    const char *name = fitconfig_get(config, "user", "name");
    const char *email = fitconfig_get(config, "user", "email");
    
    if (name && email) {
        char *result = malloc(strlen(name) + strlen(email) + 4); /* " <>\0" */
        if (result) {
            sprintf(result, "%s <%s>", name, email);
        }
        return result;
    }
    
    return NULL;
}
