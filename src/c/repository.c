#include "repository.h"
#include <stdarg.h>
#include <libgen.h>
#include <dirent.h>
#include <ctype.h>

/* For strdup and realpath on some systems */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

/* Helper function to build a path by joining components */
static char *path_join(const char *base, va_list args) {
    char result[MAX_PATH];
    char *component;
    
    strncpy(result, base, sizeof(result) - 1);
    result[sizeof(result) - 1] = '\0';
    
    while ((component = va_arg(args, char *)) != NULL) {
        size_t len = strlen(result);
        if (len > 0 && result[len - 1] != '/') {
            strncat(result, "/", sizeof(result) - len - 1);
        }
        strncat(result, component, sizeof(result) - strlen(result) - 1);
    }
    
    return strdup(result);
}

/* Helper to create directories recursively */
static int mkdir_recursive(const char *path) {
    char tmp[MAX_PATH];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return FIT_ERROR;
            }
            *p = '/';
        }
    }
    
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return FIT_ERROR;
    }
    
    return FIT_OK;
}

/* Check if directory is empty */
static bool is_dir_empty(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return true;
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        count++;
        break;
    }
    
    closedir(dir);
    return count == 0;
}

/* Create a new repository object */
fit_repository_t *fit_repository_new(const char *path, bool force) {
    fit_repository_t *repo = malloc(sizeof(fit_repository_t));
    if (!repo) {
        return NULL;
    }
    
    /* Resolve to absolute path */
    char resolved_path[MAX_PATH];
    if (!realpath(path, resolved_path)) {
        if (force) {
            strncpy(resolved_path, path, sizeof(resolved_path) - 1);
            resolved_path[sizeof(resolved_path) - 1] = '\0';
        } else {
            free(repo);
            fprintf(stderr, "Path does not exist: %s\n", path);
            return NULL;
        }
    }
    
    repo->worktree = strdup(resolved_path);
    
    char fitdir_path[MAX_PATH];
    snprintf(fitdir_path, sizeof(fitdir_path), "%s/.fit", resolved_path);
    repo->fitdir = strdup(fitdir_path);
    
    /* Check if .fit directory exists */
    struct stat st;
    if (!force && (stat(repo->fitdir, &st) != 0 || !S_ISDIR(st.st_mode))) {
        fprintf(stderr, "Not a Fit repository: %s\n", path);
        free(repo->worktree);
        free(repo->fitdir);
        free(repo);
        return NULL;
    }
    
    /* Read config file */
    char config_path[MAX_PATH];
    snprintf(config_path, sizeof(config_path), "%s/config", repo->fitdir);
    
    if (stat(config_path, &st) == 0) {
        repo->conf = malloc(sizeof(fit_config_t));
        if (repo->conf) {
            repo->conf->entries = NULL;
            
            FILE *fp = fopen(config_path, "r");
            if (fp) {
                /* Simple config parsing - reuse logic from config.c */
                char line[MAX_CONFIG_LINE];
                char current_section[256] = "";
                
                while (fgets(line, sizeof(line), fp)) {
                    char *trimmed = line;
                    while (isspace(*trimmed)) trimmed++;
                    
                    if (!*trimmed || *trimmed == '#' || *trimmed == ';') continue;
                    
                    if (*trimmed == '[') {
                        char *end = strchr(trimmed, ']');
                        if (end) {
                            *end = '\0';
                            strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
                        }
                        continue;
                    }
                    
                    char *equals = strchr(trimmed, '=');
                    if (equals) {
                        *equals = '\0';
                        char *key = trimmed;
                        char *value = equals + 1;
                        
                        /* Trim whitespace */
                        while (isspace(*key)) key++;
                        char *key_end = key + strlen(key) - 1;
                        while (key_end > key && isspace(*key_end)) *key_end-- = '\0';
                        
                        while (isspace(*value)) value++;
                        char *value_end = value + strlen(value) - 1;
                        while (value_end > value && isspace(*value_end)) *value_end-- = '\0';
                        
                        config_entry_t *entry = malloc(sizeof(config_entry_t));
                        if (entry) {
                            entry->section = strdup(current_section);
                            entry->key = strdup(key);
                            entry->value = strdup(value);
                            entry->next = repo->conf->entries;
                            repo->conf->entries = entry;
                        }
                    }
                }
                fclose(fp);
            }
        }
    } else if (!force) {
        fprintf(stderr, "Configuration file missing\n");
        free(repo->worktree);
        free(repo->fitdir);
        free(repo);
        return NULL;
    } else {
        repo->conf = NULL;
    }
    
    /* Validate repository format version */
    if (!force && repo->conf) {
        const char *version_str = fitconfig_get(repo->conf, "core", "repositoryformatversion");
        if (version_str) {
            int version = atoi(version_str);
            if (version != 0) {
                fprintf(stderr, "Unsupported repositoryformatversion: %d\n", version);
                fit_repository_free(repo);
                return NULL;
            }
        }
    }
    
    return repo;
}

/* Free repository memory */
void fit_repository_free(fit_repository_t *repo) {
    if (!repo) return;
    
    free(repo->worktree);
    free(repo->fitdir);
    if (repo->conf) {
        fitconfig_free(repo->conf);
    }
    free(repo);
}

/* Build a path relative to the .fit directory */
char *repo_path(fit_repository_t *repo, ...) {
    va_list args;
    va_start(args, repo);
    char *result = path_join(repo->fitdir, args);
    va_end(args);
    return result;
}

/* Build a file path, optionally creating parent directories */
char *repo_file(fit_repository_t *repo, bool mkdir, ...) {
    va_list args, args_copy;
    va_start(args, mkdir);
    
    /* Count arguments to separate parent dirs from filename */
    va_copy(args_copy, args);
    int count = 0;
    while (va_arg(args_copy, char *) != NULL) count++;
    va_end(args_copy);
    
    if (count == 0) {
        va_end(args);
        return NULL;
    }
    
    /* Build parent directory path */
    if (mkdir && count > 1) {
        va_list parent_args;
        va_start(parent_args, mkdir);
        
        char parent_path[MAX_PATH];
        snprintf(parent_path, sizeof(parent_path), "%s", repo->fitdir);
        
        for (int i = 0; i < count - 1; i++) {
            char *component = va_arg(parent_args, char *);
            strncat(parent_path, "/", sizeof(parent_path) - strlen(parent_path) - 1);
            strncat(parent_path, component, sizeof(parent_path) - strlen(parent_path) - 1);
        }
        va_end(parent_args);
        
        mkdir_recursive(parent_path);
    }
    
    /* Build full path */
    char *result = path_join(repo->fitdir, args);
    va_end(args);
    
    return result;
}

/* Build a directory path, optionally creating it */
char *repo_dir(fit_repository_t *repo, bool mkdir, ...) {
    va_list args;
    va_start(args, mkdir);
    char *path = path_join(repo->fitdir, args);
    va_end(args);
    
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return path;
        } else {
            fprintf(stderr, "Not a directory: %s\n", path);
            free(path);
            return NULL;
        }
    }
    
    if (mkdir) {
        if (mkdir_recursive(path) == FIT_OK) {
            return path;
        } else {
            free(path);
            return NULL;
        }
    }
    
    free(path);
    return NULL;
}

/* Create a new repository at the given path */
fit_repository_t *repo_create(const char *path) {
    fit_repository_t *repo = fit_repository_new(path, true);
    if (!repo) {
        return NULL;
    }
    
    struct stat st;
    
    /* Check if worktree exists */
    if (stat(repo->worktree, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "%s is not a directory!\n", path);
            fit_repository_free(repo);
            return NULL;
        }
        
        /* Check if .fit already exists and is not empty */
        if (stat(repo->fitdir, &st) == 0 && !is_dir_empty(repo->fitdir)) {
            printf("%s repo already initialized\n", path);
            return repo;
        }
    } else {
        /* Create worktree directory */
        if (mkdir_recursive(repo->worktree) != FIT_OK) {
            fprintf(stderr, "Failed to create directory: %s\n", repo->worktree);
            fit_repository_free(repo);
            return NULL;
        }
    }
    
    /* Create directory structure */
    char *branches = repo_dir(repo, true, "branches", NULL);
    free(branches);
    
    char *objects = repo_dir(repo, true, "objects", NULL);
    free(objects);
    
    char *refs_tags = repo_dir(repo, true, "refs", "tags", NULL);
    free(refs_tags);
    
    char *refs_heads = repo_dir(repo, true, "refs", "heads", NULL);
    free(refs_heads);
    
    /* Create description file */
    char *desc_path = repo_file(repo, false, "description", NULL);
    if (desc_path) {
        FILE *fp = fopen(desc_path, "w");
        if (fp) {
            fprintf(fp, "Unnamed repository; edit this file 'description' to name the repository.\n");
            fclose(fp);
        }
        free(desc_path);
    }
    
    /* Create HEAD file */
    char *head_path = repo_file(repo, false, "HEAD", NULL);
    if (head_path) {
        FILE *fp = fopen(head_path, "w");
        if (fp) {
            fprintf(fp, "ref: refs/heads/master\n");
            fclose(fp);
        }
        free(head_path);
    }
    
    /* Create config file */
    char *config_path = repo_file(repo, false, "config", NULL);
    if (config_path) {
        FILE *fp = fopen(config_path, "w");
        if (fp) {
            fprintf(fp, "[core]\n");
            fprintf(fp, "\trepositoryformatversion = 0\n");
            fprintf(fp, "\tfilemode = false\n");
            fprintf(fp, "\tbare = false\n");
            fclose(fp);
        }
        free(config_path);
    }
    
    return repo;
}

/* Find a repository starting from the given path */
fit_repository_t *repo_find(const char *path, bool required) {
    char resolved_path[MAX_PATH];
    
    if (!realpath(path ? path : ".", resolved_path)) {
        if (required) {
            fprintf(stderr, "No git directory.\n");
        }
        return NULL;
    }
    
    /* Check if current directory has .fit */
    char fitdir_path[MAX_PATH];
    snprintf(fitdir_path, sizeof(fitdir_path), "%s/.fit", resolved_path);
    
    struct stat st;
    if (stat(fitdir_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return fit_repository_new(resolved_path, false);
    }
    
    /* Try parent directory */
    char parent_path[MAX_PATH];
    snprintf(parent_path, sizeof(parent_path), "%s/..", resolved_path);
    
    char parent_resolved[MAX_PATH];
    if (!realpath(parent_path, parent_resolved)) {
        if (required) {
            fprintf(stderr, "No git directory.\n");
        }
        return NULL;
    }
    
    /* Check if we've reached the root */
    if (strcmp(resolved_path, parent_resolved) == 0) {
        if (required) {
            fprintf(stderr, "No git directory.\n");
        }
        return NULL;
    }
    
    return repo_find(parent_resolved, required);
}
