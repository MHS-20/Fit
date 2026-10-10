# Fit

An implementation of a version control system, built from scratch. `fit` is heavily inspired by git and writes git's on-disk formats byte for byte: loose objects, trees, commits, tags, the index (version 2, with its trailing checksum) and packfiles. Exposing a `.fit` directory as `.git` lets real `git log`, `git fsck` or `git status` read it, and the test suite checks exactly that.

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

### Tests

```bash
make test
```

Runs the suite in `tests/` through CTest, one entry per `TEST(name)`. Most tests build something with fit and ask the real `git` binary (which must be on `PATH`) whether it agrees: `git fsck`, `git log`, `git write-tree`, `git index-pack` and so on. `remote_live_push_to_github` pushes to GitHub and is skipped unless `FIT_TEST_GITHUB_TOKEN` and `FIT_TEST_GITHUB_REPO=<user>/<empty-repo>` are set.

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

Hashes each file as a blob, writes it to the object store, and updates the binary index registry. Re-adding an already-tracked file replaces its index entry. Symlinks are stored as links (their target is the blob), and the executable bit is kept: files are recorded as `100644` or `100755`, like git. A path that doesn't exist is an error.

### `rm` — Remove files

```bash
fit rm <path> [path ...]

```

Removes files from the index and deletes them from the working tree. Raises an error, before changing anything, if a path is not tracked or outside the worktree.

### `commit` — Record a snapshot

```bash
fit commit -m "message"

```

Builds a tree from the current index, wraps it in a commit object, and advances the active branch ref; a detached HEAD writes directly to `HEAD`. Author and committer are `Name <email>` plus the current timestamp and timezone. The identity is read from fit config (`$XDG_CONFIG_HOME/fit/config`, `~/.config/fit/config`, or `~/.fitconfig`), with `~/.gitconfig` and `$XDG_CONFIG_HOME/git/config` as automatic fallbacks if no fit identity is found. Refuses with "Nothing to commit" when the tree is identical to the parent's (or the index is empty on the first commit). Refs, `HEAD`, the index and objects are written to a temporary file and renamed into place, so a crash never leaves a half-written file.

### `status` — Show working tree state

```bash
fit status

```

Prints three distinct sections:

* **Branch** — Current branch name or detached HEAD hash.
* **Changes to be committed** — Diff between HEAD tree and the index (added, modified, deleted).
* **Changes not staged / Untracked** — Diff between the index and working tree, with `.fitignore` filtering rules applied. A file whose size and modification time (to the nanosecond) match its index entry is taken as unchanged without being read; otherwise its content is re-hashed, so touching a file without editing it doesn't report it as modified.

### `log` — Display commit history

```bash
fit log [commit]

```

Walks the first-parent chain backward starting from the given commit (defaults to `HEAD`).

### `tag` — List or create tags

```bash
fit tag                        # List all tags
fit tag <name> [object]        # Lightweight tag pointing to object (default HEAD)
fit tag -a -m <msg> <name> [object]  # Annotated tag (a tag object with a tagger and message)

```

### `diff` — Show unstaged changes

```bash
fit diff
```

Hashes each tracked file in the working tree (without storing anything) and compares it against the blob stored in the index. Files that match are skipped; changed files are printed line by line with `-`/`+` tags, and a last line without a trailing newline is marked `\ No newline at end of file`. The comparison walks both files with two cursors and doesn't look ahead, so the output is complete but not always minimal. Only reflects working tree vs index — to see index vs HEAD, use `status`.

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

Reads the tree of the given commit and writes it into `<path>`, which must be empty or not yet exist. Executable bits and symlinks are restored; a submodule entry becomes an empty directory. Neither `HEAD` nor the index is changed.

### `show-ref` — List all refs

```bash
fit show-ref

```

Prints every ref under `.fit/refs/` alongside its resolved SHA checksum.

### `rev-parse` — Resolve a name to a SHA

```bash
fit rev-parse [--fit-type blob|commit|tag|tree] <name>

```

Resolves `HEAD`, short or full hashes, tags and branches, in that order. A short hash that matches several objects is an error, unless `--fit-type` singles one out. With `--fit-type`, annotated tags are followed to their object and commits to their tree until the expected type is found; any other type mismatch is an error.

### `cat-file` — Inspect an object

```bash
fit cat-file <type> <object>

```

Prints the content of an object: blobs, commits and tags as stored, trees in `ls-tree` format. `type` must match the object's actual type (`blob`, `commit`, `tag`, `tree`), or be reachable from it as with `rev-parse --fit-type`.

### `hash-object` — Hash a file into the object store

```bash
fit hash-object [-t type] [-w] <file>

```

Computes the SHA-1 of the file content wrapped in a Git object header. Only `-w` writes it to `.fit/objects/`. Defaults to type `blob`.

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

Pushes the current branch (the one `HEAD` points at) to the branch of the same name on a remote Git server over HTTPS, sending only the objects the remote doesn't already have, in one undeltified packfile. Credentials are prompted interactively if `-u` and `-p` are not provided. For GitHub, use a **Personal Access Token** as the password.

Fit can't fetch, so a push is refused when the remote branch has commits that aren't present locally. HTTP errors, and a rejection in the server's status report (for example a non-fast-forward), are reported as errors.
