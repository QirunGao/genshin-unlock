file(SHA256 "${LAUNCHER_PATH}" launcher_hash)
file(SHA256 "${BOOTSTRAP_PATH}" bootstrap_hash)
file(SHA256 "${RUNTIME_PATH}" runtime_hash)

set(manifest_path "${OUTPUT_DIR}/module_hashes.json")
file(WRITE "${manifest_path}" "{\n")
file(APPEND "${manifest_path}"
    "  \"launcherSha256\": \"${launcher_hash}\",\n")
file(APPEND "${manifest_path}"
    "  \"bootstrapSha256\": \"${bootstrap_hash}\",\n")
file(APPEND "${manifest_path}"
    "  \"runtimeSha256\": \"${runtime_hash}\"\n")
file(APPEND "${manifest_path}" "}\n")
