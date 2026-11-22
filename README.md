# Plugin MySQL para SA:MP / open.mp

Plugin MySQL **v1.0.0** para Pawn, compatível com a API R39-6, atualizado para usar o **MariaDB Connector/C 3.4.11**.
Ele mantém as mesmas natives e a mesma include `a_mysql.inc`, portanto os
gamemodes existentes não precisam ser alterados.

O MariaDB Connector/C é compatível com servidores MySQL e MariaDB. A mudança
resolve a limitação do conector antigo ao conectar em MySQL 8 com senha e
suporta os métodos de autenticação `caching_sha2_password` e
`sha256_password`.

## Instalação

Copie o binário correspondente para a pasta `plugins` do servidor e carregue-o
na configuração do servidor:

```text
plugins mysql.so
```

No Windows, use `mysql.dll` e carregue o plugin como `mysql`.

Os binários Linux são x86 (32 bits), pois devem ter a mesma arquitetura do
servidor open.mp usado neste projeto. Ambos incluem o conector MariaDB
estaticamente; `mysql.so` usa OpenSSL do sistema, enquanto `mysql_static.so`
também inclui OpenSSL estaticamente.

### Dependências em Linux

Não é necessário instalar `libmysqlclient` ou `libmariadb` no servidor: o
conector já está no plugin. O host ainda precisa das bibliotecas de execução
de 32 bits do Ubuntu 20.04, incluindo C/C++ e pthread. Para `mysql.so`, também
é necessário OpenSSL 1.1; `mysql_static.so` não depende do OpenSSL do sistema.

## Compatibilidade com banco de dados

| Servidor | Suporte |
| --- | --- |
| MySQL 5.7 | Sim |
| MySQL 8.x | Sim, inclusive `caching_sha2_password` e `sha256_password` |
| MariaDB | Sim |

As chamadas Pawn continuam iguais. Por exemplo:

```pawn
new MySQL:g_SQL = mysql_connect("127.0.0.1", "usuario", "banco", "senha");
```

Use uma conta com permissões para o banco informado e confirme o erro completo
em `mysql_log.txt` caso a conexão falhe. O plugin registra o código e a mensagem
retornados pelo servidor MySQL/MariaDB.

## Alterações desta atualização

- MariaDB Connector/C 3.4.11 incluído em `third_party/mariadb-connector-c`.
- Fonte oficial fixada na tag `v3.4.11`, commit
  `be67a4fc1e0493913732df90e562f122bff9dfe3` do
  [MariaDB Connector/C](https://github.com/mariadb-corporation/mariadb-connector-c/tree/v3.4.11).
  Inclui correções de TLS, validação de resultados recebidos e prepared
  statements; a API Pawn e as opções de autenticação do plugin permanecem iguais.
- Plugins de autenticação MySQL 8 compilados de forma estática.
- Removida a dependência antiga de `libmysqlclient_r`.
- Corrigida a cópia do cache de resultados para usar `mysql_fetch_lengths()`,
  sem depender do layout interno do conector.
- Corrigidas as tentativas de conexão após falha no handshake: o objeto do
  conector é recriado para restaurar suas opções padrão, evitando o erro 5027
  (`maximum allowed number of columns (=0)`) nas consultas após reconectar.
- Adicionadas novas natives compatíveis com R39-6: conexão via arquivo,
  conexão TLS, mensagem de erro e execução de arquivos SQL.
- Adicionado CMake para a compilação Windows e workflow do GitHub Actions.

## Novas natives, sem quebrar R39-6

As natives R39-6 existentes permanecem com a mesma assinatura. As opções abaixo
são adicionais e podem ser adotadas gradualmente.

### Mensagem de erro

```pawn
new error[128];
mysql_error(error, g_SQL, sizeof error);
printf("MySQL: %s", error);
```

`mysql_error` retorna a mensagem do último comando executado sem thread no
handle informado. Para falhas em consultas com thread, continue usando
`OnQueryError` e `mysql_log.txt`.

### Conexão por arquivo e TLS

Copie [mysql.ini.example](mysql.ini.example) como `mysql.ini` para a raiz do
servidor, preencha as credenciais e conecte sem deixá-las no gamemode:

```pawn
new g_SQL = mysql_connect_file("mysql.ini");
```

A porta deve estar entre 1 e 65535; `pool_size` aceita de 0 a 64. Valores
negativos, fora da faixa ou inválidos são rejeitados nas conexões diretas e
por arquivo.

O arquivo aceita `host`, `user`, `password`, `database`, `port`,
`auto_reconnect`, `pool_size`, `ssl_enable`, `ssl_key_file`, `ssl_cert_file`,
`ssl_ca_file`, `ssl_ca_path` e `ssl_cipher`. Quando `ssl_enable = true`, as
conexões principal, em thread e do pool usam TLS.

Também há a versão direta para TLS, mantendo a ordem de argumentos R39-6:

```pawn
new g_SQL = mysql_connect_ssl(
    "db.exemplo.com", "usuario", "banco", "senha",
    "client-key.pem", "client-cert.pem", "ca.pem"
);
```

### Arquivos SQL

Guarde o arquivo em `scriptfiles`, por exemplo `scriptfiles/schema.sql`, e use:

```pawn
mysql_tquery_file(g_SQL, "schema.sql", "OnSchemaReady");
// ou, de forma síncrona:
mysql_query_file(g_SQL, "schema.sql");
```

As queries do arquivo são separadas por `;`, respeitando strings entre aspas,
identificadores entre crases, escapes e comentários SQL. Arquivos com aspas ou
comentários de bloco sem fechamento são rejeitados antes da execução. O parser
usa as regras padrão de escape do MySQL; diretivas de cliente como `DELIMITER`
não são suportadas. Na forma em thread, o callback é
chamado apenas após a última query; qualquer falha chama `OnQueryError`. Na
forma síncrona, `use_cache = true` salva somente o resultado da última query.

## Compilação local

O projeto usa somente CMake e C++17, sem Boost. O mesmo `CMakeLists.txt`
compila Linux e Windows.

### Linux x86

Instale compilador com suporte a 32 bits, OpenSSL de 32 bits e CMake 3.20 ou
mais novo. Em Ubuntu 20.04 (o `cmake` do apt é antigo; use o do `pip`):

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install g++-multilib make libssl-dev:i386 zlib1g-dev:i386 python3-pip
pip3 install "cmake==3.27.9"

cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel
ctest --test-dir build/linux --output-on-failure
```

O toolchain `cmake/linux-x86.cmake` é selecionado automaticamente e gera
binários de 32 bits. Os arquivos gerados ficam em `bin/mysql.so` (OpenSSL do
sistema) e `bin/mysql_static.so` (OpenSSL embutido).

O `ctest` verifica o parser SQL, o carregamento dos dois plugins, a versão do
conector, os métodos de autenticação MySQL 8 e as regressões do ORM, de
callbacks e de transações, sem precisar de banco.

### Teste com banco real

`tests/mysql_integration.cpp` carrega o plugin como o servidor SA:MP/open.mp
faz e executa as natives contra um MySQL/MariaDB de verdade: conexão, 
`mysql_format`, `tquery`/`pquery`, cache, `OnQueryError`, prepared statements,
transações, reconexão e uma rajada de 35000 queries. Sem `MYSQL_TEST_HOST`, o
teste é marcado como pulado.

```bash
docker run -d --name mysql-test -p 3306:3306 \
  -e MYSQL_ROOT_PASSWORD=senha -e MYSQL_DATABASE=samp_test mysql:8.4

MYSQL_TEST_HOST=127.0.0.1 MYSQL_TEST_PORT=3306 MYSQL_TEST_USER=root \
MYSQL_TEST_PASSWORD=senha MYSQL_TEST_DATABASE=samp_test \
  ctest --test-dir build/linux -L integration --output-on-failure
```

O teste cria e apaga a tabela `it_test`; use um banco descartável.

### Sanitizers

`tests/run-sanitizers.sh` compila o plugin e os testes instrumentados e roda o
`ctest` (com as mesmas variáveis `MYSQL_TEST_*` para incluir o banco real):

```bash
tests/run-sanitizers.sh address   # ASan + UBSan + LeakSanitizer, 32 bits (GCC)
tests/run-sanitizers.sh thread    # ThreadSanitizer, build de teste 64 bits (Clang)
```

O ThreadSanitizer não existe em 32 bits, por isso o modo `thread` usa
`-DPLUGIN_NATIVE_ARCH=ON`; esse binário serve só para teste. Em containers, o
modo `address` precisa de `--cap-add SYS_PTRACE` para o detector de vazamentos.
Em kernels recentes, se o TSan falhar ao iniciar, rode
`sudo sysctl vm.mmap_rnd_bits=28`.

### Segurança e comportamento do ORM

- As natives `orm_*` mantêm as mesmas assinaturas.
- As consultas usam armazenamento dinâmico, sem os buffers fixos antigos de
  `UPDATE`/`DELETE`, e escapam os nomes de tabelas e colunas.
- Chaves aceitam somente inteiro ou string. Chaves string são incluídas no
  `INSERT` e não são sobrescritas pelo ID auto-incrementado de outra coluna.
- IDs gerados que excedem o limite de uma célula Pawn não são truncados;
  nesse caso, `orm_errno` retorna `ORM_ERROR_NO_DATA`.
- Destruir um ORM, descarregar seu script, fechar a conexão ou alterar os
  campos/chave invalida a aplicação dos resultados e callbacks ORM pendentes.
  Isso não desfaz consultas SQL já enviadas ao banco.
- As variáveis associadas precisam continuar válidas até o término da operação
  assíncrona; não associe variáveis locais temporárias a um ORM persistente.

O teste `tests/orm_regression.cpp` usa o plugin compilado, com funções de strings
Pawn simuladas, sem um servidor de banco. Ele não substitui a homologação no
SA:MP/open.mp real.

### Windows x86

Use Visual Studio e vcpkg com o triplet `x86-windows-static-md`:

```powershell
vcpkg install openssl --triplet x86-windows-static-md
cmake -S . -B build/windows -A Win32 -DCMAKE_TOOLCHAIN_FILE="<vcpkg>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x86-windows-static-md
cmake --build build/windows --config Release --target mysql_plugin --parallel
```

O resultado é `build/windows/Release/mysql.dll`. O Visual Studio também abre a
pasta do projeto diretamente pelo `CMakeLists.txt`.

## GitHub Actions

O workflow em `.github/workflows/build.yml` roda em push para `main`, pull
request, tag iniciada por `v` ou execução manual. Ele:

- compila `mysql.so` e `mysql_static.so` em Ubuntu 20.04 e roda o `ctest`;
- roda o teste com banco real contra MySQL 8.4, MySQL 5.7 e MariaDB 11.4;
- roda toda a suíte com AddressSanitizer/UBSan e com ThreadSanitizer;
- compila `mysql.dll` para Windows x86;
- publica os binários como artefatos.


## Prepared statements

A API de prepared statements foi adicionada sem modificar as natives R39 existentes. Use `?` no SQL e associe os valores antes de executar; os valores são enviados ao MariaDB/MySQL separadamente do texto SQL.

```pawn
new MySQLStatement:stmt = mysql_stmt_prepare(1,
    "INSERT INTO accounts (name, score) VALUES (?, ?)");
mysql_stmt_bind_string(stmt, 0, playerName);
mysql_stmt_bind_int(stmt, 1, score);
mysql_stmt_execute(stmt, "OnAccountSaved", "d", playerid);
mysql_stmt_close(stmt); // a execução que já entrou na fila continua normalmente

forward OnAccountSaved(playerid);
public OnAccountSaved(playerid)
{
    printf("Conta %d salva; linhas: %d", playerid, cache_affected_rows());
}
```

Os índices dos parâmetros começam em `0`. A API atende `INSERT`, `UPDATE`, `DELETE` e `SELECT`. Em callbacks, os resultados de `SELECT` ficam disponíveis pelas natives de cache R39; consultas de alteração expõem `cache_affected_rows` e `cache_insert_id`.


## Transactions

Use transactions when two or more writes must either all be applied or all be reverted. The plugin executes the queued queries consecutively on one connection, beginning with `START TRANSACTION` and finalizing with `COMMIT`. If one query fails, it runs `ROLLBACK` and calls `OnQueryError`.

```pawn
new MySQLTransaction:transaction = mysql_transaction_begin(1);
mysql_transaction_query(transaction, "UPDATE accounts SET money = money - 100 WHERE id = 1");
mysql_transaction_query(transaction, "UPDATE accounts SET money = money + 100 WHERE id = 2");
mysql_transaction_commit(transaction, "OnTransferComplete", "d", playerid);

forward OnTransferComplete(playerid);
public OnTransferComplete(playerid)
{
    printf("Transferência concluída: %d linhas afetadas", cache_affected_rows());
}
```

Call `mysql_transaction_rollback` before `mysql_transaction_commit` to discard a transaction that has not been sent to the database.

### `mysql_tquery` x `mysql_pquery`

- `mysql_tquery` usa uma única conexão em thread: as queries rodam e os
  callbacks chegam exatamente na ordem em que foram enviadas.
- `mysql_pquery` usa as `pool_size` conexões do pool, que compartilham uma
  única fila: cada conexão livre pega a próxima query. Uma query lenta ocupa
  apenas uma conexão e não atrasa as demais. Não há garantia de ordem entre
  as `pquery`, nem de que duas delas usem a mesma sessão, então não dependa de
  `SET @variavel`, tabelas temporárias ou transações manuais entre chamadas.
- Se uma conexão do pool cair, as outras continuam atendendo a fila enquanto
  ela reconecta.
- Com `pool_size = 0`, `mysql_pquery` não falha mais: a query roda na conexão
  do `mysql_tquery`, na mesma ordem, e um aviso é registrado no log uma vez
  por conexão.

### Rastreio de queries no log

Com `mysql_log(LOG_ALL)`, cada query com thread recebe um identificador
(`q#34`) que aparece em todas as linhas sobre ela. Isso permite seguir a query
desde a fila até o callback e ver qual conexão a executou:

```
CMySQLHandle::QueueQuery - q#34 queued on 1/pool (2 unprocessed), callback "OnTrace": "SELECT 'trace pquery'"
CMySQLConnection::ProcessQueries[1/pool#2] - q#34 started after 0.014 ms in queue
CMySQLQuery::Execute[OnTrace, q#34] - query was successfully executed within 0.138 milliseconds
CMySQLConnection::ProcessQueries[1/pool#2] - q#34 finished in 0.213 ms (0.227 ms since queued)
CCallback::ProcessCallbacks - q#34 calling "OnTrace", 0.857 ms after the query finished
CCallback::ProcessCallbacks - q#34 callback took 0.013 ms (1.097 ms since queued)
```

- O tempo na fila (`started after`) alto indica que as conexões estão
  saturadas: aumente `pool_size` ou reduza queries em `mysql_tquery`.
- O intervalo entre `finished` e `calling` é a espera pelo próximo tick do
  servidor. `callback took` mede o código Pawn do callback.
- As conexões aparecem como `<handle>/tquery` e `<handle>/pool#N`, também nas
  mensagens de conexão, reconexão e charset.
- Um callback que não existe no script gera um `WARNING`, mesmo sem o modo
  debug. A exceção é o `OnQueryError`, que é opcional.

O rastreio só é montado quando `LOG_DEBUG` está ativo. Ele grava várias linhas
por query, então ative-o apenas para investigar.

### Reconexão e latência

- As threads de consulta acordam assim que uma query entra na fila, em vez de
  verificar a fila a cada 10 ms. Isso reduz a latência de cada `mysql_tquery`/
  `mysql_pquery` e o uso de CPU quando o servidor está ocioso.
- Com `auto_reconnect = true`, as conexões em thread (tquery, pquery, pool)
  tentam reconectar sozinhas quando o banco está fora do ar na inicialização ou
  quando uma reconexão falha. O intervalo começa em 1 s e dobra até 30 s; as
  queries ficam na fila e são executadas assim que a conexão volta.
- Erros `2006` (server gone away) e `2013` (lost connection) disparam a
  reconexão. A query que falhou continua chamando `OnQueryError`.
- `mysql_reconnect` agora fecha e reabre cada conexão em uma única etapa. Antes,
  com queries pendentes, a conexão podia ficar fechada definitivamente.
- Cada tentativa de conexão tem tempo limite de 10 s.
- A conexão principal (usada por `mysql_query` e `mysql_escape_string`) não
  reconecta em segundo plano, para não travar o servidor; use `mysql_reconnect`
  se o banco estava indisponível ao iniciar.

### Segurança dos callbacks

Callbacks de consultas e `OnQueryError` são executados somente no script que
originou a operação. Descarregar esse script cancela seus callbacks pendentes,
inclusive se outro script for carregado no mesmo endereço; o SQL já enviado
ao banco não é desfeito. É permitido fechar a conexão dentro do callback.
