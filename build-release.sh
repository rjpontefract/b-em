export ac_cv_func_malloc_0_nonnull=yes
export ac_cv_func_realloc_0_nonnull=yes

./autogen.sh
CFLAGS=-I/opt/homebrew/include LIBS=-L/opt/homebrew/lib ./configure

make clean && make -j8 && install_name_tool -add_rpath /opt/homebrew/lib ./b-em

