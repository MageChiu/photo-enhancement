#!/usr/bin/env bash
# Publish only after every platform and release assembly passed.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$GITHUB_REF_TYPE" == tag ]]; then
  tag=$GITHUB_REF_NAME
  prerelease=false
elif [[ "$GITHUB_REF_TYPE" == branch && "$GITHUB_REF_NAME" == release-* ]]; then
  # Each branch build gets an immutable tag at its exact tested commit.
  tag="${GITHUB_REF_NAME}-build-${GITHUB_RUN_ID}"
  prerelease=true
else
  echo 'Publishing is restricted to tags and release-* branches.' >&2
  exit 1
fi
cat > artifacts/release-notes.md <<EOF_NOTES
Commit: $GITHUB_SHA
Source: $GITHUB_REF
Build: $GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID

- macOS: arm64 and x86_64 application ZIPs and drag-to-Applications DMGs, ad-hoc signed, not notarized.
- Windows: x64 MSI installer and portable ZIP, including application-local VC++ runtime; unsigned.
- Android: arm64-v8a and x86_64 APKs, development signing.
- iOS: unsigned arm64 device app (requires signing before installation) and arm64 simulator debug app.
- Verify downloads with SHA256SUMS; build-info.json records provenance.
EOF_NOTES
# Recover an interrupted draft, while keeping already published releases immutable.
if gh release view "$tag" --json isDraft > artifacts/existing-release.json; then
  if [[ "$(python3 -c 'import json; print(json.load(open("artifacts/existing-release.json"))["isDraft"])')" != True ]]; then
    echo "Release $tag already published; leaving its assets unchanged."
    exit 0
  fi
else
  gh release create "$tag" --target "$GITHUB_SHA" --title "$tag" \
    --draft --prerelease="$prerelease" --latest=false --notes-file artifacts/release-notes.md
fi
# Upload the explicit manifest so local notes/metadata from gh are never attached.
assets=()
while IFS= read -r asset; do
  assets+=("$asset")
done < <(python3 -c 'import json; d=json.load(open("artifacts/build-info.json")); print("\n".join("artifacts/" + p for p in d["packages"] + ["build-info.json", "SHA256SUMS"]))')
gh release upload "$tag" "${assets[@]}" --clobber
gh release edit "$tag" --draft=false
