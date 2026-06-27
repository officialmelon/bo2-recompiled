@echo off
setlocal EnableExtensions

set "ROOT_DIR=%~dp0..\.."
for %%I in ("%ROOT_DIR%") do set "ROOT_DIR=%%~fI"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=debug"
if /i "%CONFIG%"=="debug" (
  set "PRESET=android-arm64-debug"
  set "REX_SUFFIX=d"
) else if /i "%CONFIG%"=="release" (
  set "PRESET=android-arm64-release"
  set "REX_SUFFIX="
) else (
  echo Unknown configuration "%CONFIG%". Use debug or release.
  exit /b 1
)

if "%ANDROID_SDK_ROOT%"=="" set "ANDROID_SDK_ROOT=%LOCALAPPDATA%\Android\Sdk"
if "%ANDROID_NDK_HOME%"=="" set "ANDROID_NDK_HOME=C:\Users\braxt\ndk-install\android-ndk-r27"
if "%JAVA_HOME%"=="" set "JAVA_HOME=%USERPROFILE%\.codex\jdk\temurin17"

set "BUILD_TOOLS=%ANDROID_SDK_ROOT%\build-tools\35.0.0"
set "ANDROID_JAR=%ANDROID_SDK_ROOT%\platforms\android-35\android.jar"
set "APK_ROOT=%ROOT_DIR%\android\build\apkwork"
set "OUT_DIR=%ROOT_DIR%\android\build\outputs\apk\%CONFIG%"
set "UNSIGNED_APK=%APK_ROOT%\unsigned-unaligned.apk"
set "ALIGNED_APK=%APK_ROOT%\aligned-unsigned.apk"
set "FINAL_APK=%OUT_DIR%\bo2-recompiled-%CONFIG%.apk"
set "LIB_DIR=%APK_ROOT%\staging\lib\arm64-v8a"
set "REX_OUT=%ROOT_DIR%\..\rexglue-sdk\out\linux-arm64"
for %%I in ("%REX_OUT%") do set "REX_OUT=%%~fI"

if not exist "%BUILD_TOOLS%\aapt2.exe" (
  echo Android build-tools 35.0.0 not found under "%ANDROID_SDK_ROOT%".
  exit /b 1
)
if not exist "%ANDROID_JAR%" (
  echo Android platform android-35 not found under "%ANDROID_SDK_ROOT%".
  exit /b 1
)
if not exist "%JAVA_HOME%\bin\javac.exe" (
  echo JAVA_HOME does not point to a JDK with javac: "%JAVA_HOME%".
  exit /b 1
)

if exist "%APK_ROOT%" rmdir /s /q "%APK_ROOT%"
mkdir "%APK_ROOT%\gen" || exit /b 1
mkdir "%APK_ROOT%\classes" || exit /b 1
mkdir "%APK_ROOT%\dex" || exit /b 1
mkdir "%LIB_DIR%" || exit /b 1
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%" || exit /b 1

copy /y "%ROOT_DIR%\default\out\build\%PRESET%\libdefault.so" "%LIB_DIR%\" || exit /b 1
copy /y "%ROOT_DIR%\default_mp\out\build\%PRESET%\libdefault_mp.so" "%LIB_DIR%\" || exit /b 1
copy /y "%REX_OUT%\librexruntime%REX_SUFFIX%.so" "%LIB_DIR%\" || exit /b 1
copy /y "%REX_OUT%\libTracyClient%REX_SUFFIX%.so" "%LIB_DIR%\" || exit /b 1
copy /y "%ANDROID_NDK_HOME%\toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\libc++_shared.so" "%LIB_DIR%\" || exit /b 1

"%BUILD_TOOLS%\aapt2.exe" link --manifest "%ROOT_DIR%\android\app\src\main\AndroidManifest.xml" -I "%ANDROID_JAR%" --java "%APK_ROOT%\gen" -o "%UNSIGNED_APK%" --min-sdk-version 21 --target-sdk-version 35 || exit /b 1

set "JAVA_SOURCES=%APK_ROOT%\java_sources.txt"
dir /s /b "%ROOT_DIR%\android\app\src\main\java\*.java" "%APK_ROOT%\gen\*.java" > "%JAVA_SOURCES%" || exit /b 1
"%JAVA_HOME%\bin\javac.exe" -source 8 -target 8 -bootclasspath "%ANDROID_JAR%" -d "%APK_ROOT%\classes" @"%JAVA_SOURCES%" || exit /b 1

set "PATH=%JAVA_HOME%\bin;%PATH%"
"%JAVA_HOME%\bin\jar.exe" cf "%APK_ROOT%\classes.jar" -C "%APK_ROOT%\classes" . || exit /b 1
call "%BUILD_TOOLS%\d8.bat" --min-api 21 --output "%APK_ROOT%\dex" "%APK_ROOT%\classes.jar" || exit /b 1

pushd "%APK_ROOT%\dex" || exit /b 1
"%JAVA_HOME%\bin\jar.exe" uf "%UNSIGNED_APK%" classes.dex || exit /b 1
popd
"%JAVA_HOME%\bin\jar.exe" uf "%UNSIGNED_APK%" -C "%APK_ROOT%\staging" lib || exit /b 1

"%BUILD_TOOLS%\zipalign.exe" -f -p 4 "%UNSIGNED_APK%" "%ALIGNED_APK%" || exit /b 1

set "DEBUG_KEYSTORE=%ROOT_DIR%\android\debug.keystore"
if not exist "%DEBUG_KEYSTORE%" (
  "%JAVA_HOME%\bin\keytool.exe" -genkeypair -v -keystore "%DEBUG_KEYSTORE%" -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" || exit /b 1
)

call "%BUILD_TOOLS%\apksigner.bat" sign --ks "%DEBUG_KEYSTORE%" --ks-pass pass:android --key-pass pass:android --out "%FINAL_APK%" "%ALIGNED_APK%" || exit /b 1
call "%BUILD_TOOLS%\apksigner.bat" verify --verbose --print-certs "%FINAL_APK%" || exit /b 1

echo APK written to "%FINAL_APK%"
