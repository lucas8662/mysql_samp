#include <a_samp>
#include <a_mysql>

#define COLOR_GREEN 0x33CC33FF
#define COLOR_RED   0xCC3333FF
#define COLOR_YELLOW 0xFFCC33FF

new g_SQL = -1;
new MySQLStatement:g_InsertPlayer;
new MySQLStatement:g_SelectPlayer;

stock SendUsage(playerid)
{
    SendClientMessage(playerid, COLOR_YELLOW, "Testes MySQL: /mysqlps, /mysqlpsselect e /mysqltx");
}

public OnFilterScriptInit()
{
    print("\n-- Prepared statements / transactions test FS --");
    mysql_log(LOG_ERROR | LOG_WARNING, LOG_TYPE_TEXT);

    // Copie mysql.ini.example para mysql.ini na raiz do servidor e preencha-o.
    g_SQL = mysql_connect_file("mysql.ini");
    if (g_SQL == 0)
    {
        print("[MySQL Test] Não foi possível criar a conexão. Verifique mysql.ini e mysql_log.txt.");
        return 0;
    }

    mysql_query(g_SQL,
        "CREATE TABLE IF NOT EXISTS mysql_feature_test (id INT NOT NULL AUTO_INCREMENT, name VARCHAR(24) NOT NULL, balance INT NOT NULL DEFAULT 1000, PRIMARY KEY (id), UNIQUE KEY unique_name (name)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
        false);

    g_InsertPlayer = mysql_stmt_prepare(g_SQL,
        "INSERT INTO mysql_feature_test (name, balance) VALUES (?, ?) ON DUPLICATE KEY UPDATE balance = VALUES(balance)");
    g_SelectPlayer = mysql_stmt_prepare(g_SQL,
        "SELECT id, name, balance FROM mysql_feature_test WHERE name = ? LIMIT 1");

    print("[MySQL Test] Use /mysqlps, /mysqlpsselect ou /mysqltx.");
    return 1;
}

public OnFilterScriptExit()
{
    if (g_InsertPlayer)
        mysql_stmt_close(g_InsertPlayer);
    if (g_SelectPlayer)
        mysql_stmt_close(g_SelectPlayer);
    if (g_SQL)
        mysql_close(g_SQL);
    return 1;
}

public OnPlayerConnect(playerid)
{
    SendUsage(playerid);
    return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    if (!strcmp(cmdtext, "/mysqlps", true))
    {
        new name[MAX_PLAYER_NAME];
        GetPlayerName(playerid, name, sizeof name);

        mysql_stmt_bind_string(g_InsertPlayer, 0, name);
        mysql_stmt_bind_int(g_InsertPlayer, 1, 1000);
        mysql_stmt_execute(g_InsertPlayer, "OnPreparedPlayerSaved", "d", playerid);
        SendClientMessage(playerid, COLOR_YELLOW, "Prepared INSERT/UPDATE enviado.");
        return 1;
    }

    if (!strcmp(cmdtext, "/mysqlpsselect", true))
    {
        new name[MAX_PLAYER_NAME];
        GetPlayerName(playerid, name, sizeof name);

        mysql_stmt_bind_string(g_SelectPlayer, 0, name);
        mysql_stmt_execute(g_SelectPlayer, "OnPreparedPlayerLoaded", "d", playerid);
        return 1;
    }

    if (!strcmp(cmdtext, "/mysqltx", true))
    {
        new MySQLTransaction:transaction = mysql_transaction_begin(g_SQL);

        // Não há dados do jogador nestas duas queries. Para dados externos,
        // formate e escape os valores antes de adicioná-los à transação.
        mysql_transaction_query(transaction,
            "INSERT INTO mysql_feature_test (name, balance) VALUES ('tx_source', 1000) ON DUPLICATE KEY UPDATE name = name");
        mysql_transaction_query(transaction,
            "INSERT INTO mysql_feature_test (name, balance) VALUES ('tx_destination', 1000) ON DUPLICATE KEY UPDATE name = name");
        mysql_transaction_query(transaction,
            "UPDATE mysql_feature_test SET balance = balance - 10 WHERE name = 'tx_source' AND balance >= 10");
        mysql_transaction_query(transaction,
            "UPDATE mysql_feature_test SET balance = balance + 10 WHERE name = 'tx_destination'");
        mysql_transaction_commit(transaction, "OnTransactionComplete", "d", playerid);

        SendClientMessage(playerid, COLOR_YELLOW, "Transação de teste enviada: tx_source -> tx_destination.");
        return 1;
    }

    return 0;
}

forward OnPreparedPlayerSaved(playerid);
public OnPreparedPlayerSaved(playerid)
{
    new message[96];
    format(message, sizeof message, "Prepared statement concluído. Linhas afetadas: %d.", cache_affected_rows());
    SendClientMessage(playerid, COLOR_GREEN, message);
    return 1;
}

forward OnPreparedPlayerLoaded(playerid);
public OnPreparedPlayerLoaded(playerid)
{
    if (!cache_num_rows())
    {
        SendClientMessage(playerid, COLOR_RED, "Nenhum registro. Use /mysqlps primeiro.");
        return 1;
    }

    new name[MAX_PLAYER_NAME], message[128];
    new id = cache_get_row_int(0, 0);
    new balance = cache_get_row_int(0, 2);
    cache_get_row(0, 1, name, g_SQL, sizeof name);
    format(message, sizeof message, "SELECT preparado: id=%d, nome=%s, saldo=%d.", id, name, balance);
    SendClientMessage(playerid, COLOR_GREEN, message);
    return 1;
}

forward OnTransactionComplete(playerid);
public OnTransactionComplete(playerid)
{
    new message[96];
    format(message, sizeof message, "Transação confirmada. Linhas afetadas: %d.", cache_affected_rows());
    SendClientMessage(playerid, COLOR_GREEN, message);
    return 1;
}

public OnQueryError(errorid, error[], callback[], query[], connectionHandle)
{
    new message[144];
    format(message, sizeof message, "MySQL erro %d: %s", errorid, error);
    print(message);
    printf("[MySQL Test] Callback: %s | Query: %s | Handle: %d", callback, query, connectionHandle);
    return 1;
}
