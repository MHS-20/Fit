# Fit

A Git-compatible version control system, built from scratch. `fit` speaks the same
object format as Git — repositories created with `fit` can be read by Git and
vice versa.

<div style="text-align: center;">
<img src="fit.png" alt="Logo" width="300" />
</div>

---

## Installation

```bash
cat << 'EOF' > ~/.fitconfig
[user]
    name = Your Name
    email = you@example.com
EOF

make          # create venv, install dependencies, install fit in editable mode
make install  # build a wheel and install fit as a system-wide command
```

After `make install`, the `fit` command is available globally without `make run`.

To uninstall:

```bash
make uninstall
```

### Build a standalone binary

```bash
make bundle   # produces dist/fit — no Python required on the target machine
```

---

## Commands

### `init` — Create a new repository

```bash
fit init [directory]
```

Initialises a `.fit/` directory with the standard object store, refs, HEAD,
and a default config. Defaults to the current directory if no path is given.

### `add` — Stage files

```bash
fit add <path> [path ...]
```

Hashes each file as a blob, writes it to the object store, and updates the
index. Re-adding an already-tracked file replaces its index entry.

### `rm` — Remove files

```bash
fit rm <path> [path ...]
```

Removes files from the index and deletes them from the working tree. Raises
an error if a path is not tracked or outside the worktree.

### `commit` — Record a snapshot

```bash
fit commit -m "message"
```

Builds a tree from the current index, wraps it in a commit object (with
author/committer read from `~/.fitconfig` or `$XDG_CONFIG_HOME/fit/config`),
and advances the active branch ref. Detached HEAD writes directly to `HEAD`.

### `status` — Show working tree state

```bash
fit status
```

Prints three sections:

- **Branch** — current branch name or detached HEAD hash
- **Changes to be committed** — diff between HEAD tree and index (added, modified, deleted)
- **Changes not staged / Untracked** — diff between index and working tree, with `.fitignore` filtering applied

### `log` — Display commit history

```bash
fit log [commit]
```

Walks the parent chain from the given commit (defaults to `HEAD`)

### `checkout` — Restore a commit into a directory

```bash
fit checkout <commit> <path>
```

Reads the tree of the given commit and writes it into `<path>`, which must
exist and be empty (or not yet exist).

### `tag` — List or create tags

```bash
fit tag                        # list all tags
fit tag <name> [object]        # lightweight tag pointing to object (default HEAD)
fit tag -a <name> [object]     # annotated tag (creates a tag object)
```

### `show-ref` — List all refs

```bash
fit show-ref
```

Prints every ref under `.fit/refs/` with its resolved SHA.

### `rev-parse` — Resolve a name to a SHA

```bash
fit rev-parse [--fit-type blob|commit|tag|tree] <name>
```

Resolves branches, tags, remote refs, short hashes, and `HEAD`. With
`--fit-type`, follows tag/commit indirection until the expected type is found.

### `cat-file` — Inspect an object

```bash
fit cat-file <type> <object>
```

Deserialises and prints the raw content of any object. `type` must match the
object's actual type (`blob`, `commit`, `tag`, `tree`).

### `hash-object` — Hash a file into the object store

```bash
fit hash-object [-t type] [-w] <file>
```

Computes the SHA-1 of the file content wrapped in a Git object header. `-w`
writes it to `.fit/objects/`. Defaults to type `blob`.

### `ls-tree` — Print a tree object

```bash
fit ls-tree [-r] <tree>
```

Lists the entries of a tree object. `-r` recurses into subtrees.

### `ls-files` — List staged files

```bash
fit ls-files [--verbose]
```

Prints every file currently in the index. `--verbose` shows the full index
entry: blob SHA, permissions, timestamps, device, inode, uid/gid, and flags.

### `check-ignore` — Test paths against ignore rules

```bash
fit check-ignore <path> [path ...]
```

Prints each path that would be ignored. Checks scoped `.fitignore` files
first, then absolute rules from `.fit/info/exclude` and the global config.

### `push` — Push to a remote

```bash
fit push <url>
fit push <url> -u <username> -p <token>
```

Pushes the local master/main branch to a remote Git server over HTTPS.
Credentials are prompted interactively if `-u` and `-p` are not provided.

For GitHub, use a **Personal Access Token** as the password

## Implementation highlights

### Git-compatible object store

Objects are stored identically to Git: zlib-compressed, SHA-1 addressed, in
`.fit/objects/<xx>/<38-char-remainder>`. A `fit`-initialised repo can be
opened with `git` by symlinking `.fit` to `.git`.

### Recursive KVLM parser

Commits and tags use Git's *key-value list with message* format. `kvlm_parse`
in `objects.py` handles continuation lines (values that span multiple lines
indented with a space), duplicate keys (merged into a list — used for merge
commits with multiple `parent` fields), and the trailing free-form message
separated by a blank line.

### Type-following object resolution

`object_find` in `objects.py` accepts any ref name and an optional expected
type, then walks the object graph — dereferencing annotated tags and
commit→tree links — until it reaches an object of the right type. This is
what lets commands accept a branch name, a tag, or a raw hash interchangeably.

### Short hash resolution

`object_resolve` scans `.fit/objects/<prefix>/` for all filenames starting
with the remainder of a short hash (minimum 4 hex chars), collecting
candidates. Ambiguous short hashes raise an error listing all matches, exactly
as Git does.

### Index binary format

`index.py` implements the full Git index v2 binary format: 62-byte fixed
header per entry (timestamps with nanosecond precision, device, inode, mode,
uid/gid, size, SHA-1, flags), variable-length null-terminated UTF-8 name, and
8-byte alignment padding. The reader and writer are symmetric, so indexes
written by `fit` are readable by Git.

### Scoped ignore rules

`ignore.py` evaluates `.fitignore` rules in order of precedence: scoped rules
from the nearest `.fitignore` ancestor directory first, then absolute rules
from `.fit/info/exclude` and the global config file. Negation patterns (`!`)
and escape prefixes (`\`) are supported. Rules are matched with `fnmatch`.

### Tree serialisation and sorting

Git requires tree entries to be sorted by a specific key: directories sort as
if their name had a trailing `/`, so a directory `foo` sorts after a file
`foo.c`. `tree_serialize` in `objects.py` applies this comparator before
writing, ensuring the SHA-1 of a tree is deterministic regardless of insertion
order.

### Smart HTTP push protocol

remote.py implements Git's smart HTTP transfer protocol using only the
standard library. A push involves three stages.

1. First, a GET to <url>/info/refs?service=git-receive-pack discovers the
remote's current branch tip. The response is encoded in pkt-line format:
every chunk is prefixed with a 4-hex-digit length that includes the length
field itself, and the special token 0000 marks section boundaries.
2. Second, the local and remote object graphs are walked recursively — following
commit parents and tree entries — to compute the set of objects the remote is
missing. Only this delta is sent, not the entire history.
3. Third, a POST to <url>/git-receive-pack sends a pkt-line ref-update header
followed by a PACK file containing all missing objects. Each object in the
pack is encoded with a variable-length type-and-size header (3 bits for type,
then a sequence of 7-bit groups) and zlib-compressed content. The pack file
itself ends with a SHA-1 checksum of its own contents. The server replies with
unpack ok on success
