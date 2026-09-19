# Prepared statements e transactions: filterscript de teste

1. Copie `a_mysql.inc` atualizada para a pasta `pawno/include`.
2. Copie `prepared_transactions_test.pwn` para `filterscripts/` e compile-o com Pawn.
3. Copie o binário atualizado do plugin para `plugins/`.
4. Copie `mysql.ini.example` para a raiz do servidor como `mysql.ini` e informe suas credenciais.
5. Adicione `filterscripts prepared_transactions_test` no `server.cfg`.

Comandos no servidor:

- `/mysqlps`: executa um `INSERT ... ON DUPLICATE KEY UPDATE` usando prepared statement.
- `/mysqlpsselect`: executa um `SELECT` preparado e mostra o valor obtido pelo cache R39.
- `/mysqltx`: cria duas contas de teste e transfere 10 entre `tx_source` e `tx_destination` em uma transação.

A tabela `mysql_feature_test` é criada automaticamente. Ela usa InnoDB, necessário para `COMMIT` e `ROLLBACK` funcionarem.
