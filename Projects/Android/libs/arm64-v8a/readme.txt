No per-HMD OpenXR loader is required here.

JKXR links against the generic Khronos loader in:

Projects/AndroidPrebuilt/jni/libopenxr_loader.so

At runtime the system OpenXR broker selects the active Meta or Pico runtime.
