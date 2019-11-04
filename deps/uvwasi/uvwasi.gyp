{
  'targets': [
    {
      'target_name': 'uvwasi',
      'type': 'static_library',
      'cflags': ['-fvisibility=hidden'],
      'xcode_settings': {
        'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES',  # -fvisibility=hidden
      },
      'include_dirs': ['include'],
      'sources': [
        'src/clocks.c',
        'src/fd_table.c',
        'src/uv_mapping.c',
        'src/uvwasi.c',
      ],
      'dependencies': [
        '../uv/uv.gyp:libuv',
      ],
      'direct_dependent_settings': {
        'include_dirs': ['include']
      },
    }
  ]
}
