
# Android structure for BO2 Recompiled

To build the APK, you can use the following structure.
This will use CMake to build the native libraries.

android/
  app/
    build.gradle
    src/main/
      AndroidManifest.xml
      java/com/rex/bo2/MainActivity.java
      java/com/rex/bo2/NativeBridge.java
  build.gradle
  settings.gradle
