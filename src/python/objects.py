import hashlib
import os
import zlib

from fit.repository import repo_file
from fit.refs import ref_resolve


class FitObject(object):
    def __init__(self, data=None):
        if data is not None:
            self.deserialize(data)
        else:
            self.init()

    def serialize(self, repo):
        raise Exception("Unimplemented!")

    def deserialize(self, data):
        raise Exception("Unimplemented!")

    def init(self):
        pass


# ---------------------------------------------------------------------------
# Blob
# ---------------------------------------------------------------------------


class FitBlob(FitObject):
    fmt = b"blob"

    def serialize(self):
        return self.blobdata

    def deserialize(self, data):
        self.blobdata = data


# ---------------------------------------------------------------------------
# Key-value list with message (commits & tags)
# ---------------------------------------------------------------------------


def kvlm_parse(raw, start=0, dct=None):
    if not dct:
        dct = dict()

    spc = raw.find(b" ", start)
    nl = raw.find(b"\n", start)

    if (spc < 0) or (nl < spc):
        dct[None] = raw[start + 1 :]
        return dct

    key = raw[start:spc]

    end = start
    while True:
        end = raw.find(b"\n", end + 1)
        if raw[end + 1] != ord(" "):
            break

    value = raw[spc + 1 : end].replace(b"\n ", b"\n")

    if key in dct:
        if type(dct[key]) is list:
            dct[key].append(value)
        else:
            dct[key] = [dct[key], value]
    else:
        dct[key] = value

    return kvlm_parse(raw, start=end + 1, dct=dct)


def kvlm_serialize(kvlm):
    ret = b""
    for k in kvlm.keys():
        if k is None:
            continue
        val = kvlm[k]
        if type(val) is not list:
            val = [val]
        for v in val:
            ret += k + b" " + (v.replace(b"\n", b"\n ")) + b"\n"
    ret += b"\n" + kvlm[None]
    return ret


# ---------------------------------------------------------------------------
# Commit
# ---------------------------------------------------------------------------


class FitCommit(FitObject):
    fmt = b"commit"

    def deserialize(self, data):
        self.kvlm = kvlm_parse(data)

    def serialize(self):
        return kvlm_serialize(self.kvlm)

    def init(self):
        self.kvlm = dict()


# ---------------------------------------------------------------------------
# Tag
# ---------------------------------------------------------------------------


class FitTag(FitCommit):
    fmt = b"tag"


# ---------------------------------------------------------------------------
# Tree
# ---------------------------------------------------------------------------


class FitTreeLeaf(object):
    def __init__(self, mode, path, sha):
        self.mode = mode
        self.path = path
        self.sha = sha


def tree_parse_one(raw, start=0):
    x = raw.find(b" ", start)
    assert x - start == 5 or x - start == 6

    mode = raw[start:x]
    if len(mode) == 5:
        mode = b"0" + mode

    y = raw.find(b"\x00", x)
    path = raw[x + 1 : y]

    raw_sha = int.from_bytes(raw[y + 1 : y + 21], "big")
    sha = format(raw_sha, "040x")
    return y + 21, FitTreeLeaf(mode, path.decode("utf8"), sha)


def tree_parse(raw):
    pos = 0
    max = len(raw)
    ret = list()
    while pos < max:
        pos, data = tree_parse_one(raw, pos)
        ret.append(data)
    return ret


def tree_leaf_sort_key(leaf):
    if leaf.mode.startswith(b"4"):
        return leaf.path + "/"
    else:
        return leaf.path


def tree_serialize(obj):
    obj.items.sort(key=tree_leaf_sort_key)
    ret = b""
    for i in obj.items:
        ret += i.mode
        ret += b" "
        ret += i.path.encode("utf8")
        ret += b"\x00"
        sha = int(i.sha, 16)
        ret += sha.to_bytes(20, byteorder="big")
    return ret


class FitTree(FitObject):
    fmt = b"tree"

    def deserialize(self, data):
        self.items = tree_parse(data)

    def serialize(self):
        return tree_serialize(self)

    def init(self):
        self.items = list()


# ---------------------------------------------------------------------------
# Object I/O
# ---------------------------------------------------------------------------


def object_read(repo, sha):
    path = repo_file(repo, "objects", sha[0:2], sha[2:])

    if not os.path.isfile(path):
        return None

    with open(path, "rb") as f:
        raw = zlib.decompress(f.read())

        x = raw.find(b" ")
        fmt = raw[0:x]

        y = raw.find(b"\x00", x)
        size = int(raw[x:y].decode("ascii"))
        if size != len(raw) - y - 1:
            raise Exception(f"Malformed object {sha}: bad length")

        match fmt:
            case b"commit":
                c = FitCommit
            case b"tree":
                c = FitTree
            case b"tag":
                c = FitTag
            case b"blob":
                c = FitBlob
            case _:
                raise Exception(f"Unknown type {fmt.decode('ascii')} for object {sha}")

        return c(raw[y + 1 :])


def object_write(obj, repo=None):
    data = obj.serialize()
    result = obj.fmt + b" " + str(len(data)).encode() + b"\x00" + data
    sha = hashlib.sha1(result).hexdigest()

    if repo:
        path = repo_file(repo, "objects", sha[0:2], sha[2:], mkdir=True)
        if not os.path.exists(path):
            with open(path, "wb") as f:
                f.write(zlib.compress(result))
    return sha


def object_hash(fd, fmt, repo=None):
    data = fd.read()
    match fmt:
        case b"commit":
            obj = FitCommit(data)
        case b"tree":
            obj = FitTree(data)
        case b"tag":
            obj = FitTag(data)
        case b"blob":
            obj = FitBlob(data)
        case _:
            raise Exception(f"Unknown type {fmt}!")
    return object_write(obj, repo)


def object_find(repo, name, fmt=None, follow=True):
    sha = object_resolve(repo, name)

    if not sha:
        raise Exception(f"No such reference {name}.")

    if len(sha) > 1:
        raise Exception(
            f"Ambiguous reference {name}: Candidates are:\n - {chr(10).join(sha)}."
        )

    sha = sha[0]

    if not fmt:
        return sha

    while True:
        obj = object_read(repo, sha)

        if obj.fmt == fmt:
            return sha

        if not follow:
            return None

        if obj.fmt == b"tag":
            sha = obj.kvlm[b"object"].decode("ascii")
        elif obj.fmt == b"commit" and fmt == b"tree":
            sha = obj.kvlm[b"tree"].decode("ascii")
        else:
            return None


def object_resolve(repo, name):
    """Resolve name to an object hash in repo.

    Handles: HEAD, short/long hashes, tags, branches, remote branches.
    """
    import re

    candidates = list()
    hashRE = re.compile(r"^[0-9A-Fa-f]{4,40}$")

    if not name.strip():
        return None

    if name == "HEAD":
        return [ref_resolve(repo, "HEAD")]

    if hashRE.match(name):
        name = name.lower()
        prefix = name[0:2]
        from repository import repo_dir

        path = repo_dir(repo, "objects", prefix, mkdir=False)
        if path:
            rem = name[2:]
            for f in os.listdir(path):
                if f.startswith(rem):
                    candidates.append(prefix + f)

    as_tag = ref_resolve(repo, "refs/tags/" + name)
    if as_tag:
        candidates.append(as_tag)

    as_branch = ref_resolve(repo, "refs/heads/" + name)
    if as_branch:
        candidates.append(as_branch)

    as_remote_branch = ref_resolve(repo, "refs/remotes/" + name)
    if as_remote_branch:
        candidates.append(as_remote_branch)

    return candidates
