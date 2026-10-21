#!/usr/bin/env python3
"""Provision disposable Docker database services for connection/auth/TLS tests.

Only pass IDs of isolated test services: this changes their TLS configuration,
restarts MySQL/MariaDB, and creates an anonymous account with no privileges.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

SERVER_DIRECTORY = "/tmp/orm-cxx-ci-tls"


def run(command: list[str], *, input_text: str | None = None) -> str:
    result = subprocess.run(command, input=input_text, text=True, capture_output=True)
    if result.returncode:
        # Commands can carry credentials or SQL; never echo their arguments/output.
        raise RuntimeError(f"{Path(command[0]).name} failed while provisioning isolated database services")
    return result.stdout.strip()


def docker(*arguments: str, input_text: str | None = None) -> str:
    return run(["docker", *arguments], input_text=input_text)


def validate_container(container: str) -> None:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", container):
        raise ValueError("Invalid isolated service container ID")
    state = json.loads(docker("inspect", container))[0]
    if not state["State"]["Running"]:
        raise RuntimeError("An isolated database service is not running")


def certificates(directory: Path) -> None:
    if directory.exists() and any(directory.iterdir()):
        raise ValueError("Certificate output must be a new or empty directory")
    directory.mkdir(parents=True, exist_ok=True)
    ca = directory / "ca.pem"
    key = directory / "ca-key.pem"
    # Avoid inheriting workstation OpenSSL defaults (including duplicate CA extensions).
    configuration = directory / "openssl.cnf"
    configuration.write_text("[req]\ndistinguished_name=dn\n[dn]\n", encoding="utf-8")
    run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-sha256",
         "-config", str(configuration), "-subj", "/CN=orm-cxx isolated CI CA", "-keyout", str(key), "-out", str(ca),
         "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign"])
    for name, usage, subject in [("server", "serverAuth", "localhost"), ("client", "clientAuth", "orm_cxx")]:
        extension = directory / f"{name}.ext"
        extension.write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
                             f"extendedKeyUsage={usage}\n" +
                             ("subjectAltName=DNS:localhost,IP:127.0.0.1,DNS:postgres,DNS:mysql,DNS:mariadb\n"
                              if name == "server" else ""), encoding="utf-8")
        run(["openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes", "-sha256", "-config", str(configuration), "-subj", f"/CN={subject}",
             "-keyout", str(directory / f"{name}-key.pem"), "-out", str(directory / f"{name}.csr")])
        run(["openssl", "x509", "-req", "-days", "2", "-sha256", "-in", str(directory / f"{name}.csr"),
             "-CA", str(ca), "-CAkey", str(key), "-set_serial", "1" if name == "server" else "2",
             "-extfile", str(extension), "-out", str(directory / f"{name}-cert.pem")])
    for name in ("ca-key.pem", "server-key.pem", "client-key.pem"):
        (directory / name).chmod(0o600)
    run(["openssl", "verify", "-CAfile", str(ca), str(directory / "server-cert.pem"),
         str(directory / "client-cert.pem")])


def copy_certificates(container: str, directory: Path, owner: str) -> None:
    docker("exec", "--user", "0", container, "mkdir", "-p", SERVER_DIRECTORY)
    for name in ("ca.pem", "server-cert.pem", "server-key.pem"):
        docker("cp", str(directory / name), f"{container}:{SERVER_DIRECTORY}/{name}")
    docker("exec", "--user", "0", container, "chown", "-R", f"{owner}:{owner}", SERVER_DIRECTORY)
    docker("exec", "--user", "0", container, "chmod", "600", f"{SERVER_DIRECTORY}/server-key.pem")


def pg_sql(container: str, query: str, *, user: str, database: str) -> str:
    return docker("exec", "--user", "postgres", "-i", container, "psql", "-X", "-v", "ON_ERROR_STOP=1",
                  "-A", "-t", "-U", user, "-d", database, input_text=query)


def provision_postgres(container: str, directory: Path, user: str, database: str, password: str) -> None:
    copy_certificates(container, directory, "postgres")
    settings = ["ALTER SYSTEM SET ssl = 'on';"]
    for option, filename in [("ssl_ca_file", "ca.pem"), ("ssl_cert_file", "server-cert.pem"),
                             ("ssl_key_file", "server-key.pem")]:
        settings.append(f"ALTER SYSTEM SET {option} = '{SERVER_DIRECTORY}/{filename}';")
    pg_sql(container, "\n".join(settings), user=user, database=database)
    hba = pg_sql(container, "SHOW hba_file;", user=user, database=database)
    # Keep local administration through the Unix socket; every TCP login must prove its password.
    docker("exec", "--user", "postgres", "-i", container, "sh", "-c", 'cat > "$1"', "sh", hba,
           input_text="local all all trust\nhost all all 0.0.0.0/0 scram-sha-256\nhost all all ::/0 scram-sha-256\n")
    pg_sql(container, "SELECT pg_reload_conf();", user=user, database=database)
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if pg_sql(container, "SHOW ssl;", user=user, database=database) == "on":
            encrypted = docker("exec", "-i", "-e", f"PGPASSWORD={password}", "-e", "PGSSLMODE=verify-full",
                               "-e", f"PGSSLROOTCERT={SERVER_DIRECTORY}/ca.pem", container, "psql", "-X",
                               "-v", "ON_ERROR_STOP=1", "-A", "-t", "-h", "localhost", "-U", user, "-d", database,
                               input_text="SELECT ssl FROM pg_stat_ssl WHERE pid=pg_backend_pid();")
            if encrypted != "t":
                raise RuntimeError("PostgreSQL verification did not negotiate an encrypted session")
            print("PostgreSQL: password authentication and TLS enabled", flush=True)
            return
        time.sleep(0.5)
    raise RuntimeError("PostgreSQL did not activate TLS")


def mysql_sql(container: str, family: str, password: str, query: str, *, tls: bool = False,
              anonymous: bool = False) -> str:
    client = "mysql" if family == "MYSQL" else "mariadb"
    command = ["exec", "-i", "-e", f"MYSQL_PWD={'' if anonymous else password}", container, client,
               "--batch", "--skip-column-names", f"--user={'orm_anonymous_probe' if anonymous else 'root'}"]
    if tls:
        command += ["--protocol=TCP", "--host=localhost", f"--ssl-ca={SERVER_DIRECTORY}/ca.pem"]
        command += ["--ssl-mode=VERIFY_IDENTITY"] if family == "MYSQL" else ["--ssl-verify-server-cert"]
    return docker(*command, input_text=query)


def provision_mysql(container: str, family: str, directory: Path, password: str) -> None:
    copy_certificates(container, directory, "mysql")
    configuration = ("\n[mysqld]\n" + "".join(f"{option}={SERVER_DIRECTORY}/{filename}\n" for option, filename in
                     [("ssl-ca", "ca.pem"), ("ssl-cert", "server-cert.pem"), ("ssl-key", "server-key.pem")]))
    config_file = "/etc/my.cnf" if family == "MYSQL" else "/etc/mysql/mariadb.conf.d/99-orm-cxx-ci.cnf"
    docker("exec", "--user", "0", "-i", container, "sh", "-c", 'cat >> "$1"', "sh", config_file,
           input_text=configuration)
    docker("restart", container)
    deadline = time.monotonic() + 120
    while True:
        try:
            mysql_sql(container, family, password, "SELECT 1;")
            break
        except RuntimeError:
            if time.monotonic() >= deadline:
                raise RuntimeError(f"{family} did not become ready after TLS configuration") from None
            time.sleep(1)
    mysql_sql(container, family, password,
              "CREATE USER IF NOT EXISTS ''@'%' IDENTIFIED BY '';\n"
              "ALTER USER ''@'%' IDENTIFIED BY '';\n"
              "DROP USER IF EXISTS 'orm_anonymous_probe'@'%';\n")
    cipher = mysql_sql(container, family, password, "SHOW SESSION STATUS LIKE 'Ssl_cipher';", tls=True)
    if len(cipher.split()) != 2:
        raise RuntimeError(f"{family} verification did not negotiate an encrypted session")
    identity = mysql_sql(container, family, password, "SELECT CURRENT_USER();", tls=True, anonymous=True)
    if not identity.startswith("@"):
        raise RuntimeError(f"{family} anonymous profile authenticated as a named account")
    print(f"{family}: verified server certificate, encrypted session and anonymous identity", flush=True)


def environment_values(directory: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for family in ("POSTGRESQL", "MYSQL", "MARIADB"):
        prefix = f"ORM_CXX_{family}_TEST_"
        for suffix, filename in [("SSL_CA", "ca.pem"), ("SSL_CERT", "client-cert.pem"), ("SSL_KEY", "client-key.pem")]:
            values[prefix + suffix] = str(directory / filename)
        values[prefix + "SSL_MODE"] = "verify-full" if family == "POSTGRESQL" else "verify-identity"
        if family == "POSTGRESQL":
            continue
        for profile in ("ANONYMOUS", "TLS"):
            profile_prefix = f"ORM_CXX_{family}_{profile}_TEST_"
            values[profile_prefix + "ENABLED"] = "1"
            values[profile_prefix + "HOST"] = os.environ[prefix + "HOST"]
            values[profile_prefix + "PORT"] = os.environ[prefix + "PORT"]
            values[profile_prefix + "USER"] = "orm_anonymous_probe" if profile == "ANONYMOUS" else os.environ[prefix + "USER"]
            values[profile_prefix + "PASSWORD"] = "" if profile == "ANONYMOUS" else os.environ[prefix + "PASSWORD"]
            values[profile_prefix + "SSL_MODE"] = "required" if profile == "ANONYMOUS" else "verify-identity"
            for suffix, filename in [("SSL_CA", "ca.pem"), ("SSL_CERT", "client-cert.pem"), ("SSL_KEY", "client-key.pem")]:
                values[profile_prefix + suffix] = str(directory / filename)
    return values


def socket_profiles(directory: Path, values: dict[str, str]) -> None:
    """Exercise the client Unix-socket option against the isolated TCP servers."""
    if os.name != "posix":
        return
    executable = shutil.which("socat")
    if executable is None:
        raise RuntimeError("socat is required for the Unix-socket connection profiles")
    processes: list[subprocess.Popen] = []
    try:
        for family in ("MYSQL", "MARIADB"):
            prefix = f"ORM_CXX_{family}_TEST_"
            path = directory / f"{family.lower()}.sock"
            if len(os.fsencode(path)) > 100:
                raise ValueError("Unix-socket test output path is too long")
            process = subprocess.Popen(
                [executable, f"UNIX-LISTEN:{path},fork,mode=0600",
                 f"TCP:{os.environ[prefix + 'HOST']}:{os.environ[prefix + 'PORT']}"],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            processes.append(process)
            deadline = time.monotonic() + 10
            while not path.is_socket():
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Unix-socket test proxy failed to start")
                time.sleep(0.05)
            values[f"ORM_CXX_{family}_SOCKET_TEST_SOCKET"] = str(path)
        (directory / "socket-proxy-pids.json").write_text(
            json.dumps([process.pid for process in processes]) + "\n", encoding="utf-8")
    except BaseException:
        for process in processes:
            process.terminate()
            process.wait(timeout=10)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--postgres-container", required=True)
    parser.add_argument("--mysql-container", required=True)
    parser.add_argument("--mariadb-container", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--github-env", type=Path, default=os.environ.get("GITHUB_ENV"))
    args = parser.parse_args()
    try:
        docker("info", "--format", "{{.ServerVersion}}")
        for container in (args.postgres_container, args.mysql_container, args.mariadb_container):
            validate_container(container)
        directory = args.output.resolve()
        if any(character in str(directory) for character in "\r\n"):
            raise ValueError("Invalid certificate output path")
        certificates(directory)
        provision_postgres(args.postgres_container, directory, os.environ["ORM_CXX_POSTGRESQL_TEST_USER"],
                           os.environ["ORM_CXX_POSTGRESQL_TEST_DATABASE"], os.environ["ORM_CXX_POSTGRESQL_TEST_PASSWORD"])
        provision_mysql(args.mysql_container, "MYSQL", directory, os.environ["ORM_CXX_MYSQL_TEST_PASSWORD"])
        provision_mysql(args.mariadb_container, "MARIADB", directory, os.environ["ORM_CXX_MARIADB_TEST_PASSWORD"])
        values = environment_values(directory)
        socket_profiles(directory, values)
        if args.github_env:
            with args.github_env.open("a", encoding="utf-8") as output:
                for key, value in values.items():
                    if any(character in value for character in "\r\n"):
                        raise ValueError("Invalid multiline connection test environment value")
                    output.write(f"{key}={value}\n")
        else:
            (directory / "environment.json").write_text(json.dumps(values, indent=2) + "\n", encoding="utf-8")
        print("Required connection profiles provisioned; no authentication/TLS profile is optional in CI", flush=True)
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"Connection test provisioning failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
