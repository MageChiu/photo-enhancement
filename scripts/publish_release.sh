#!/usr/bin/env bash
# Each successful build uploads only its own assets to the shared release.
set -euo pipefail
cd "$(dirname "$0")/.."
platform=${1:?Usage: publish_release.sh PLATFORM}
case "$platform" in
  macos-arm64|macos-x86_64|windows-x64|android|ios) ;;
  *) echo "Unknown release platform: $platform" >&2; exit 1 ;;
esac
python_command=${PE_PYTHON:-python3}
if [[ "$GITHUB_REF_TYPE" == tag ]]; then
  tag=$GITHUB_REF_NAME
  prerelease=false
elif [[ "$GITHUB_REF_TYPE" == branch && "$GITHUB_REF_NAME" == release-* ]]; then
  tag="${GITHUB_REF_NAME}-build-${GITHUB_RUN_ID}"
  prerelease=true
else
  echo 'Publishing is restricted to tags and release-* branches.' >&2
  exit 1
fi
# Parse and validate before making any remote changes. Filenames are namespaced
# by platform, so parallel uploads and retries cannot overwrite other platforms.
asset_list="artifacts/release-assets-${platform}.txt"
"$python_command" - "$platform" "$GITHUB_SHA" > "$asset_list" <<'PY'
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path('scripts').resolve()))
from prepare_release import PLATFORM_PACKAGES
platform, commit = sys.argv[1:]
manifest = f'build-info-{platform}.json'
metadata = json.loads((Path('artifacts') / manifest).read_text(encoding='utf-8'))
if metadata['platform'] != platform or metadata['commit'] != commit:
    raise SystemExit('Platform metadata does not match this build')
if metadata['packages'] != PLATFORM_PACKAGES[platform]:
    raise SystemExit('Platform package manifest does not match the expected asset set')
for name in metadata['packages'] + [manifest, f'SHA256SUMS-{platform}']:
    path = Path('artifacts') / name
    if not path.is_file() or not path.stat().st_size:
        raise SystemExit(f'Missing release asset: {name}')
    print(path.as_posix())
PY
notes="artifacts/release-notes-${platform}.md"
cat > "$notes" <<EOF_NOTES
Commit: $GITHUB_SHA
Source: $GITHUB_REF
Build: $GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID

Platforms publish independently as their builds finish. A failed platform does not block the other downloads; its assets can be added by rerunning its failed job.

- macOS: application ZIPs and DMGs; ad-hoc signed, not notarized.
- Windows: x64 MSI and complete portable ZIP with VC++ runtime; unsigned.
- Android: arm64-v8a and x86_64 APKs; development signing.
- iOS: unsigned device app and simulator app; device installation requires signing.
- Each available platform has its own SHA256SUMS-<platform> and build-info-<platform>.json.
EOF_NOTES
release_exists() {
  gh release view "$tag" --json isDraft > /dev/null 2>&1
}
if ! release_exists; then
  if ! gh release create "$tag" --target "$GITHUB_SHA" --title "$tag" \
      --draft --prerelease="$prerelease" --latest=false --notes-file "$notes"; then
    # Another platform may have won the create race. Reuse that release, but
    # fail normally if creation failed for a reason other than an existing one.
    release_found=false
    for attempt in 1 2 3 4 5; do
      if release_exists; then release_found=true; break; fi
      sleep "$attempt"
    done
    if [[ "$release_found" != true ]]; then
      echo "Cannot create or find release $tag" >&2
      exit 1
    fi
  fi
fi
assets=()
while IFS= read -r asset; do
  assets+=("$asset")
done < "$asset_list"
# The release can already be public: add/retry this platform without touching
# the names or checksums belonging to any other platform.
gh release upload "$tag" "${assets[@]}" --clobber
gh release edit "$tag" --draft=false
