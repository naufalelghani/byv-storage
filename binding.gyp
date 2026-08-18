{
  "targets": [
    {
      "target_name": "byv_core",

      "sources": [
        "byv_core.cpp"
      ],

      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],

      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],

      "defines": [
        "NAPI_CPP_EXCEPTIONS"
        # "BYV_ENABLE_PROFILE"
      ],

      "cflags!": [
        "-fno-exceptions"
      ],

      "cflags_cc!": [
        "-fno-exceptions"
      ],

      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1,
          "RuntimeLibrary": 2
        }
      }
    }
  ]
}