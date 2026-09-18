MySQL Plugin for San Andreas Multiplayer (SA:MP) [![Build Status](https://travis-ci.org/pBlueG/SA-MP-MySQL.svg?branch=master)](https://travis-ci.org/pBlueG/SA-MP-MySQL)
------------------------------------------------
*The best and most famous MySQL plugin for SA:MP out there!*

**This plugin allows you to use MySQL in PAWN. It's currently being developed by maddinat0r.**

How to install
--------------
Move *mysql.dll* (Windows) or *mysql.so* (Linux) to your `plugins/` directory. The Linux build in this repository includes MariaDB Connector/C 3.4.9 and supports MySQL 8 password authentication, including `caching_sha2_password`.
You'll have to edit the server configuration (*server.cfg*) as follows:
#### Windows
<pre>plugins mysql</pre>

#### Linux
<pre>plugins mysql.so</pre>

F.A.Q.
------
Q: *Do I need to install `libmysqlclient` on Linux?*  
A: No. The updated Linux plugin embeds MariaDB Connector/C. It still requires the 32-bit OpenSSL 1.1 runtime because the server and plugin target Linux x86.

Q: *The plugin just fails to load on Windows, how can I fix this?*  
A: You have to install the Microsoft C++ redistributables ([2010 (x86)](http://www.microsoft.com/en-us/download/details.aspx?id=5555), [2010 SP1 (x86)](http://www.microsoft.com/en-us/download/details.aspx?id=8328) and [2012 (x86)](http://www.microsoft.com/en-us/download/details.aspx?id=30679)).

Q: *I get a ton of debug messages regarding connections even though I'm calling* `mysql_connect` *only once, why is that so?*  
A: That's because the plugin uses multiple direct database connections per connection handle. The number of direct connections (and thus the number of those log messages) is 2+pool_size.  

Build instruction
---------------
GitHub Actions builds downloadable Linux x86 and Windows x86 artifacts on each push to `master`, pull request, tag starting with `v`, or manual workflow run.

#### Windows
1. Install Microsoft Visual Studio C++ (2012 or newer, the Express version also works) and the [MySQL C Connector (32-bit)](http://dev.mysql.com/downloads/connector/c/)
2. Install the [boost libraries (version 1.55 or higher)](http://www.boost.org/users/download/)
3. Open the solution file with Visual Studio -> right click on the project -> Properties -> VC++ Directories, use *Release* as configuration and adjust the paths to the previously installed libraries
4. Build the solution with *Release* as configuration

#### Linux
1. Install 32-bit build dependencies: `g++-multilib cmake libssl-dev:i386 zlib1g-dev:i386` and the 32-bit Boost thread, chrono, date-time, system and atomic development packages.
2. The MariaDB Connector/C 3.4.9 source is included in `third_party/mariadb-connector-c`.
3. Navigate to the project root directory and execute `make`. This builds the connector with the MySQL 8 password plugins statically included, then produces `bin/mysql.so` and `bin/mysql_static.so`.

Thanks to
---------
- AndreT (testing/several tutorials)
- DamianC (testing reports)
- JernejL (testing/suggestions)
- krisk (testing/suggestions)
- Kye (coding support)
- maddinat0r (developing the plugin as of R8)
- Mow (compiling/testing/hosting)
- nemesis (testing)
- Sergei (testing/suggestions/wiki documentation)
- xxmitsu (testing/compiling)
# mysql_samp
