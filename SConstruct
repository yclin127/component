env = Environment()

#env.ParseConfig( 'pkg-config --cflags glib-2.0' )

#env.Append(CPPPATH = ['/usr/local/include/'])

env.Append(CCFLAGS = ['-g','-Wall'])

#env.Append(CPPDEFINES=['BIG_ENDIAN'])
#env.Append(CPPDEFINES={'RELEASE_BUILD' : '1'})

#env.Append(LIBPATH = ['/usr/local/lib/'])
#env.Append(LIBS = ['SDL_image','GL'])

#env.Append(LINKFLAGS = ['-Wl,--rpath,/usr/local/lib/'])

env.Program(target='component', source=['dram.cpp', 'main.cpp'])