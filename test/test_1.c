#include "repository.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("=== Fit Batch 1 Test ===\n\n");
    
    /* Test 1: Read configuration */
    printf("Test 1: Reading configuration...\n");
    fit_config_t *config = fitconfig_read();
    if (config) {
        printf("  Config loaded successfully\n");
        
        char *user = fitconfig_user_get(config);
        if (user) {
            printf("  User: %s\n", user);
            free(user);
        } else {
            printf("  No user configured\n");
        }
        
        fitconfig_free(config);
    } else {
        printf("  Failed to load config\n");
    }
    printf("\n");
    
    /* Test 2: Create a new repository */
    printf("Test 2: Creating a new repository...\n");
    const char *test_path = argc > 1 ? argv[1] : "/tmp/test_fit_repo";
    
    /* Clean up if exists */
    char cleanup_cmd[MAX_PATH + 20];
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf %s", test_path);
    system(cleanup_cmd);
    
    fit_repository_t *repo = repo_create(test_path);
    if (repo) {
        printf("  Repository created at: %s\n", repo->worktree);
        printf("  .fit directory: %s\n", repo->fitdir);
        
        /* Verify structure */
        char *objects_dir = repo_dir(repo, false, "objects", NULL);
        if (objects_dir) {
            printf("  ✓ objects/ directory exists\n");
            free(objects_dir);
        }
        
        char *refs_heads = repo_dir(repo, false, "refs", "heads", NULL);
        if (refs_heads) {
            printf("  ✓ refs/heads/ directory exists\n");
            free(refs_heads);
        }
        
        char *head_file = repo_file(repo, false, "HEAD", NULL);
        if (head_file) {
            FILE *fp = fopen(head_file, "r");
            if (fp) {
                char line[256];
                if (fgets(line, sizeof(line), fp)) {
                    printf("  ✓ HEAD: %s", line);
                }
                fclose(fp);
            }
            free(head_file);
        }
        
        fit_repository_free(repo);
    } else {
        printf("  Failed to create repository\n");
        return 1;
    }
    printf("\n");
    
    /* Test 3: Find the repository */
    printf("Test 3: Finding the repository...\n");
    char subdir_path[MAX_PATH];
    snprintf(subdir_path, sizeof(subdir_path), "%s/subdir", test_path);
    mkdir(subdir_path, 0755);
    
    fit_repository_t *found_repo = repo_find(subdir_path, true);
    if (found_repo) {
        printf("  ✓ Found repository from subdirectory\n");
        printf("  Repository root: %s\n", found_repo->worktree);
        fit_repository_free(found_repo);
    } else {
        printf("  Failed to find repository\n");
        return 1;
    }
    printf("\n");
    
    /* Test 4: Test repo_path and repo_file */
    printf("Test 4: Testing path functions...\n");
    repo = fit_repository_new(test_path, false);
    if (repo) {
        char *obj_path = repo_path(repo, "objects", "ab", "cdef1234", NULL);
        if (obj_path) {
            printf("  Object path: %s\n", obj_path);
            free(obj_path);
        }
        
        char *new_dir = repo_dir(repo, true, "test", "nested", NULL);
        if (new_dir) {
            printf("  ✓ Created nested directory: %s\n", new_dir);
            free(new_dir);
        }
        
        fit_repository_free(repo);
    }
    printf("\n");
    
    printf("=== All Batch 1 tests passed! ===\n");
    return 0;
}
