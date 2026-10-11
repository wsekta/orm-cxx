#!/bin/sh
# Debian's default MySQL-compatible development package is MariaDB Connector/C.
# Install the Oracle C API required by SOCI from Oracle's signed 8.4 LTS repository.
set -eu
. /etc/os-release
test "$ID" = debian
apt-get update
apt-get install --no-install-recommends --yes ca-certificates curl gnupg
curl --fail --silent --show-error https://repo.mysql.com/RPM-GPG-KEY-mysql-2025 \
    | gpg --batch --yes --dearmor --output /usr/share/keyrings/orm-cxx-mysql.gpg
printf 'deb [signed-by=/usr/share/keyrings/orm-cxx-mysql.gpg] https://repo.mysql.com/apt/debian/ %s mysql-8.4-lts\n' \
    "$VERSION_CODENAME" > /etc/apt/sources.list.d/orm-cxx-mysql.list
apt-get update
apt-get install --no-install-recommends --yes libmysqlclient-dev mysql-community-client
