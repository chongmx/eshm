# 07 - The DataHandler types Python does not speak

Every other example here is a C++ ↔ Python pair. This one is deliberately not,
and the reason is worth knowing before you design a payload.

`DataHandler` defines eight `DataType`s. Five cross the language boundary and
are covered in [02](../02_structured_data/) and [03](../03_c_api/). The other
three — `EVENT`, `FUNCTION_CALL`, `IMAGE_FRAME` — are encoded with custom
application tags and are implemented **only in the C++ codec**:

| DataType | Tag | C++ | Python |
|---|---|---|---|
| `INTEGER`, `BOOLEAN`, `REAL`, `STRING`, `BINARY` | universal | yes | yes |
| `EVENT` (5) | `0x80` | yes | **no** |
| `FUNCTION_CALL` (6) | `0x81` | yes | **no** |
| `IMAGE_FRAME` (7) | `0x83` | yes | **no** |

`py/data_handler.py` raises `ValueError: Unsupported data type` when asked to
encode one, and `ESHMData.write_data()` (the native path) rejects them too. A
buffer containing one of these tags will not decode in Python.

## Run

```bash
cmake -S . -B build && cmake --build build
./build/rich_types
```

No channel, no peer — it encodes and decodes in the same process, so you can
read the sizes and the round-trip results directly.

## What it covers

| Section | Shows |
|---|---|
| 1 — simple data | The five scalar types, and `extractSimpleValues` |
| 2 — events | `Event` with a named parameter map, `extractEvents` |
| 3 — function calls | `registerFunction`, `processFunctionCalls`, return values, `extractFunctions` |
| 4 — image frames | `ImageFrame` at 1080p and 4K, `extractImages`, encode/decode cost |
| 5 — mixed payload | All of the above in one buffer |

The function-call registry is the interesting one: `DataHandler` can carry a
call *and* dispatch it. Register a handler by name, decode a buffer containing
a `FUNCTION_CALL`, and `processFunctionCalls` runs it and writes the result
back into the item's return value — an RPC layer in about twenty lines.

## If Python is on the other end

Three options, cheapest first:

1. **Flatten to scalars.** An event becomes a `STRING` name plus its parameters
   as separate keyed items. Works everywhere, no library changes, and it is
   what most of these payloads reduce to anyway.
2. **Carry your own encoding in a `BINARY` item.** JSON, protobuf, whatever —
   the channel does not care, and `BINARY` crosses cleanly.
3. **Keep the rich types and put C++ on both ends.** Fine when Python is not
   involved; just know that adding a Python consumer later means revisiting
   the payload.

For image data specifically, prefer example [06](../06_large_payload/): raw
bytes with a small header move faster than an `IMAGE_FRAME` wrapped in DER,
and that path already works in both languages.
