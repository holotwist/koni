#!/usr/bin/env bash
set -e
set -o pipefail

BUILD_DIR="build"
DIST_DIR="dist"
PORTABLE=OFF
SEMI_STATIC=OFF
MODERN_X86=OFF
X86_LEVEL="x86-64-v3"
BUILD_TYPE="Release"
BUILD_SPARKLES=OFF
PACKAGE=0
PACKAGE_VER=""
CLEAN=0
CMAKE_FLAGS=()

print_help() {
    cat << EOF
Usage: ./build.sh [OPTIONS] [-- <additional cmake flags>]

Options:
  -p,  --package [ver]        Build release binaries and create .tar.gz packages
  -ps, --package-static [ver] Package release with semi-static linking (-static suffix)
       --semi-static          Link dependencies statically (.a) where available
       --portable             Compile for generic baseline architecture1
  -s, --sparkles          Build Koni Sparkles graphical UI (requires raylib, opt-in)
      --no-sparkles       Ensure Sparkles GUI is disabled (default)
  -m, --modern            Build for modern x86_64 (x86-64-v3) instead of generic x86-64
  -l, --level <level>     Specify x86_64 level when modern is used (x86-64-v2, x86-64-v3, x86-64-v4)
  -c, --clean             Wipe the build directory before configuring
  -d, --debug             Configure build in Debug mode
  -r, --release           Configure build in Release mode (default: -O3 + stripped symbols)
  -h, --help              Show this help message

Examples:
  ./build.sh                         Build CLI player with -O3, native tuning, stripped
  ./build.sh -s                      Build CLI player and Koni Sparkles GUI
  ./build.sh -p                      Build portable binaries & package into dist/*.tar.gz
  ./build.sh -p v1.0.0               Package release tarballs with explicit version tag
  ./build.sh -c -r                   Clean wipe and rebuild
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -ps|--package-static)
            PACKAGE=1
            SEMI_STATIC=ON
            PORTABLE=ON
            BUILD_TYPE="Release"
            BUILD_SPARKLES=ON
            if [[ $# -gt 1 && "$2" != -* ]]; then
                PACKAGE_VER="$2"
                shift 2
            else
                shift
            fi
            ;;
        --semi-static)
            SEMI_STATIC=ON
            shift
            ;;
        -p|--package)
            PACKAGE=1
            PORTABLE=ON
            BUILD_TYPE="Release"
            BUILD_SPARKLES=ON
            if [[ $# -gt 1 && "$2" != -* ]]; then
                PACKAGE_VER="$2"
                shift 2
            else
                shift
            fi
            ;;
        --portable)
            PORTABLE=ON
            shift
            ;;
        -s|--sparkles)
            BUILD_SPARKLES=ON
            shift
            ;;
        --no-sparkles)
            BUILD_SPARKLES=OFF
            shift
            ;;
        -m|--modern)
            MODERN_X86=ON
            PORTABLE=ON
            shift
            ;;
        -l|--level)
            X86_LEVEL="$2"
            PORTABLE=ON
            MODERN_X86=ON
            shift 2
            ;;
        -c|--clean)
            CLEAN=1
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        -h|--help)
            print_help
            exit 0
            ;;
        --)
            shift
            CMAKE_FLAGS+=("$@")
            break
            ;;
        *)
            CMAKE_FLAGS+=("$1")
            shift
            ;;
    esac
done

if [ "$CLEAN" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    echo "==> Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

CMAKE_CONFIG_ARGS=(
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DPORTABLE_BUILD="$PORTABLE"
    -DSEMI_STATIC="$SEMI_STATIC"
    -DMODERN_X86_64="$MODERN_X86"
    -DBUILD_SPARKLES="$BUILD_SPARKLES"
)

if [ "$MODERN_X86" = "ON" ]; then
    CMAKE_CONFIG_ARGS+=("-DX86_64_LEVEL=$X86_LEVEL")
fi

if [ ${#CMAKE_FLAGS[@]} -gt 0 ]; then
    CMAKE_CONFIG_ARGS+=("${CMAKE_FLAGS[@]}")
fi

echo "==> Configuring with: cmake ${CMAKE_CONFIG_ARGS[*]}"
cmake "${CMAKE_CONFIG_ARGS[@]}"

echo "==> Building project..."

BUILD_CMD=(cmake --build "$BUILD_DIR" --parallel)
if command -v stdbuf >/dev/null 2>&1; then
    BUILD_CMD=(stdbuf -oL -eL "${BUILD_CMD[@]}")
fi

if [ -t 1 ]; then
    "${BUILD_CMD[@]}" 2>&1 | while IFS= read -r line; do
        if [[ "$line" =~ ^\[[[:space:]]*([0-9]+(%|/[0-9]+))\][[:space:]]*(.*) ]]; then
            pct="${BASH_REMATCH[1]}"
            rest="${BASH_REMATCH[3]}"

            if [[ "$rest" =~ Building[[:space:]]C[[:space:]]object[[:space:]]+(.*) ]]; then
                file="${BASH_REMATCH[1]}"
                file="${file#CMakeFiles/*.dir/}"
                file="${file%.o}"
                printf "\r\033[K\033[1;32m[%4s]\033[0m %s" "$pct" "$file"
            elif [[ "$rest" =~ Linking[[:space:]]+(.*) ]]; then
                target="${BASH_REMATCH[1]}"
                target="${target#*executable }"
                target="${target#*library }"
                printf "\r\033[K\033[1;36m[%4s]\033[0m Linking %s" "$pct" "$target"
            elif [[ "$rest" =~ Built[[:space:]]target[[:space:]]+(.*) ]]; then
                printf "\r\033[K\033[1;34m[%4s]\033[0m Built %s" "$pct" "${BASH_REMATCH[1]}"
            fi
        else
            printf "\n%s" "$line"
        fi
    done
    printf "\r\033[K\033[1;32m==> Build complete.\033[0m\n"
else
    "${BUILD_CMD[@]}"
fi

if [ "$PACKAGE" -eq 1 ]; then
    echo "==> Packaging release tarballs..."

    # Determine Version
    VERSION="$PACKAGE_VER"
    if [ -z "$VERSION" ]; then
        if command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
            VERSION=$(git describe --tags --always 2>/dev/null || echo "v1.0.0")
        else
            VERSION="v1.0.0"
        fi
    fi
    [[ "$VERSION" != v* ]] && VERSION="v${VERSION}"

    # Determine Target OS & Architecture
    OS_NAME="$(uname -s | tr '[:upper:]' '[:lower:]')"
    RAW_ARCH="$(uname -m)"
    case "$RAW_ARCH" in
        x86_64|amd64) ARCH="x86_64" ;;
        aarch64|arm64) ARCH="aarch64" ;;
        armv7l|armv7) ARCH="armv7" ;;
        i686|i386) ARCH="x86" ;;
        *) ARCH="$RAW_ARCH" ;;
    esac

    # Hash helper (sha256sum or shasum -a 256)
    calc_sha256() {
        local file="$1"
        if command -v sha256sum >/dev/null 2>&1; then
            sha256sum "$file"
        elif command -v shasum >/dev/null 2>&1; then
            shasum -a 256 "$file"
        else
            openssl dgst -sha256 "$file" | awk '{print $NF "  " "'"$file"'"}'
        fi
    }

    STATIC_TAG=""
    if [ "$SEMI_STATIC" = "ON" ]; then
        STATIC_TAG="-static"
    fi

    mkdir -p "$DIST_DIR"

    # Package koni-cli
    CLI_NAME="koni-cli-${VERSION}-${OS_NAME}-${ARCH}${STATIC_TAG}"
    CLI_STAGE="${DIST_DIR}/${CLI_NAME}"
    rm -rf "$CLI_STAGE"
    mkdir -p "$CLI_STAGE"

    cp "${BUILD_DIR}/koni" "${CLI_STAGE}/"
    command -v strip >/dev/null 2>&1 && strip -s "${CLI_STAGE}/koni" 2>/dev/null || true

    for doc in README.md LICENSE LICENSE.txt NOTICE.txt src/codecs/pxtone_codec/NOTICE.txt; do
        [ -f "$doc" ] && cp "$doc" "${CLI_STAGE}/"
    done

    CLI_TAR="${CLI_NAME}.tar.gz"
    tar -czf "${DIST_DIR}/${CLI_TAR}" -C "$DIST_DIR" "$CLI_NAME"
    (cd "$DIST_DIR" && calc_sha256 "$CLI_TAR" > "${CLI_TAR}.sha256")
    rm -rf "$CLI_STAGE"
    echo -e "  \033[1;32m[+]\033[0m Created ${DIST_DIR}/${CLI_TAR}"

    # Package koni-full (if koni-sparkles was built)
    if [ -f "${BUILD_DIR}/koni-sparkles" ]; then
        FULL_NAME="koni-full-${VERSION}-${OS_NAME}-${ARCH}${STATIC_TAG}"
        FULL_STAGE="${DIST_DIR}/${FULL_NAME}"
        rm -rf "$FULL_STAGE"
        mkdir -p "$FULL_STAGE"

        cp "${BUILD_DIR}/koni" "${FULL_STAGE}/"
        cp "${BUILD_DIR}/koni-sparkles" "${FULL_STAGE}/"
        command -v strip >/dev/null 2>&1 && strip -s "${FULL_STAGE}/koni" "${FULL_STAGE}/koni-sparkles" 2>/dev/null || true

        [ -d "resources" ] && cp -r resources "${FULL_STAGE}/"

        for doc in README.md LICENSE LICENSE.txt NOTICE.txt CREDITS.md src/codecs/pxtone_codec/NOTICE.txt; do
            [ -f "$doc" ] && cp "$doc" "${FULL_STAGE}/"
        done

        FULL_TAR="${FULL_NAME}.tar.gz"
        tar -czf "${DIST_DIR}/${FULL_TAR}" -C "$DIST_DIR" "$FULL_NAME"
        (cd "$DIST_DIR" && calc_sha256 "$FULL_TAR" > "${FULL_TAR}.sha256")
        rm -rf "$FULL_STAGE"
        echo -e "  \033[1;32m[+]\033[0m Created ${DIST_DIR}/${FULL_TAR}"
    else
        echo -e "  \033[1;33m[!]\033[0m Raylib/Sparkles not built; skipped koni-full package."
    fi

    echo -e "\033[1;32m==> All packages ready in ${DIST_DIR}/\033[0m"
fi