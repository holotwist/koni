#!/usr/bin/env bash
set -e
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Validate environment variables
: "${ANDROID_HOME:?Please set ANDROID_HOME or ANDROID_SDK_ROOT}"
: "${ANDROID_NDK_ROOT:?Please set ANDROID_NDK_ROOT}"
: "${RAYLIB_DIR:?Please set RAYLIB_DIR to your cloned raylib repo}"
: "${SQLITE_AMALG:?Please set SQLITE_AMALG to directory with sqlite3.c and sqlite3.h}"

TARGET_ABI="${TARGET_ABI:-arm64-v8a}"
ANDROID_API="${ANDROID_API:-34}"

# Find build-tools and android.jar
BUILD_TOOLS_DIR="$(ls -d "${ANDROID_HOME}/build-tools/"* 2>/dev/null | sort -V | tail -n 1)"
if [ -z "$BUILD_TOOLS_DIR" ] || [ ! -x "${BUILD_TOOLS_DIR}/aapt2" ]; then
    echo "Error: aapt2 not found in ${ANDROID_HOME}/build-tools/"
    exit 1
fi

ANDROID_JAR="${ANDROID_HOME}/platforms/android-${ANDROID_API}/android.jar"
if [ ! -f "$ANDROID_JAR" ]; then
    # Fall back to highest available platform
    ANDROID_JAR="$(ls -d "${ANDROID_HOME}/platforms/android-"*/android.jar 2>/dev/null | sort -V | tail -n 1)"
fi

echo "==> Using Build Tools: ${BUILD_TOOLS_DIR}"
echo "==> Using Platform:    ${ANDROID_JAR}"
echo "==> Target ABI:        ${TARGET_ABI}"

BUILD_DIR="build_android_${TARGET_ABI}"
mkdir -p "$BUILD_DIR"

OPUS_CMAKE_ARGS=()
if [ -n "$OGG_DIR" ] && [ -n "$OPUS_DIR" ] && [ -n "$OPUSFILE_DIR" ]; then
    echo "==> Enabling Opus Codec support..."
    OPUS_CMAKE_ARGS+=(
        -DOGG_SOURCE_DIR="${OGG_DIR}"
        -DOPUS_SOURCE_DIR="${OPUS_DIR}"
        -DOPUSFILE_SOURCE_DIR="${OPUSFILE_DIR}"
    )
fi

# Compile C/C++ shared library
echo "==> Compiling native library..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="${TARGET_ABI}" \
    -DANDROID_PLATFORM="android-26" \
    -DANDROID_STL="c++_static" \
    -DCMAKE_BUILD_TYPE="Release" \
    -DCUSTOMIZE_BUILD="ON" \
    -DSUPPORT_MODULE_RAUDIO="OFF" \
    -DUSE_AUDIO="OFF" \
    -DRAYLIB_SOURCE_DIR="${RAYLIB_DIR}" \
    -DSQLITE_AMALGAMATION_DIR="${SQLITE_AMALG}" \
    "${OPUS_CMAKE_ARGS[@]}"

cmake --build "$BUILD_DIR" --parallel

# Compile Java activity and generate dex
echo "==> Compiling Java sources and running d8..."
mkdir -p "${BUILD_DIR}/obj_java"
javac -encoding UTF-8 \
    -source 8 -target 8 \
    -cp "$ANDROID_JAR" \
    -d "${BUILD_DIR}/obj_java" \
    src/com/holotwist/koni/MainActivity.java

"${BUILD_TOOLS_DIR}/d8" \
    --lib "$ANDROID_JAR" \
    --output "${BUILD_DIR}" \
    $(find "${BUILD_DIR}/obj_java" -name "*.class")

# Compile resources with AAPT2
echo "==> Compiling resources with aapt2..."
mkdir -p "${BUILD_DIR}/compiled_res"
"${BUILD_TOOLS_DIR}/aapt2" compile --dir res -o "${BUILD_DIR}/compiled_res/res.zip"

# Prepare assets directory
mkdir -p "${BUILD_DIR}/assets"
if [ -f "../../resources/unifont.otf" ]; then
    cp "../../resources/unifont.otf" "${BUILD_DIR}/assets/"
fi

# Link APK with AAPT2
echo "==> Linking APK with aapt2..."
"${BUILD_TOOLS_DIR}/aapt2" link \
    -I "$ANDROID_JAR" \
    --manifest AndroidManifest.xml \
    -o "${BUILD_DIR}/unaligned.apk" \
    -A "${BUILD_DIR}/assets" \
    "${BUILD_DIR}/compiled_res/res.zip"

# Pack native library into APK
echo "==> Packaging native libraries into APK..."
mkdir -p "${BUILD_DIR}/lib/${TARGET_ABI}"
cp "${BUILD_DIR}/libkoni-sparkles.so" "${BUILD_DIR}/lib/${TARGET_ABI}/"
${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip -s "${BUILD_DIR}/lib/${TARGET_ABI}/libkoni-sparkles.so" 2>/dev/null || true

(cd "$BUILD_DIR" && zip -u "unaligned.apk" "classes.dex")
(cd "$BUILD_DIR" && zip -u -r "unaligned.apk" "lib/${TARGET_ABI}/libkoni-sparkles.so")

# Zipalign
echo "==> Running zipalign..."
"${BUILD_TOOLS_DIR}/zipalign" -p -f -v 4 "${BUILD_DIR}/unaligned.apk" "${BUILD_DIR}/koni-aligned.apk" >/dev/null

# Generate Debug Keystore (if needed) and sign APK
KEYSTORE="${BUILD_DIR}/debug.keystore"
if [ ! -f "$KEYSTORE" ]; then
    echo "==> Creating debug keystore..."
    keytool -genkey -v -keystore "$KEYSTORE" -storepass android -alias androiddebugkey \
        -keypass android -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=KoniDebug,O=HoloTwist,C=US" 2>/dev/null
fi

echo "==> Signing APK with apksigner..."
"${BUILD_TOOLS_DIR}/apksigner" sign \
    --ks "$KEYSTORE" \
    --ks-pass pass:android \
    --out "${BUILD_DIR}/koni-debug.apk" \
    "${BUILD_DIR}/koni-aligned.apk"

echo -e "\033[1;32m==> Build complete\033[0m"
echo -e "    Output APK: \033[1;36m${SCRIPT_DIR}/${BUILD_DIR}/koni-debug.apk\033[0m"