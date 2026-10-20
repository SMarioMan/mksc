"""IPS encoder and strict decoder used by the release builder.

Every patch targets the supplied US base ROM. The optional EOF size field
also supports the smaller homebrew image without leaving trailing game data.
"""

LIMIT = 0x1000000
EOF_OFFSET = int.from_bytes(b"EOF", "big")


def create(source: bytes, target: bytes) -> bytes:
    if len(source) > LIMIT or len(target) > LIMIT:
        raise ValueError("IPS supports images up to 16 MiB")
    patch = bytearray(b"PATCH")
    offset = 0
    while offset < len(target):
        if offset < len(source) and source[offset] == target[offset]:
            offset += 1
            continue
        start = offset
        # EOF is reserved as a record address. Include the preceding byte.
        if start == EOF_OFFSET:
            start -= 1
        end = offset + 1
        while end < len(target) and end - start < 65535:
            if end < len(source) and target[end] == source[end]:
                # Merge short equal gaps: cheaper than a new five-byte header.
                gap = end
                while gap < min(end + 6, len(target), start + 65535):
                    if gap >= len(source) or target[gap] != source[gap]:
                        break
                    gap += 1
                if gap - end >= 6 or gap == len(target) or gap == start + 65535:
                    break
                end = gap
            else:
                end += 1
        data = target[start:end]
        patch += start.to_bytes(3, "big")
        if len(data) > 3 and data.count(data[:1]) == len(data):
            patch += b"\0\0" + len(data).to_bytes(2, "big") + data[:1]
        else:
            patch += len(data).to_bytes(2, "big") + data
        offset = end
    patch += b"EOF"
    if len(target) != len(source) and len(target) < LIMIT:
        patch += len(target).to_bytes(3, "big")
    return bytes(patch)


def apply(source: bytes, patch: bytes) -> bytes:
    if patch[:5] != b"PATCH":
        raise ValueError("Invalid IPS signature")
    output = bytearray(source)
    position = 5
    while True:
        if position + 3 > len(patch):
            raise ValueError("Missing IPS EOF")
        address = patch[position:position + 3]
        position += 3
        if address == b"EOF":
            break
        if position + 2 > len(patch):
            raise ValueError("Truncated IPS record")
        size = int.from_bytes(patch[position:position + 2], "big")
        position += 2
        if size:
            data = patch[position:position + size]
            if len(data) != size:
                raise ValueError("Truncated IPS data")
            position += size
        else:
            if position + 3 > len(patch):
                raise ValueError("Truncated IPS run")
            size = int.from_bytes(patch[position:position + 2], "big")
            if not size:
                raise ValueError("Empty IPS run")
            data = patch[position + 2:position + 3] * size
            position += 3
        offset = int.from_bytes(address, "big")
        if offset + size > LIMIT:
            raise ValueError("IPS record exceeds image limit")
        if offset + size > len(output):
            output.extend(bytes(offset + size - len(output)))
        output[offset:offset + size] = data
    if len(patch) - position == 3:
        size = int.from_bytes(patch[position:], "big")
        del output[size:]
        output.extend(bytes(max(0, size - len(output))))
    elif position != len(patch):
        raise ValueError("Unexpected data after IPS EOF")
    return bytes(output)
