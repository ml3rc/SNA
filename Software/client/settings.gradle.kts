rootProject.name = "sna-client"

pluginManagement {
    repositories {
        google()
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
        // usb-serial-for-android (mik3y) is published via JitPack, not Maven Central.
        maven("https://jitpack.io")
    }
}

include(":composeApp")
