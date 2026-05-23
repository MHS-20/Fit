#include "commands.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
  fprintf(stderr, "usage: fit <command> [args]\n"
                  "\n"
                  "Commands:\n"
                  "  init [path]              Initialize a new repository\n"
                  "  cat-file <type> <obj>    Display object contents\n"
                  "  hash-object [-t type] [-w] <file>\n"
                  "  log [commit]             Show commit graph\n"
                  "  ls-tree [-r] <tree>      List tree contents\n"
                  "  checkout <commit> <dir>  Checkout commit into directory\n"
                  "  show-ref                 List all refs\n"
                  "  tag [-a] [name] [obj]    Create or list tags\n"
                  "  rev-parse [--fit-type T] <name>\n"
                  "  ls-files [--verbose]     List staged files\n"
                  "  check-ignore <path...>   Check ignore rules\n"
                  "  status                   Show working tree status\n"
                  "  rm <path...>             Remove files from index\n"
                  "  add <path...>            Add files to index\n"
                  "  commit -m <msg>          Record changes\n"
                  "  push <url> [-u user] [-p pass]\n");
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    usage();
    return 1;
  }

  const char *cmd = argv[1];

  if (strcmp(cmd, "init") == 0) {
    return cmd_init(argc > 2 ? argv[2] : ".");

  } else if (strcmp(cmd, "cat-file") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: fit cat-file <type> <object>\n");
      return 1;
    }
    return cmd_cat_file(argv[2], argv[3]);

  } else if (strcmp(cmd, "hash-object") == 0) {
    const char *type = "blob";
    bool write = false;
    const char *path = NULL;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
        type = argv[++i];
      } else if (strcmp(argv[i], "-w") == 0) {
        write = true;
      } else {
        path = argv[i];
      }
    }
    if (!path) {
      fprintf(stderr, "usage: fit hash-object [-t type] [-w] <file>\n");
      return 1;
    }
    return cmd_hash_object(type, write, path);

  } else if (strcmp(cmd, "log") == 0) {
    return cmd_log(argc > 2 ? argv[2] : "HEAD");

  } else if (strcmp(cmd, "ls-tree") == 0) {
    bool recursive = false;
    const char *tree = NULL;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-r") == 0)
        recursive = true;
      else
        tree = argv[i];
    }
    if (!tree) {
      fprintf(stderr, "usage: fit ls-tree [-r] <tree>\n");
      return 1;
    }
    return cmd_ls_tree(recursive, tree);

  } else if (strcmp(cmd, "checkout") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: fit checkout <commit> <path>\n");
      return 1;
    }
    return cmd_checkout(argv[2], argv[3]);

  } else if (strcmp(cmd, "show-ref") == 0) {
    return cmd_show_ref();

  } else if (strcmp(cmd, "tag") == 0) {
    bool create_obj = false;
    const char *name = NULL, *object = NULL;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "-a") == 0)
        create_obj = true;
      else if (!name)
        name = argv[i];
      else
        object = argv[i];
    }
    return cmd_tag(create_obj, name, object);

  } else if (strcmp(cmd, "rev-parse") == 0) {
    const char *type = NULL, *name = NULL;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--fit-type") == 0 && i + 1 < argc) {
        type = argv[++i];
      } else {
        name = argv[i];
      }
    }
    if (!name) {
      fprintf(stderr, "usage: fit rev-parse [--fit-type T] <name>\n");
      return 1;
    }
    return cmd_rev_parse(type, name);

  } else if (strcmp(cmd, "ls-files") == 0) {
    bool verbose = false;
    for (int i = 2; i < argc; i++)
      if (strcmp(argv[i], "--verbose") == 0)
        verbose = true;
    return cmd_ls_files(verbose);

  } else if (strcmp(cmd, "check-ignore") == 0) {
    if (argc < 3) {
      fprintf(stderr, "usage: fit check-ignore <path...>\n");
      return 1;
    }
    return cmd_check_ignore(argv + 2, argc - 2);

  } else if (strcmp(cmd, "status") == 0) {
    return cmd_status();

  } else if (strcmp(cmd, "rm") == 0) {
    if (argc < 3) {
      fprintf(stderr, "usage: fit rm <path...>\n");
      return 1;
    }
    return cmd_rm(argv + 2, argc - 2);

  } else if (strcmp(cmd, "add") == 0) {
    if (argc < 3) {
      fprintf(stderr, "usage: fit add <path...>\n");
      return 1;
    }
    return cmd_add(argv + 2, argc - 2);

  } else if (strcmp(cmd, "commit") == 0) {
    const char *message = NULL;
    for (int i = 2; i < argc; i++)
      if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
        message = argv[++i];
    if (!message) {
      fprintf(stderr, "usage: fit commit -m <message>\n");
      return 1;
    }
    return cmd_commit(message);

  } else if (strcmp(cmd, "push") == 0) {
    if (argc < 3) {
      fprintf(stderr, "usage: fit push <url> [-u user] [-p pass]\n");
      return 1;
    }
    const char *remote = argv[2];
    const char *username = NULL, *password = NULL;
    for (int i = 3; i < argc; i++) {
      if (strcmp(argv[i], "-u") == 0 && i + 1 < argc)
        username = argv[++i];
      else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        password = argv[++i];
    }
    return cmd_push(remote, username, password);

  } else {
    fprintf(stderr, "fit: unknown command '%s'\n", cmd);
    usage();
    return 1;
  }
}
