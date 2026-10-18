plugins {
    id("com.android.application")
}

// Release signing comes from the environment (CI secrets); without it, release builds are signed with the debug key.
val releaseKeystore: String? = System.getenv("TQC_KEYSTORE")

android {
    namespace = "io.github.spoopyghosty0.testquestcontrollers"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "io.github.spoopyghosty0.testquestcontrollers"
        minSdk = 29
        targetSdk = 32
        versionCode = 3
        versionName = "0.2.1"
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    signingConfigs {
        if (releaseKeystore != null) {
            create("release") {
                storeFile = file(releaseKeystore)
                storePassword = System.getenv("TQC_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("TQC_KEY_ALIAS")
                keyPassword = System.getenv("TQC_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    buildFeatures {
        prefab = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    lint {
        // Quest apps target API 32; this one is never published on Google Play
        disable += "ExpiredTargetSdkVersion"
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

dependencies {
    implementation("org.khronos.openxr:openxr_loader_for_android:1.1.65")
}
