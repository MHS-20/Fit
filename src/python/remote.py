import hashlib
import os
import struct
import urllib.request
import urllib.error
import zlib
from enum import Enum

from fit.objects import object_read, object_write, FitCommit, FitTree, FitBlob, FitTag
from fit.refs import ref_resolve
from fit.repository import repo_file, repo_dir


# ---------------------------------------------------------------------------
# Object type numbers used inside pack files
# ---------------------------------------------------------------------------


class PackObjectType(Enum):
    commit = 1
    tree = 2
    blob = 3
    tag = 4


# ---------------------------------------------------------------------------
# pkt-line codec
# ---------------------------------------------------------------------------


def pkt_read_lines(data: bytes) -> list[bytes]:
    """Decode a pkt-line stream into a list of line payloads."""
    lines = []
    i = 0
    while i < len(data):
        # Each line starts with a 4-hex-digit length that includes the 4
        # bytes of the length field itself.  "0000" is a flush packet.
        raw_len = data[i : i + 4]
        if len(raw_len) < 4:
            break
        line_len = int(raw_len, 16)
        if line_len == 0:
            lines.append(b"")  # flush packet sentinel
            i += 4
        else:
            lines.append(data[i + 4 : i + line_len])
            i += line_len
    return lines


def pkt_build_lines(lines: list[bytes]) -> bytes:
    """Encode a list of payloads into pkt-line format, terminated by 0000."""
    out = []
    for line in lines:
        out.append(f"{len(line) + 5:04x}".encode())
        out.append(line)
        out.append(b"\n")
    out.append(b"0000")
    return b"".join(out)


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------


def _http(
    url: str,
    username: str,
    password: str,
    data: bytes | None = None,
    content_type: str | None = None,
) -> bytes:
    """Authenticated GET (data=None) or POST (data provided)."""
    mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    mgr.add_password(None, url, username, password)
    opener = urllib.request.build_opener(urllib.request.HTTPBasicAuthHandler(mgr))
    headers = {}
    if content_type:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=data, headers=headers)
    try:
        with opener.open(req) as resp:
            return resp.read()
    except urllib.error.HTTPError as e:
        raise Exception(f"HTTP {e.code} {e.reason} for {url}") from e


# ---------------------------------------------------------------------------
# Remote ref discovery
# ---------------------------------------------------------------------------


def get_remote_master_hash(git_url: str, username: str, password: str) -> str | None:
    """Return the SHA-1 of the remote's master/main branch, or None."""
    url = git_url.rstrip("/") + "/info/refs?service=git-receive-pack"
    data = _http(url, username, password)
    lines = pkt_read_lines(data)

    # lines[0] = b"# service=git-receive-pack\n", lines[1] = flush b""
    if len(lines) < 3:
        return None

    # lines[2] is "<sha1> <refname>\x00<capabilities>"  (or all-zeros for empty repo)
    first = lines[2]
    sha_and_ref = first.split(b"\x00")[0]  # strip capabilities
    parts = sha_and_ref.split()
    if len(parts) < 2:
        return None

    sha1 = parts[0].decode()
    if sha1 == "0" * 40:
        return None  # empty remote repo

    ref = parts[1].decode()
    if ref not in ("refs/heads/master", "refs/heads/main"):
        # Try to find master or main in remaining lines
        for line in lines[3:]:
            if not line:
                continue
            p = line.split()
            if len(p) >= 2 and p[1] in (b"refs/heads/master", b"refs/heads/main"):
                return p[0].decode()
        return None

    return sha1


# ---------------------------------------------------------------------------
# Object graph traversal — find everything reachable from a commit
# ---------------------------------------------------------------------------


def _collect_tree_objects(repo, tree_sha: str, seen: set[str]) -> None:
    if tree_sha in seen:
        return
    seen.add(tree_sha)
    tree = object_read(repo, tree_sha)
    for leaf in tree.items:
        if leaf.mode.startswith(b"04"):  # subtree
            _collect_tree_objects(repo, leaf.sha, seen)
        else:
            seen.add(leaf.sha)


def _collect_commit_objects(repo, commit_sha: str, seen: set[str]) -> None:
    """Recursively collect all object SHAs reachable from commit_sha."""
    if commit_sha in seen:
        return
    seen.add(commit_sha)

    commit = object_read(repo, commit_sha)
    if not isinstance(commit, FitCommit):
        return

    tree_sha = commit.kvlm.get(b"tree", b"").decode("ascii")
    if tree_sha:
        _collect_tree_objects(repo, tree_sha, seen)

    parents = commit.kvlm.get(b"parent", [])
    if isinstance(parents, bytes):
        parents = [parents]
    for p in parents:
        _collect_commit_objects(repo, p.decode("ascii"), seen)


def find_missing_objects(repo, local_sha: str, remote_sha: str | None) -> set[str]:
    """Return the set of SHAs the remote doesn't have."""
    local_objects: set[str] = set()
    _collect_commit_objects(repo, local_sha, local_objects)

    if remote_sha is None:
        return local_objects

    remote_objects: set[str] = set()
    _collect_commit_objects(repo, remote_sha, remote_objects)

    return local_objects - remote_objects


# ---------------------------------------------------------------------------
# Pack file creation
# ---------------------------------------------------------------------------


def _encode_pack_object(repo, sha: str) -> bytes:
    """Encode one object in the undeltified pack format."""
    obj = object_read(repo, sha)

    raw = obj.serialize()

    type_map = {
        b"commit": PackObjectType.commit.value,
        b"tree": PackObjectType.tree.value,
        b"blob": PackObjectType.blob.value,
        b"tag": PackObjectType.tag.value,
    }
    type_num = type_map[obj.fmt]
    size = len(raw)

    # Variable-length size encoding (MSB = "more bytes follow")
    # First byte: [type:3][size low 4 bits]
    header = []
    byte = (type_num << 4) | (size & 0x0F)
    size >>= 4
    while size:
        header.append(byte | 0x80)
        byte = size & 0x7F
        size >>= 7
    header.append(byte)

    return bytes(header) + zlib.compress(raw)


def create_pack(repo, objects: set[str]) -> bytes:
    """Build a complete PACK file for the given set of object SHAs."""
    # Header: magic, version=2, object count
    header = struct.pack("!4sLL", b"PACK", 2, len(objects))
    body = b"".join(_encode_pack_object(repo, sha) for sha in sorted(objects))
    contents = header + body
    checksum = hashlib.sha1(contents).digest()
    return contents + checksum


# ---------------------------------------------------------------------------
# Push
# ---------------------------------------------------------------------------


def get_local_master_hash(repo) -> str | None:
    """Return the SHA of the local master or main branch tip."""
    for branch in ("master", "main"):
        sha = ref_resolve(repo, f"refs/heads/{branch}")
        if sha:
            return sha
    return None


def push(repo, git_url: str, username: str, password: str) -> None:
    """Push the local master/main branch to the remote."""
    git_url = git_url.rstrip("/")

    local_sha = get_local_master_hash(repo)
    if not local_sha:
        raise Exception("Nothing to push: no local commits on master/main.")

    print(f"Fetching remote state from {git_url} …")
    remote_sha = get_remote_master_hash(git_url, username, password)

    if remote_sha == local_sha:
        print("Already up to date.")
        return

    print(f"Local  : {local_sha}")
    print(f"Remote : {remote_sha or '(empty)'}")

    missing = find_missing_objects(repo, local_sha, remote_sha)
    print(f"Packing {len(missing)} object(s) …")

    # Determine which ref name to update
    branch = "master" if ref_resolve(repo, "refs/heads/master") else "main"
    remote_ref = f"refs/heads/{branch}"

    # pkt-line ref-update line:  "<old-sha> <new-sha> <refname>\0<caps>"
    old = remote_sha or ("0" * 40)
    ref_line = f"{old} {local_sha} {remote_ref}\x00 report-status".encode()
    request_body = pkt_build_lines([ref_line]) + create_pack(repo, missing)

    print("Pushing …")
    url = git_url + "/git-receive-pack"
    response = _http(
        url,
        username,
        password,
        data=request_body,
        content_type="application/x-git-receive-pack-request",
    )

    lines = pkt_read_lines(response)
    # First non-empty line after the flush should be "unpack ok"
    result_lines = [l for l in lines if l]
    if not result_lines:
        raise Exception("Empty response from server.")
    if result_lines[0] != b"unpack ok\n":
        raise Exception(f"Push failed: {result_lines[0]!r}")

    print(f"Pushed {local_sha[:7]} → {remote_ref}")
