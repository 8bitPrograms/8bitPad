#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
installer=${1:-"$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)/install.sh"}
installer=$(readlink -f -- "$installer")
test_root=$(mktemp -d "${TMPDIR:-/tmp}/8bitpad-bootstrap-tests.XXXXXXXX")
trap 'rm -rf -- "$test_root"' EXIT
test_count=0
test_install="$test_root/version store; literal"
test_bin="$test_root/launcher dir"
test_data="$test_root/desktop data"
mkdir -p -- "$test_root/config"
printf 'preserve user preferences\n' > "$test_root/config/8bitpad.ini"

new_fixture() {
    local folder=$1 release_version=$2 bundle="8bitPad-$2-linux-x86_64" asset="8bitPad-$2-linux-x86_64.tar.gz"
    mkdir -p -- "$folder/content/$bundle/usr/bin" "$folder/content/$bundle/usr/share/icons/hicolor/scalable/apps"
    printf 'v%s\n' "$release_version" > "$folder/release-tag.txt"
    cat > "$folder/content/$bundle/usr/bin/8bitPad" <<'APP'
#!/usr/bin/env bash
# Locally authored argument-capture fixture, not the real application.
printf '%s\n' "$@" > "$BIT8PAD_TEST_OUTPUT"
APP
    printf '# release %s\n' "$release_version" >> "$folder/content/$bundle/usr/bin/8bitPad"
    cat > "$folder/content/$bundle/8bitPad" <<'LAUNCHER'
#!/usr/bin/env bash
bundle_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec "$bundle_dir/usr/bin/8bitPad" "$@"
LAUNCHER
    chmod +x -- "$folder/content/$bundle/8bitPad" "$folder/content/$bundle/usr/bin/8bitPad"
    printf '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M1 1h14v14H1z"/></svg>\n' > "$folder/content/$bundle/usr/share/icons/hicolor/scalable/apps/8bitpad.svg"
    tar -czf "$folder/$asset" -C "$folder/content" "$bundle"
    (cd -- "$folder" && sha256sum "$asset" > SHA256SUMS.txt)
}
run_ok() {
    local name=$1; shift
    if ! "$@" > "$test_root/$name.log" 2>&1; then cat "$test_root/$name.log" >&2; printf 'FAIL: %s\n' "$name" >&2; exit 1; fi
    test_count=$((test_count+1))
}
run_bad() {
    local name=$1; shift
    if "$@" > "$test_root/$name.log" 2>&1; then cat "$test_root/$name.log" >&2; printf 'FAIL: %s should fail\n' "$name" >&2; exit 1; fi
    test_count=$((test_count+1))
}
assert_equal() { [[ "$1" == "$2" ]] || { printf 'FAIL: %s\nExpected: %s\nActual: %s\n' "$3" "$2" "$1" >&2; exit 1; }; }
common=(--repo fixture-owner/8bitpad --install-dir "$test_install" --bin-dir "$test_bin" --data-dir "$test_data")
fixture="$test_root/fixture one"
new_fixture "$fixture" 1.2.3
run_bad placeholder bash "$installer" --repo OWNER/8bitpad --fixture-directory "$fixture" --download-only
run_bad traversal bash "$installer" "${common[@]}" --fixture-directory "$fixture" --version ../../escape
run_bad fixture_paths_required bash "$installer" --repo fixture-owner/8bitpad --fixture-directory "$fixture"
run_bad version_mismatch bash "$installer" "${common[@]}" --fixture-directory "$fixture" --version 1.2.4
run_ok download bash "$installer" --repo fixture-owner/8bitpad --fixture-directory "$fixture" --version v1.2.3 --download-only --download-dir "$test_root/downloads"
[[ -f "$test_root/downloads/8bitPad-1.2.3-linux-x86_64.tar.gz" ]]
run_ok first_install env XDG_CONFIG_HOME="$test_root/config" bash "$installer" "${common[@]}" --fixture-directory "$fixture"
[[ -L "$test_bin/8bitpad" && -f "$test_data/applications/8bitpad.desktop" && -f "$test_data/icons/hicolor/scalable/apps/8bitpad.svg" ]]
grep -Fqx -- "Exec=\"$test_bin/8bitpad\" %F" "$test_data/applications/8bitpad.desktop"
[[ ! -e "$test_root/launch-output" ]] # installation must not auto-start even the fixture
old_link=$(readlink -- "$test_bin/8bitpad")
old_desktop=$(sha256sum "$test_data/applications/8bitpad.desktop")
old_icon=$(sha256sum "$test_data/icons/hicolor/scalable/apps/8bitpad.svg")

run_ok launch_literal_arguments env BIT8PAD_TEST_OUTPUT="$test_root/launch-output" "$test_bin/8bitpad" 'two words' 'semi;colon' '$(literal)' 'Türkçe ıİ' '{folder}'
printf '%s\n' 'two words' 'semi;colon' '$(literal)' 'Türkçe ıİ' '{folder}' > "$test_root/expected-output"
cmp "$test_root/launch-output" "$test_root/expected-output"

update="$test_root/fixture update"
new_fixture "$update" 1.2.4
run_bad update_rollback bash "$installer" "${common[@]}" --fixture-directory "$update" --fixture-fail-before-link
assert_equal "$(readlink -- "$test_bin/8bitpad")" "$old_link" 'Failed update must retain the old atomic launcher'
assert_equal "$(sha256sum "$test_data/applications/8bitpad.desktop")" "$old_desktop" 'Desktop rollback'
assert_equal "$(sha256sum "$test_data/icons/hicolor/scalable/apps/8bitpad.svg")" "$old_icon" 'Icon rollback'
[[ ! -e "$test_install/versions/1.2.4" ]]
run_ok update_success bash "$installer" "${common[@]}" --fixture-directory "$update"
[[ $(readlink -- "$test_bin/8bitpad") == "$test_install/versions/1.2.4/8bitpad" ]]
[[ -x "$test_install/versions/1.2.3/8bitPad" && -x "$test_install/versions/1.2.3/usr/bin/8bitPad" ]]
assert_equal "$(cat "$test_root/config/8bitpad.ini")" 'preserve user preferences' 'User configuration preserved'

bad="$test_root/fixture tampered"
new_fixture "$bad" 1.2.5
printf tampered >> "$bad/8bitPad-1.2.5-linux-x86_64.tar.gz"
run_bad checksum_mismatch bash "$installer" "${common[@]}" --fixture-directory "$bad"
assert_equal "$(readlink -- "$test_bin/8bitpad")" "$test_install/versions/1.2.4/8bitpad" 'Checksum failure must not switch versions'
[[ ! -e "$test_install/versions/1.2.5" ]]

duplicate="$test_root/fixture duplicate"
new_fixture "$duplicate" 1.2.6
cat "$duplicate/SHA256SUMS.txt" > "$duplicate/copy"
cat "$duplicate/copy" >> "$duplicate/SHA256SUMS.txt"
run_bad duplicate_checksum bash "$installer" "${common[@]}" --fixture-directory "$duplicate"
wrong="$test_root/fixture wrong name"
new_fixture "$wrong" 1.2.7
sed -i 's/tar.gz$/tar.gz.extra/' "$wrong/SHA256SUMS.txt"
run_bad exact_name bash "$installer" "${common[@]}" --fixture-directory "$wrong"
missing="$test_root/fixture missing"
new_fixture "$missing" 1.2.8
rm -- "$missing/8bitPad-1.2.8-linux-x86_64.tar.gz"
run_bad missing_asset bash "$installer" "${common[@]}" --fixture-directory "$missing"

mkdir -p "$test_root/foreign-bin"
printf 'keep this launcher\n' > "$test_root/foreign-bin/8bitpad"
run_bad foreign_launcher bash "$installer" --repo fixture-owner/8bitpad --fixture-directory "$fixture" --install-dir "$test_install" --bin-dir "$test_root/foreign-bin" --data-dir "$test_data"
assert_equal "$(cat "$test_root/foreign-bin/8bitpad")" 'keep this launcher' 'Unmanaged executable preserved'

real_store="$test_root/real store"
real_bin="$test_root/"'real bin 100% $cash'
real_data="$test_root/real data"
mkdir -p -- "$real_store" "$real_bin" "$real_data"
ln -s -- "$real_store" "$test_root/store-alias"
ln -s -- "$real_bin" "$test_root/bin-alias"
ln -s -- "$real_data" "$test_root/data-alias"
run_ok symlink_directories bash "$installer" --repo fixture-owner/8bitpad --fixture-directory "$fixture" --install-dir "$test_root/store-alias" --bin-dir "$test_root/bin-alias" --data-dir "$test_root/data-alias"
assert_equal "$(readlink -- "$real_bin/8bitpad")" "$real_store/versions/1.2.3/8bitpad" 'Canonical version target through symlinked directories'
expected_exec='Exec="'"$test_root"'/real bin 100%% \\$cash/8bitpad" %F'
grep -Fqx -- "$expected_exec" "$real_data/applications/8bitpad.desktop"
[[ -L "$test_root/store-alias" && -L "$test_root/bin-alias" && -L "$test_root/data-alias" ]]
if command -v desktop-file-validate >/dev/null 2>&1; then
    run_ok desktop_entry_validation desktop-file-validate "$real_data/applications/8bitpad.desktop"
fi

# Fully checksummed malicious archives must still fail before extraction.
tar_fixture="$test_root/archive-traversal"
new_fixture "$tar_fixture" 1.3.0
tar_bundle=8bitPad-1.3.0-linux-x86_64
tar -czf "$tar_fixture/$tar_bundle.tar.gz" --transform="s|$tar_bundle/usr/bin/8bitPad|$tar_bundle/../escape|" -C "$tar_fixture/content" "$tar_bundle"
(cd "$tar_fixture" && sha256sum "$tar_bundle.tar.gz" > SHA256SUMS.txt)
printf 'outside extraction target\n' > "$test_install/escape"
run_bad archive_traversal bash "$installer" "${common[@]}" --fixture-directory "$tar_fixture"
grep -q 'path traversal' "$test_root/archive_traversal.log"
assert_equal "$(cat "$test_install/escape")" 'outside extraction target' 'Archive traversal must not overwrite outside staging'

tar_fixture="$test_root/archive-symlink"
new_fixture "$tar_fixture" 1.3.1
tar_bundle=8bitPad-1.3.1-linux-x86_64
ln -s /tmp/8bitpad-forbidden "$tar_fixture/content/$tar_bundle/escape-link"
tar -czf "$tar_fixture/$tar_bundle.tar.gz" -C "$tar_fixture/content" "$tar_bundle"
(cd "$tar_fixture" && sha256sum "$tar_bundle.tar.gz" > SHA256SUMS.txt)
run_bad archive_symlink bash "$installer" "${common[@]}" --fixture-directory "$tar_fixture"
grep -q 'Archive links' "$test_root/archive_symlink.log"

tar_fixture="$test_root/archive-hardlink"
new_fixture "$tar_fixture" 1.3.2
tar_bundle=8bitPad-1.3.2-linux-x86_64
ln "$tar_fixture/content/$tar_bundle/usr/bin/8bitPad" "$tar_fixture/content/$tar_bundle/hard-link"
tar -czf "$tar_fixture/$tar_bundle.tar.gz" -C "$tar_fixture/content" "$tar_bundle"
(cd "$tar_fixture" && sha256sum "$tar_bundle.tar.gz" > SHA256SUMS.txt)
run_bad archive_hardlink bash "$installer" "${common[@]}" --fixture-directory "$tar_fixture"
grep -q 'Archive links' "$test_root/archive_hardlink.log"

tar_fixture="$test_root/archive-duplicate"
new_fixture "$tar_fixture" 1.3.3
tar_bundle=8bitPad-1.3.3-linux-x86_64
tar --hard-dereference -czf "$tar_fixture/$tar_bundle.tar.gz" -C "$tar_fixture/content" "$tar_bundle" "$tar_bundle/usr/bin/8bitPad"
(cd "$tar_fixture" && sha256sum "$tar_bundle.tar.gz" > SHA256SUMS.txt)
run_bad archive_duplicate bash "$installer" "${common[@]}" --fixture-directory "$tar_fixture"
grep -q 'duplicate path' "$test_root/archive_duplicate.log"

tar_fixture="$test_root/archive-newline"
new_fixture "$tar_fixture" 1.3.4
tar_bundle=8bitPad-1.3.4-linux-x86_64
printf invalid > "$tar_fixture/content/$tar_bundle/"$'name\nsecond-line'
tar -czf "$tar_fixture/$tar_bundle.tar.gz" -C "$tar_fixture/content" "$tar_bundle"
(cd "$tar_fixture" && sha256sum "$tar_bundle.tar.gz" > SHA256SUMS.txt)
run_bad archive_newline bash "$installer" "${common[@]}" --fixture-directory "$tar_fixture"
grep -q 'unsafe or nonportable' "$test_root/archive_newline.log"
assert_equal "$(readlink -- "$test_bin/8bitpad")" "$test_install/versions/1.2.4/8bitpad" 'Rejected archives retain prior version'

mkdir -p -- "$test_root/reject-store" "$test_root/outside-store"
ln -s -- "$test_root/outside-store" "$test_root/reject-store/versions"
printf keep > "$test_root/outside-store/canary"
run_bad version_store_symlink bash "$installer" --repo fixture-owner/8bitpad --fixture-directory "$fixture" --install-dir "$test_root/reject-store" --bin-dir "$test_root/reject-bin" --data-dir "$test_root/reject-data"
assert_equal "$(cat "$test_root/outside-store/canary")" keep 'Nested symlink version store preserved'

mock_bin="$test_root/mock-tools"
mkdir -p "$mock_bin"
cat > "$mock_bin/curl" <<'CURL'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$BIT8PAD_TEST_CURL_LOG"
exit 22
CURL
chmod +x "$mock_bin/curl"
run_bad http_error env PATH="$mock_bin:$PATH" BIT8PAD_TEST_CURL_LOG="$test_root/curl-args" bash "$installer" "${common[@]}" --version 1.2.9
grep -q '^https://github.com/fixture-owner/8bitpad/releases/download/v1.2.9/SHA256SUMS.txt$' "$test_root/curl-args"
assert_equal "$(readlink -- "$test_bin/8bitpad")" "$test_install/versions/1.2.4/8bitpad" 'HTTP failure retains installation'

printf 'PASS: %s Linux bootstrap cases; fixture paths only, no real 8bitPad launch or default-app changes.\n' "$test_count"
