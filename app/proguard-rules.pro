# Keep JNI native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

-keep class com.vmgo.app.core.NativeVmEngine { *; }
-keep class com.vmgo.app.model.** { *; }
