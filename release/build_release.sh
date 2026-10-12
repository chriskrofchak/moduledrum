#!/usr/bin/env bash
# Builds a Moduledrum release for macOS: a clean Release build, the tests that need no OS file, the signed bundles, an
# installer (.pkg) and a .zip of the three bundles, both in dist/. Installs nothing.
#
#   release/build_release.sh [-v version] [-s identity] [-n] [-a arm64|universal]
#
#   -v version   the release version x.y.z (default: MD_VERSION in CMakeLists.txt); the bundles' version, the file names
#   -s identity  the code-signing identity (default "-": ad-hoc, which runs on this Mac only); for a release, the name
#                or SHA-1 of a "Developer ID Application" certificate in the keychain
#   -n           notarize the installer and staple the tickets (needs a real identity, MD_INSTALLER_IDENTITY and
#                MD_NOTARY_PROFILE); without it the script says that notarization was skipped
#   -a archs     arm64 (the default) or universal (arm64 + x86_64). The x86_64 slice runs dsp56300's x64 JIT, whose patch
#                still lacks a Z-flag fix (.clew increment 0010, docs/DEVELOPMENT.md) and which no Intel Mac has run: it
#                stays out of the default until that is in.
#
# Environment:
#   MD_INSTALLER_IDENTITY  "Developer ID Installer: ..." certificate that signs the .pkg; without it the .pkg is unsigned
#                          (Gatekeeper refuses an unsigned installer that was downloaded, and notarytool rejects it)
#   MD_NOTARY_PROFILE      name of a keychain profile for notarytool, made once with
#                            xcrun notarytool store-credentials "<profile>" --apple-id <id> --team-id <team> --password <app-specific password>
#                          (or with --key/--key-id/--issuer for an App Store Connect API key)
#   MD_CMAKE_ARGS          extra arguments for the configure step, e.g. -DFETCHCONTENT_SOURCE_DIR_JUCE=<a JUCE 8.0.9 checkout>
#
# The OS file is never needed and never packaged: the plugin asks for it at run time. The build directory is
# build-release/ (deleted first); the product is dist/Moduledrum-<version>-macos-<arm64|universal>.{pkg,zip}.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-release"
DIST="$ROOT/dist"

VERSION="$(sed -n 's/^ *set(MD_VERSION \([0-9][0-9.]*\)).*/\1/p' "$ROOT/CMakeLists.txt" | head -n 1)"
IDENTITY="-"
NOTARIZE=0
ARCHS_NAME="arm64"

usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed -e '$d' -e 's/^# \{0,1\}//'; }
die() { echo "build_release: $*" >&2; exit 1; }
step() { echo; echo "==> $*"; }

while getopts "v:s:na:h" opt; do
  case "$opt" in
    v) VERSION="$OPTARG" ;;
    s) IDENTITY="$OPTARG" ;;
    n) NOTARIZE=1 ;;
    a) ARCHS_NAME="$OPTARG" ;;
    h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "version '$VERSION' is not x.y.z"
[[ "$(uname -s)" == Darwin ]] || die "this builds the macOS release"
case "$ARCHS_NAME" in
  arm64) OSX_ARCHS="arm64" ;;
  universal) OSX_ARCHS="arm64;x86_64" ;;
  *) die "-a takes arm64 or universal, not '$ARCHS_NAME'" ;;
esac

INSTALLER_IDENTITY="${MD_INSTALLER_IDENTITY:-}"
NAME="Moduledrum-$VERSION-macos-$ARCHS_NAME"
PKG="$DIST/$NAME.pkg"
ZIP="$DIST/$NAME.zip"

for tool in cmake codesign lipo pkgbuild productbuild ditto shasum /usr/libexec/PlistBuddy; do
  command -v "$tool" >/dev/null 2>&1 || die "'$tool' not found"
done
if [[ "$NOTARIZE" == 1 ]]; then
  [[ "$IDENTITY" != "-" ]] || die "-n needs a real signing identity (-s), not the ad-hoc one"
  [[ -n "$INSTALLER_IDENTITY" ]] || die "-n needs MD_INSTALLER_IDENTITY, the Developer ID Installer certificate that signs the .pkg"
  [[ -n "${MD_NOTARY_PROFILE:-}" ]] || die "-n needs MD_NOTARY_PROFILE, a notarytool keychain profile (see the top of this script)"
  xcrun --find notarytool >/dev/null 2>&1 || die "xcrun notarytool not found (Xcode 13 or the command line tools of Xcode 13+)"
fi

GENERATOR=()
if command -v ninja >/dev/null 2>&1; then GENERATOR=(-G Ninja); fi

# A real identity gets Apple's secure timestamp, which notarization requires; the ad-hoc one stays offline.
TIMESTAMP="none"
if [[ "$IDENTITY" != "-" ]]; then TIMESTAMP=""; fi

step "Configuring a clean $ARCHS_NAME Release build of $VERSION in build-release/ (signing as '$IDENTITY')"
if [[ -z "${MD_KEEP_BUILD:-}" ]]; then rm -rf "$BUILD"; fi   # MD_KEEP_BUILD=1: only for working on this script
# MD_OS_SYX stays empty: nothing of the user's OS file is baked into the tools. MD_COPY_PLUGINS=OFF: nothing is installed.
# shellcheck disable=SC2086
cmake -S "$ROOT" -B "$BUILD" ${GENERATOR[@]+"${GENERATOR[@]}"} -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_OSX_ARCHITECTURES=$OSX_ARCHS" -DMD_USE_JIT=ON -DMD_COPY_PLUGINS=OFF -DMD_OS_SYX= \
  -DMD_VERSION="$VERSION" "-DMD_CODESIGN_IDENTITY=$IDENTITY" "-DMD_CODESIGN_TIMESTAMP=$TIMESTAMP" ${MD_CMAKE_ARGS:-}

step "Building"
cmake --build "$BUILD" --parallel

step "Running the tests that need no OS file (the others report as skipped)"
# ctest runs the slice of this Mac (the x86_64 slice of a universal build: on Intel, or under Rosetta, see the README).
env -u MD_OS_SYX -u MD_KITS_SYX ctest --test-dir "$BUILD" --output-on-failure

ART="$BUILD/src/plugin/MdOne_artefacts/Release"
AU="$ART/AU/Moduledrum.component"
VST3="$ART/VST3/Moduledrum.vst3"
APP="$ART/Standalone/Moduledrum.app"

step "Checking the bundles"
for b in "$AU" "$VST3" "$APP"; do
  [[ -d "$b" ]] || die "$b was not built"
  archs="$(lipo -archs "$b/Contents/MacOS/Moduledrum")"
  want="$(echo "$OSX_ARCHS" | tr ';' '\n' | sort | tr '\n' ' ')"
  have="$(echo "$archs" | tr ' ' '\n' | sort | tr '\n' ' ')"
  [[ "$want" == "$have" ]] || die "$b has the architectures '$archs', not '$OSX_ARCHS'"
  codesign --verify --strict --deep "$b" || die "$b: the signature does not verify"
  echo "$(basename "$b"): $archs, signature valid"
done
# The hardened runtime needs the JIT entitlements to run the emulated DSP; codesign keeps them in executables only.
entitlements="$(codesign -d --entitlements - "$APP" 2>/dev/null || true)"
[[ "$entitlements" == *com.apple.security.cs.allow-jit* ]] || die "the standalone app has no JIT entitlement"
echo "Moduledrum.app: com.apple.security.cs.allow-jit and the two other JIT entitlements are in place"

step "Staging"
mkdir -p "$DIST"
rm -rf "$DIST/.stage" "$PKG" "$ZIP" "$DIST/$NAME.sha256" "$DIST/notarytool.log"
STAGE="$DIST/.stage"        # one folder per component package's payload; the folder of the .zip
mkdir -p "$STAGE/au" "$STAGE/vst3" "$STAGE/app" "$STAGE/pkgs" "$STAGE/zip/$NAME"/{AU,VST3,Standalone}
ditto "$AU" "$STAGE/au/Moduledrum.component"
ditto "$VST3" "$STAGE/vst3/Moduledrum.vst3"
ditto "$APP" "$STAGE/app/Moduledrum.app"
ditto "$AU" "$STAGE/zip/$NAME/AU/Moduledrum.component"
ditto "$VST3" "$STAGE/zip/$NAME/VST3/Moduledrum.vst3"
ditto "$APP" "$STAGE/zip/$NAME/Standalone/Moduledrum.app"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$ROOT/README.md" "$STAGE/zip/$NAME/"

# One component package per bundle. pkgbuild's defaults would make each bundle relocatable (the installer then updates a
# copy it finds elsewhere, such as the one in a build folder) and refuse to replace a newer version: both turned off.
component() {   # component <payload folder> <package id> <install location> <package file>
  local plist="$STAGE/$4.plist"
  pkgbuild --analyze --root "$1" "$plist" >/dev/null
  local key
  for key in BundleIsRelocatable BundleIsVersionChecked BundleHasStrictIdentifier; do   # not all of them are in the analysis
    /usr/libexec/PlistBuddy -c "Set :0:$key false" "$plist" 2>/dev/null || /usr/libexec/PlistBuddy -c "Add :0:$key bool false" "$plist"
  done
  /usr/libexec/PlistBuddy -c "Set :0:BundleOverwriteAction upgrade" "$plist" 2>/dev/null \
    || /usr/libexec/PlistBuddy -c "Add :0:BundleOverwriteAction string upgrade" "$plist"
  pkgbuild --root "$1" --component-plist "$plist" --identifier "$2" --version "$VERSION" --install-location "$3" \
    --ownership recommended "$STAGE/pkgs/$4.pkg" >/dev/null
}

step "Building the installer"
component "$STAGE/au"   md.elektron-md.one.au         "/Library/Audio/Plug-Ins/Components" Moduledrum-AU
component "$STAGE/vst3" md.elektron-md.one.vst3       "/Library/Audio/Plug-Ins/VST3"       Moduledrum-VST3
component "$STAGE/app"  md.elektron-md.one.standalone "/Applications"                      Moduledrum-Standalone
sed "s/@VERSION@/$VERSION/g" "$ROOT/release/distribution.xml" > "$STAGE/distribution.xml"
PRODUCT_SIGN=()
if [[ -n "$INSTALLER_IDENTITY" ]]; then PRODUCT_SIGN=(--sign "$INSTALLER_IDENTITY" --timestamp); fi
productbuild --distribution "$STAGE/distribution.xml" --package-path "$STAGE/pkgs" --resources "$ROOT/release/resources" \
  ${PRODUCT_SIGN[@]+"${PRODUCT_SIGN[@]}"} "$PKG"
if [[ -z "$INSTALLER_IDENTITY" ]]; then
  echo "note: the installer is not signed (MD_INSTALLER_IDENTITY is not set)"
fi

if [[ "$NOTARIZE" == 1 ]]; then
  step "Notarizing $(basename "$PKG") (this waits for Apple)"
  LOG="$DIST/notarytool.log"
  xcrun notarytool submit "$PKG" --keychain-profile "$MD_NOTARY_PROFILE" --wait 2>&1 | tee "$LOG" || true
  grep -q "status: Accepted" "$LOG" || die "notarization was not accepted; see $LOG ('xcrun notarytool log <id> --keychain-profile ...')"
  xcrun stapler staple "$PKG"
  xcrun stapler validate "$PKG"
  # The ticket covers the bundles inside too: staple them, so that the .zip works offline as well.
  for b in "$STAGE/zip/$NAME/AU/Moduledrum.component" "$STAGE/zip/$NAME/VST3/Moduledrum.vst3" \
           "$STAGE/zip/$NAME/Standalone/Moduledrum.app"; do
    xcrun stapler staple "$b" || echo "warning: could not staple $(basename "$b"); it still passes Gatekeeper online"
  done
else
  echo
  echo "Notarization skipped (-n not given): Gatekeeper will ask for approval of a download of this build."
fi

step "Making the .zip"
ditto -c -k --keepParent "$STAGE/zip/$NAME" "$ZIP"

rm -rf "$STAGE"
( cd "$DIST" && shasum -a 256 "$NAME.pkg" "$NAME.zip" > "$NAME.sha256" )

step "dist/"
ls -lh "$DIST"
echo
cat "$DIST/$NAME.sha256"
