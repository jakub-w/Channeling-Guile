project('channeling-guile', 'cpp',
        default_options: ['cpp_std=c++17'])

# Build types
if get_option('buildtype').startswith('debug')
  add_project_arguments('-Wall', '-Wextra', '-Werror', '-DFORTIFY_SOURCE=1',
                        '-ggdb3',
                        # '-fsanitize=address',
                        # '-fsanitize=leak',
                        # '-fsanitize=undefined',
			'-Og',
                        language : 'cpp')
  add_project_link_arguments(
    # '-lasan',
    # '-ltsan',
    # '-lubsan',
    language : 'cpp')
elif get_option('buildtype') == 'release'
  add_project_arguments('-DNDEBUG', '-O2',
                        language : 'cpp')
endif


# Dependencies
channeling_deps = [dependency('libzmq'),
		   dependency('libsodium'),
		   dependency('msgpack')]
guile_dep = dependency('guile-3.0')

channeling_dep = declare_dependency(
  sources: ['include/Channeling/EncryptionCommon.cpp',
	    'include/Channeling/SodiumCipherStream/SodiumCipherStream.cpp'],
  include_directories: ['include/Channeling'],
  dependencies: [dependency('libzmq'),
		 dependency('libsodium'),
		 dependency('msgpack'),
		 dependency('spdlog')])


# TODO: This is stupid, why should I state this. Just make a fucking shared
#       lib out of Channeling

# Binaries
shared_library('channeling-guile',
               sources: ['channeling_wrapper.cpp'],
	       include_directories: ['include'],
	       link_args: ['-lpthread'],
               dependencies: [guile_dep, channeling_dep])
