plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.nle.editor"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.nle.editor"
        // minSdk 28 specifically because Decoder.h's SurfaceTexture-attach
        // path uses the ASurfaceTexture NDK API (android/surface_texture.h),
        // which is API 28+. This is the one hard floor in the whole engine
        // -- see the header comment there before lowering this.
        minSdk = 28
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0-phase1"

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += "-DANDROID_STL=c++_shared"
            }
        }
        ndk {
            // arm64-v8a first since it's what every relevant test device
            // and the vast majority of the target install base runs;
            // x86_64 is kept for emulator development builds.
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    buildFeatures {
        compose = true
    }

    composeOptions { kotlinCompilerExtensionVersion = "1.5.14" }
    
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2024.09.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.9.2")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.6")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.6")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
    // org.json (used by EditorRepository to parse native snapshots) ships
    // with the Android platform SDK -- no dependency entry needed.
}
