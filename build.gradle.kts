// Root build file. Per-module configuration (the native build, Compose,
// minSdk) lives in app/build.gradle.kts -- this project has exactly one
// module today, but keeping config at the module level rather than here
// is what lets a second module (e.g. a future :engine-test module that
// links libnleengine.so for native unit tests, or a :desktop module once
// that future phase arrives) be added without restructuring this file.
plugins {
    id("com.android.application") version "8.7.3" apply false
    id("org.jetbrains.kotlin.android") version "2.0.21" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.0.21" apply false
}
