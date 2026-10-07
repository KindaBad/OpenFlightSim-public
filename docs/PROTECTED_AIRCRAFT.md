# Protected aircraft content

Player Su-57 assets use authenticated XChaCha20-Poly1305 encryption through
Monocypher 4.0.2. The runtime resolves the canonical `.glb` name to a sibling
`.glb.ofspack`, verifies authentication, and parses the decrypted GLB in memory.
It writes no decrypted file. Corrupt packs fail instead of falling back to a
loose copy. All textures and buffers must be embedded; external URIs are rejected.
The original rig, materials and authored LODs are unchanged.

An OFSPACK1 envelope contains eight magic/version bytes, a random 24-byte nonce,
an eight-byte little-endian plaintext length, ciphertext and a 16-byte tag. The
40-byte header is authenticated. Payloads are limited to 512 MiB. Nonces come
from Python's operating-system-backed `secrets` module; never reuse a nonce/key
pair. Initial decrypted document and binary-chunk buffers are wiped after parse.
Renderable geometry and textures necessarily remain in game memory.

Generate a random 32-byte key as 64 lowercase hexadecimal digits in an ignored
`.ofskey` file. Keep it outside Git, public archives and logs. Configure a local
build with `-DOFS_ASSET_KEY_FILE=/absolute/path/content.ofskey`; build
`ofs_protected_asset_tool`. The generated header exists only in the build tree.
Create packs with:

```sh
python scripts/protect_aircraft_assets.py \
  --tool build/keyed/ofs_protected_asset_tool \
  --source-root . --output-root build/protected-content \
  assets/aircraft/su57/su57_lod0.glb \
  assets/aircraft/su57/su57_lod1.glb \
  assets/aircraft/su57/su57_lod2.glb \
  assets/aircraft/su57/su57_lod3.glb
```

For installation configure `OFS_PROTECTED_SU57_ROOT` to that output root and
retain the same key. The installer copies only encrypted Su-57 LODs. Source
artwork remains local. Public builds receive the key through the existing
repository's `OFS_ASSET_KEY_HEX` Actions secret. Unkeyed development builds
retain loose-model support and reject protected packs.

Asset approval uses the encrypted filename and SHA-256, with
`redistributable: true`, `license`, `source`, `protection: "OFSPACK1"`, `credit`,
and `rights_evidence`. Approval is for incorporated game use, not standalone
model distribution. Packaging rejects loose Su-57 content, duplicate loose
copies of protected models, unregistered packs, Blender files and content keys.
Repair/update indexes distribute only encrypted content for protected models.
The existing update signature/hash, slot activation and rollback flow is retained.

Encryption is a practical asset safeguard, not an extraction-proof guarantee.
A sufficiently determined person can reverse engineer a game binary or inspect
its memory, especially with a public loader implementation. No server check,
online requirement or DRM is introduced. The key is embedded in the game binary,
not published as a separate downloadable key file. Compiled-dependency caches
are saved only before the content key is added; keyed headers and objects are
never saved to the shared build cache. This format does not expand
any artwork license; each approved asset still needs documented rights evidence.
