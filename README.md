# Fit

A Git-compatible version control system, built from scratch. `fit` speaks the same object format as Git — repositories created with `fit` can be read by Git and vice versa.

<div style="text-align: center;">
<img src="fit.png" alt="Logo" width="300" />
</div>

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

We provide a quality-of-life wrapper `Makefile` at the repository root to simplify daily workspace workflow. Ensure your environment variable `$VCPKG_ROOT` is configured in your shell environment before initializing the compilation sequence.

### Installation

Install the binary executable system-wide (installs to `/usr/local/bin` by default):

```bash
make init
make 
make install
```

Configure your user:

```
printf "[user]\nname = Your Name\nemail = <you@example.com>\n" > ~/.fitconfig
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

Builds a tree from the current index, wraps it in a commit object (with author/committer identities read from `~/.fitconfig` or `$XDG_CONFIG_HOME/fit/config`), and advances the active branch ref. Detached HEAD writes directly to `HEAD`.

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

### `checkout` — Restore a commit into a directory

```bash
fit checkout <commit> <path>

```

Reads the tree of the given commit and writes it into `<path>`, which must exist and be empty (or not yet exist).

### `tag` — List or create tags

```bash
fit tag                        # List all tags
fit tag <name> [object]        # Lightweight tag pointing to object (default HEAD)
fit tag -a <name> [object]     # Annotated tag (creates a distinct tag object)

```

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

---

## Implementation Highlights (C++ Architecture)

### Git-Compatible Object Store

Objects are stored identically to Git: zlib-compressed (`<zlib.h>`), SHA-1 addressed via OpenSSL (`<openssl/sha.h>`), and organized under `.fit/objects/<xx>/<38-char-remainder>`. A `fit`-initialized repository can be fully interrogated natively with standard Git by symlinking `.fit` to `.git`.

### Key-Value List with Message (KVLM) Parser

Commits and tags use Git's *key-value list with message* format. The extraction loop parsing sequence handles continuation lines (values that span multiple lines indented with an explicit leading space), duplicate keys (merged into lists to process merge commits with multiple `parent` entries), and the trailing free-form commit message block separated by a blank line.

### Safe Whitespace INI Configuration Loader

Our configuration subsystem implements an aggressive, bidirectional whitespace and tab stripping mechanism utilizing the C-based `inih` streaming engine. This ensures system-wide profiles like `~/.fitconfig` and repository parameters map fields flawlessly into structured `std::map<std::string, std::map<std::string, std::string>>` contexts regardless of tabulations or indents.

### Index Binary Parsing Layout

`Index.cpp` handles raw transformations over the explicit Git index v2 binary serialization spec. This maps out 62-byte fixed-width block structures per file entry (nanosecond-precision modification timestamps, device identifiers, inodes, explicit file modes, uid/gid mappings, file sizes, SHA-1 byte streams, and bit flags) tied to variable-length null-terminated UTF-8 paths padded out to 8-byte boundaries.

### Smart HTTP Push Transfer Engine

`Remote.cpp` handles native communication with remote servers over Git's smart HTTP transport standard utilizing raw `libcurl` streaming blocks:

1. **Discovery**: Executes a `GET` request against `<url>/info/refs?service=git-receive-pack` to read remote references formatted inside standard length-prefixed `pkt-line` encoding blocks.
2. **Delta Generation**: Computes graph intersections locally by checking object structures to extract only the missing delta stream dependencies.
3. **Transmission**: Fires a `POST` request to `<url>/git-receive-pack` containing a `pkt-line` modification header followed by a raw packed structural binary stream (PACK file) embedded with bit-shifted variable-length headers and a terminal trailing SHA-1 validation checksum.
