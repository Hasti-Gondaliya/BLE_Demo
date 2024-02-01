# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

set(srcs "main.c"
         "led.c"
         "gatt_svr.c"
         "temp.c")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS ".")
