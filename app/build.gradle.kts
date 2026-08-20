// ==============================================================================
// File: app/build.gradle.kts
// ==============================================================================
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.axiom.shaper"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.axiom.shaper"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        
        // Enable NDK build for the C++ shaping engine
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17 -O2"
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    
    // Link the C++ source directory
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
}
