#include <ma_global.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <mysql.h>
#include <mysql/client_plugin.h>

/* Exercise the connector actually embedded in the plugin, without a database. */
int main(int argc, char **argv)
{
    void *plugin;
    MYSQL *connection;
    int (*initialize)(int, char **, char **);
    void (*finish)(void);
    const char *(*version)(void);
    MYSQL *(*create)(MYSQL *);
    void (*close_connection)(MYSQL *);
    struct st_mysql_client_plugin *(*find_auth)(MYSQL *, const char *, int);
    if (argc != 2) return 2;
    plugin = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!plugin) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    initialize = dlsym(plugin, "mysql_server_init");
    finish = dlsym(plugin, "mysql_server_end");
    version = dlsym(plugin, "mysql_get_client_info");
    create = dlsym(plugin, "mysql_init");
    close_connection = dlsym(plugin, "mysql_close");
    find_auth = dlsym(plugin, "mysql_client_find_plugin");
    assert(initialize && finish && version && create && close_connection && find_auth);
    assert(strcmp(version(), "3.4.11") == 0);
    assert(initialize(0, NULL, NULL) == 0);
    connection = create(NULL);
    assert(connection);
    assert(find_auth(connection, "caching_sha2_password", MYSQL_CLIENT_AUTHENTICATION_PLUGIN));
    assert(find_auth(connection, "sha256_password", MYSQL_CLIENT_AUTHENTICATION_PLUGIN));
    close_connection(connection);
    finish();
    assert(dlclose(plugin) == 0);
    puts("Connector/C 3.4.11: load, initialization and MySQL 8 auth plugins OK");
    return 0;
}
