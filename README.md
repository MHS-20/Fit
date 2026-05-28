# Fit

An implementation of a version control system, built from scratch. `fit` is heavily inspired by git, and uses the same objects types and organization, however the byte format is not strictly the same, therefore git and fit cannot be swapped seamlessly.

<p align="center">
<img src="fit.png" alt="Logo" width="300"/>
</p>

---

## Prerequisites & Dependencies

`fit` is compiled as a fully native, statically linked binary. It leverages **vcpkg** in Manifest Mode to manage its structural dependencies locally:

* **Compiler**: A modern toolchain supporting full `C++20` (e.g., `g++ >= 11` or `clang >= 13`)
* **Build System**: `CMake (>= 3.20)` and an underlying generator (`Make` or `Ninja`)
* **Package Manager**: `vcpkg` (for isolated, project-specific dependencies)
* **Libraries managed via vcpkg**:
  * `cli11` — Command line processing framework
  * `openssl` — Crypto backend for SHA-1 hashing signatures
  * `zlib` — Object compression routines
  * `curl` — Smart HTTP transfer protocol networking layers
  * `inih` — Configuration INI parser

---

## Installation & Build Automation

It is provided a quality-of-life wrapper `Makefile` at the repository root to simplify daily workspace workflow. Ensure your environment variable `$VCPKG_ROOT` is configured in your shell environment before initializing the compilation sequence.

### Installation

Install the binary executable system-wide (installs to `/usr/local/bin` by default):

```bash
make init
make
make install
```

Configure your user:

```bash
fit config --global user.name "Your Name"
fit config --global user.email "you@example.com"
fit config <key> <value>
```

Uninstall:

```bash
make uninstall

```

## Commands

### `init` — Create a new repository

```bash
fit init [directory]

```

Initializes a `.fit/` directory with the standard object store, refs, HEAD, and a default config. Defaults to the current directory if no path is given.

### `add` — Stage files

```bash
fit add <path> [path ...]

```

Hashes each file as a blob, writes it to the object store, and updates the binary index registry. Re-adding an already-tracked file replaces its index entry.

### `rm` — Remove files

```bash
fit rm <path> [path ...]

```

Removes files from the index and deletes them from the working tree. Raises an error if a path is not tracked or outside the worktree.

### `commit` — Record a snapshot

```bash
fit commit -m "message"

```

Builds a tree from the current index, wraps it in a commit object (with author/committer identities are read from fit config (`$XDG_CONFIG_HOME/fit/config`, `~/.config/fit/config`, or `~/.fitconfig`), with `~/.gitconfig` and `$XDG_CONFIG_HOME/git/config` as automatic fallbacks if no fit identity is found), and advances the active branch ref. Detached HEAD writes directly to `HEAD`.

### `status` — Show working tree state

```bash
fit status

```

Prints three distinct sections:

* **Branch** — Current branch name or detached HEAD hash.
* **Changes to be committed** — Diff between HEAD tree and the index (added, modified, deleted).
* **Changes not staged / Untracked** — Diff between the index and working tree, with `.fitignore` filtering rules applied.

### `log` — Display commit history

```bash
fit log [commit]

```

Walks the parent chain backward starting from the given commit (defaults to `HEAD`).

### `tag` — List or create tags

```bash
fit tag                        # List all tags
fit tag <name> [object]        # Lightweight tag pointing to object (default HEAD)
fit tag -a <name> [object]     # Annotated tag (creates a distinct tag object)

```

### `diff` — Show unstaged changes

```bash
fit diff
```

Hashes each tracked file in the working tree and compares it against the blob stored in the index. Files that match are skipped; changed files are printed in unified diff format (`-`/`+` lines). Only reflects working tree vs index — to see index vs HEAD, use `status`.

### `branch` — List or create branches

```bash
fit branch              # List all branches (* marks the current one)
fit branch <name>       # Create a new branch pointing at the current HEAD
```

Branches are stored as refs under `.fit/refs/heads/`. Creating a branch requires at least one commit. Merge is not supported — use `branch` and `switch` to navigate, not to reconcile histories.

### `switch` — Change the active branch

```bash
fit switch <name>
```

Rewrites `HEAD` to point at the given branch ref. Does not update the working tree or index — you are responsible for ensuring the state is clean before switching.

### `config` — Set configuration values

```bash
fit config --global user.name "Your Name"
fit config --global user.email "you@example.com"
fit config <key> <value>          # Set in the current repository's .fit/config
```

Writes to `$XDG_CONFIG_HOME/fit/config` (or `~/.config/fit/config`) when `--global` is given, or to `.fit/config` for repository-local settings. Key format is `section.key` (e.g. `user.name`).

`commit` reads identity in this order: fit global config → fit repo config → `~/.gitconfig` / `$XDG_CONFIG_HOME/git/config`. If you already have Git configured, `fit commit` will pick up your identity automatically with no extra setup.

### `checkout` — Restore a commit into a directory

```bash
fit checkout <commit> <path>

```

Reads the tree of the given commit and writes it into `<path>`, which must exist and be empty (or not yet exist).

### `show-ref` — List all refs

```bash
fit show-ref

```

Prints every ref under `.fit/refs/` alongside its resolved SHA checksum.

### `rev-parse` — Resolve a name to a SHA

```bash
fit rev-parse [--fit-type blob|commit|tag|tree] <name>

```

Resolves branches, tags, remote refs, short hashes, and `HEAD`. With `--fit-type`, follows tag/commit indirection until the expected type is found.

### `cat-file` — Inspect an object

```bash
fit cat-file <type> <object>

```

Deserializes and prints the raw content of any object. `type` must match the object's actual type (`blob`, `commit`, `tag`, `tree`).

### `hash-object` — Hash a file into the object store

```bash
fit hash-object [-t type] [-w] <file>

```

Computes the SHA-1 of the file content wrapped in a Git object header. `-w` writes it to `.fit/objects/`. Defaults to type `blob`.

### `ls-tree` — Print a tree object

```bash
fit ls-tree [-r] <tree>

```

Lists the entries of a tree object. `-r` recurses down into subtrees.

### `ls-files` — List staged files

```bash
fit ls-files [--verbose]

```

Prints every file currently tracked in the index. `--verbose` shows the full index entry data: blob SHA, permissions, timestamps, device, inode, uid/gid, and bit flags.

### `check-ignore` — Test paths against ignore rules

```bash
fit check-ignore <path> [path ...]

```

Prints each path that would be ignored. Checks scoped `.fitignore` files first, then absolute rules from `.fit/info/exclude` and the global config.

### `push` — Push to a remote

```bash
fit push <url>
fit push <url> -u <username> -p <token>

```

Pushes the local master/main branch to a remote Git server over HTTPS. Credentials are prompted interactively if `-u` and `-p` are not provided. For GitHub, use a **Personal Access Token** as the password.
