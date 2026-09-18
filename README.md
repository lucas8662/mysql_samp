# Plugin MySQL para SA:MP / open.mp

Plugin R39-6 para Pawn, atualizado para usar o **MariaDB Connector/C 3.4.9**.
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
servidor open.mp usado neste projeto. `mysql.so` e `mysql_static.so` têm o
mesmo conteúdo: ambos incluem o conector MariaDB estaticamente.

### Dependências em Linux

Não é necessário instalar `libmysqlclient` ou `libmariadb` no servidor: o
conector já está no plugin. O host ainda precisa das bibliotecas de execução
de 32 bits do Ubuntu 20.04, inclusive OpenSSL 1.1, C/C++ e pthread.

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

- MariaDB Connector/C 3.4.9 incluído em `third_party/mariadb-connector-c`.
- Plugins de autenticação MySQL 8 compilados de forma estática.
- Removida a dependência antiga de `libmysqlclient_r`.
- Corrigida a cópia do cache de resultados para usar `mysql_fetch_lengths()`,
  sem depender do layout interno do conector.
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

As queries do arquivo são separadas por `;`. Na forma em thread, o callback é
chamado apenas após a última query; qualquer falha chama `OnQueryError`. Na
forma síncrona, `use_cache = true` salva somente o resultado da última query.

## Compilação local

### Linux x86

Instale compilador, CMake, bibliotecas Boost de 32 bits, OpenSSL de 32 bits e
zlib de 32 bits. Em Ubuntu 20.04, por exemplo:

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install cmake g++-multilib make \
  libboost-thread-dev:i386 libboost-chrono-dev:i386 \
  libboost-date-time-dev:i386 libboost-system-dev:i386 \
  libboost-atomic-dev:i386 libssl-dev:i386 zlib1g-dev:i386
make all
```

Os arquivos gerados ficam em `bin/mysql.so` e `bin/mysql_static.so`.

### Windows x86

O projeto usa CMake, Visual Studio e vcpkg. Instale as dependências com o
triplet `x86-windows-static-md`:

```powershell
vcpkg install boost-atomic boost-chrono boost-date-time boost-system boost-thread openssl --triplet x86-windows-static-md
cmake -S . -B build/windows -A Win32 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="<vcpkg>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x86-windows-static-md
cmake --build build/windows --config Release --parallel
```

O resultado é `build/windows/Release/mysql.dll`.

## GitHub Actions

O workflow em `.github/workflows/build.yml` roda em push para `master`, pull
request, tag iniciada por `v` ou execução manual. Ele publica artefatos
separados para:

- Linux x86: `mysql.so` e `mysql_static.so`, compilados em Ubuntu 20.04.
- Windows x86: `mysql.dll`.

## Validação feita

O binário Linux foi compilado e verificado em Ubuntu 20.04 x86. Ele não depende
de `libmysqlclient`, contém o plugin `caching_sha2_password` e não apresentou
bibliotecas ou símbolos ausentes. A validação contra o banco real depende das
credenciais e das permissões do ambiente em que o servidor for executado.


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
