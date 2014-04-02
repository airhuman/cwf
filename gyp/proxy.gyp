{
  'conditions': [
    ['OS=="linux"', {
      'target_defaults': {
        'cflags': ['-fPIC', '-g', '-O2',],
        'defines': ['OS_LINUX'],
      },
    },],
    ['OS=="win"', {
      'target_defaults': {
        # 'cflags': ['-fPIC', '-g', '-O2',],
        'defines': ['WIN32', 'OS_WIN', 'NOMINMAX', 'UNICODE', '_UNICODE', 'WIN32_LEAN_AND_MEAN', '_WIN32_WINNT=0x0501'],
        'msvs_settings': {
          'VCLinkerTool': {'GenerateDebugInformation': 'true',},
          'VCCLCompilerTool': {'DebugInformationFormat': '3',},
        },
		'include_dirs': ['%(INCLUDE)'],
      },
    },],
  ],
  'targets': [
    {
      'target_name': 'proxy_server',
      'type': 'executable',
      'msvs_guid': '11384247-5F84-4DAE-8AB2-655600A90963',
      'dependencies': [
        'base3.gyp:base3',
      ],
      'conditions':[
        ['OS=="linux"', {'libraries': ['-lboost_system', '-lboost_thread', '-lpthread'] }],
        ['OS=="win"', {'libraries': ['ws2_32.lib'] }],
      ],
      'sources': [
        '../src/proxy/connection.cc',
        '../src/proxy/server.cc',
        '../src/proxy/proxy_main.cc',
      ],
      'include_dirs': [
        '../src',
      ],
    },
  ],
}
