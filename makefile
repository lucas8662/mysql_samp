GPP ?= g++ -m32
GCC ?= gcc -m32
CONNECTOR_DIR ?= third_party/mariadb-connector-c
CONNECTOR_BUILD_DIR ?= $(CONNECTOR_DIR)/build-linux-x86


COMPILE_FLAGS = -c -O3 -w -fPIC -DLINUX -Wall -I libs/ -I libs/sdk/amx/ -I $(CONNECTOR_DIR)/include -I $(CONNECTOR_BUILD_DIR)/include
DYNAMIC_LIBRARIES = -Wl,-Bstatic -lboost_thread -lboost_chrono -lboost_date_time -lboost_system -lboost_atomic -lmariadbclient -Wl,-Bdynamic -lssl -lcrypto -lz -ldl -pthread -lrt
STATIC_LIBRARIES = -Wl,-Bstatic -lboost_thread -lboost_chrono -lboost_date_time -lboost_system -lboost_atomic -lmariadbclient -lssl -lcrypto -Wl,-Bdynamic -lz -ldl -pthread -lrt


all: connector compile dynamic_link static_link clean
dynamic: connector compile dynamic_link clean
static: connector compile static_link clean

connector:
	@CC="gcc" CXX="g++" cmake -S $(CONNECTOR_DIR) -B $(CONNECTOR_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-m32" -DCMAKE_CXX_FLAGS="-m32" -DWITH_SSL=OPENSSL -DOPENSSL_USE_STATIC_LIBS=TRUE -DCLIENT_PLUGIN_CACHING_SHA2_PASSWORD=STATIC -DCLIENT_PLUGIN_SHA256_PASSWORD=STATIC
	@cmake --build $(CONNECTOR_BUILD_DIR) --target mariadbclient --parallel

compile:
	@mkdir -p bin
	@echo Compiling plugin..
	@ $(GPP) $(COMPILE_FLAGS) -std=c++0x src/*.cpp
	@echo Compiling plugin SDK..
	@ $(GPP) $(COMPILE_FLAGS) libs/sdk/*.cpp
	@ $(GCC) $(COMPILE_FLAGS) libs/sdk/amx/*.c

link:
	@echo Linking plugin..
	@ $(GPP) -O2 -fshort-wchar -shared -Wl,-z,defs -o "bin/mysql.so" *.o -L $(CONNECTOR_BUILD_DIR)/libmariadb $(DYNAMIC_LIBRARIES)

dynamic_link: link

static_link: link
	@ $(GPP) -O2 -fshort-wchar -shared -Wl,-z,defs -o "bin/mysql_static.so" *.o -L $(CONNECTOR_BUILD_DIR)/libmariadb $(STATIC_LIBRARIES)

clean:
	@ rm -f *.o
	@echo Done.
