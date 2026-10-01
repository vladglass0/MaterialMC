cmake_minimum_required(VERSION 3.15)
project(launcher Java)
find_package(Java 1.7 REQUIRED COMPONENTS Development)

include(UseJava)
set(CMAKE_JAVA_JAR_ENTRY_POINT org.materialmc.EntryPoint)
set(CMAKE_JAVA_COMPILE_FLAGS -target 7 -source 7)

set(SRC
    org/materialmc/EntryPoint.java
    org/materialmc/launcher/Launcher.java
    org/materialmc/launcher/impl/AbstractLauncher.java
    org/materialmc/launcher/impl/StandardLauncher.java
    org/materialmc/exception/ParameterNotFoundException.java
    org/materialmc/exception/ParseException.java
    org/materialmc/utils/Parameters.java
    org/materialmc/utils/ReflectionUtils.java
    org/materialmc/utils/logging/Level.java
    org/materialmc/utils/logging/Log.java
    org/materialmc/legacy/LegacyProxy.java
)

set(LEGACY_SRC
    legacy/org/materialmc/legacy/LegacyFrame.java
    legacy/org/materialmc/legacy/LegacyLauncher.java
    legacy/org/materialmc/legacy/fix/online/Handler.java
    legacy/org/materialmc/legacy/fix/online/OnlineFixes.java
    legacy/org/materialmc/legacy/fix/online/OnlineModeFix.java
    legacy/org/materialmc/legacy/fix/online/SkinFix.java
    legacy/org/materialmc/legacy/utils/Base64.java
    legacy/org/materialmc/legacy/utils/api/MojangApi.java
    legacy/org/materialmc/legacy/utils/api/Texture.java
    legacy/org/materialmc/legacy/utils/json/JsonParseException.java
    legacy/org/materialmc/legacy/utils/json/JsonParser.java
    legacy/org/materialmc/legacy/utils/url/ByteArrayUrlConnection.java
    legacy/org/materialmc/legacy/utils/url/UrlUtils.java
    legacy/net/minecraft/Launcher.java
    legacy/org/materialmc/legacy/LegacyProxy.java
)

add_jar(NewLaunch ${SRC})
add_jar(NewLaunchLegacy ${LEGACY_SRC} INCLUDE_JARS NewLaunch)
install_jar(NewLaunch "${JARS_DEST_DIR}")
install_jar(NewLaunchLegacy "${JARS_DEST_DIR}")
