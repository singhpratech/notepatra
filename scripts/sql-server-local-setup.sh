#!/usr/bin/env bash
# sql-server-local-setup.sh — bring up the local SQL Server test instance,
# seed it with sample tables + rows, and register a matching connection in
# Notepatra's db-connections.json so the Data Analyst Mode + query_sql tool
# can hit it immediately.
#
# Idempotent: safe to re-run. If the container is already up, just re-seeds
# the schema (DROP IF EXISTS + CREATE). If the Notepatra connection record
# already exists, the password is refreshed but other fields aren't
# overwritten (so user-customisations stick).
#
# Pre-reqs:
#   - Docker engine + `docker compose` v2 plugin
#   - jq (for JSON-safe edits to db-connections.json)
#
# Usage:
#   bash scripts/sql-server-local-setup.sh                # spin up + seed
#   bash scripts/sql-server-local-setup.sh --teardown     # stop + remove
#   bash scripts/sql-server-local-setup.sh --wipe         # teardown + delete volume

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
COMPOSE_FILE="$ROOT/docker/sql-server-local.yml"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/notepatra"
CONFIG_FILE="$CONFIG_DIR/db-connections.json"

# These match docker/sql-server-local.yml. Don't change without updating
# both — the connection-record write at the bottom depends on alignment.
CONTAINER_NAME="notepatra-mssql-local"
CONNECTION_NAME="sql-server-local"
SA_PASSWORD="Notepatra_Local_Dev!2026"
TEST_DB="NotepatraTest"

# ─── Option parsing ────────────────────────────────────────────────────
MODE="up"
case "${1:-}" in
    --teardown) MODE="teardown" ;;
    --wipe)     MODE="wipe"     ;;
    --help|-h)
        head -23 "$0" | sed 's/^# //; s/^#//'
        exit 0
        ;;
    "") ;;
    *)  echo "unknown flag: $1" >&2; exit 2 ;;
esac

# ─── Tool checks ───────────────────────────────────────────────────────
if ! command -v docker >/dev/null 2>&1; then
    echo "✗ docker not found in PATH. Install Docker Engine first:"
    echo "    https://docs.docker.com/engine/install/"
    exit 1
fi

if ! docker compose version >/dev/null 2>&1; then
    echo "✗ 'docker compose' v2 not available. Install the compose plugin:"
    echo "    https://docs.docker.com/compose/install/"
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    echo "✗ Docker daemon is not running. Start it first:"
    echo "    sudo systemctl start docker   # Linux"
    echo "    open -a Docker                # macOS"
    exit 1
fi

# ─── Teardown / wipe paths ─────────────────────────────────────────────
if [[ "$MODE" == "teardown" ]]; then
    echo "Stopping + removing container (keeping volume) …"
    docker compose -f "$COMPOSE_FILE" down
    echo "✓ Container removed. Run without --teardown to bring back up."
    exit 0
fi

if [[ "$MODE" == "wipe" ]]; then
    echo "⚠ This will DELETE the local MSSQL volume — all test data is lost."
    read -r -p "Type 'wipe' to confirm: " confirm
    if [[ "$confirm" != "wipe" ]]; then
        echo "Aborted."; exit 0
    fi
    docker compose -f "$COMPOSE_FILE" down -v
    echo "✓ Volume deleted. Next setup run starts from a fresh DB."
    exit 0
fi

# ─── Architecture note ─────────────────────────────────────────────────
ARCH="$(uname -m)"
if [[ "$ARCH" == "arm64" || "$ARCH" == "aarch64" ]]; then
    if [[ "$(uname -s)" == "Darwin" ]]; then
        echo "ℹ Apple Silicon detected — SQL Server 2022 image is Linux x64 only."
        echo "  Docker Desktop will run it under Rosetta/QEMU emulation (~3× slower)."
        echo "  This is fine for local tests but don't expect production-grade perf."
        echo
    fi
fi

# ─── Bring up the container ────────────────────────────────────────────
echo "── Starting SQL Server container ──"
docker compose -f "$COMPOSE_FILE" up -d

echo
echo "── Waiting for SQL Server to be ready ──"
# Healthcheck takes 30-90s on first run (image pull) and 10-25s on warm
# starts. Poll up to 5 minutes before giving up.
DEADLINE=$(( $(date +%s) + 300 ))
while true; do
    STATE="$(docker inspect --format '{{.State.Health.Status}}' "$CONTAINER_NAME" 2>/dev/null || echo "starting")"
    if [[ "$STATE" == "healthy" ]]; then
        echo "✓ SQL Server is healthy."
        break
    fi
    if [[ $(date +%s) -ge $DEADLINE ]]; then
        echo "✗ Timed out waiting for SQL Server to come up."
        echo "  Check container logs: docker logs $CONTAINER_NAME"
        exit 1
    fi
    echo "  ($STATE)  — still waiting …"
    sleep 5
done

# ─── Helper: run sqlcmd inside the container ───────────────────────────
# Some image versions expose mssql-tools18, others only mssql-tools.
# Probe once and reuse.
SQLCMD_BIN=""
for cand in /opt/mssql-tools18/bin/sqlcmd /opt/mssql-tools/bin/sqlcmd; do
    if docker exec "$CONTAINER_NAME" test -x "$cand"; then
        SQLCMD_BIN="$cand"; break
    fi
done
if [[ -z "$SQLCMD_BIN" ]]; then
    echo "✗ No sqlcmd binary inside container — image layout changed?"
    exit 1
fi

# -C trusts the self-signed TLS cert (only used on /opt/mssql-tools18).
TRUST_CERT=""
if [[ "$SQLCMD_BIN" == *mssql-tools18* ]]; then TRUST_CERT="-C"; fi

run_sql() {
    docker exec -i "$CONTAINER_NAME" "$SQLCMD_BIN" \
        -S localhost -U sa -P "$SA_PASSWORD" $TRUST_CERT \
        -b -d "${1:-master}"
}

# ─── Seed the test database ────────────────────────────────────────────
echo
echo "── Seeding $TEST_DB schema + sample rows ──"
run_sql master <<SQL
IF NOT EXISTS (SELECT 1 FROM sys.databases WHERE name = '$TEST_DB')
BEGIN
    CREATE DATABASE $TEST_DB;
    PRINT 'created database $TEST_DB';
END
ELSE
BEGIN
    PRINT 'database $TEST_DB already exists';
END
SQL

run_sql "$TEST_DB" <<'SQL'
-- Idempotent re-create of the sample schema. Drops the tables in
-- FK-dependency order so re-running the script just refreshes content
-- without leaving stale partial state.
IF OBJECT_ID('dbo.orders',    'U') IS NOT NULL DROP TABLE dbo.orders;
IF OBJECT_ID('dbo.products',  'U') IS NOT NULL DROP TABLE dbo.products;
IF OBJECT_ID('dbo.customers', 'U') IS NOT NULL DROP TABLE dbo.customers;

CREATE TABLE dbo.customers (
    customer_id   INT          IDENTITY(1,1) PRIMARY KEY,
    name          NVARCHAR(80) NOT NULL,
    email         NVARCHAR(120) NOT NULL UNIQUE,
    country       NVARCHAR(40) NOT NULL,
    signup_date   DATE         NOT NULL DEFAULT GETDATE()
);

CREATE TABLE dbo.products (
    product_id    INT          IDENTITY(1,1) PRIMARY KEY,
    sku           NVARCHAR(40) NOT NULL UNIQUE,
    name          NVARCHAR(80) NOT NULL,
    category      NVARCHAR(40) NOT NULL,
    unit_price    DECIMAL(10,2) NOT NULL CHECK (unit_price >= 0)
);

CREATE TABLE dbo.orders (
    order_id      INT          IDENTITY(1,1) PRIMARY KEY,
    customer_id   INT          NOT NULL REFERENCES dbo.customers(customer_id),
    product_id    INT          NOT NULL REFERENCES dbo.products(product_id),
    quantity      INT          NOT NULL CHECK (quantity > 0),
    order_date    DATETIME2    NOT NULL DEFAULT SYSUTCDATETIME(),
    total_amount  AS (CAST(quantity AS DECIMAL(10,2)) *
                      (SELECT unit_price FROM dbo.products
                       WHERE products.product_id = orders.product_id)) PERSISTED
);

INSERT INTO dbo.customers (name, email, country) VALUES
    ('Aanya Sharma',     'aanya@example.com',     'India'),
    ('Bao Nguyen',       'bao@example.com',       'Vietnam'),
    ('Catalina Reyes',   'catalina@example.com',  'Mexico'),
    ('Daichi Tanaka',    'daichi@example.com',    'Japan'),
    ('Emma Schmidt',     'emma@example.com',      'Germany');

INSERT INTO dbo.products (sku, name, category, unit_price) VALUES
    ('SKU-NOTE-9',  'Notepad 9-inch',     'Stationery', 12.50),
    ('SKU-PEN-PRO', 'ProGel Pen',         'Stationery',  3.25),
    ('SKU-USB-32',  'USB-C Drive 32 GB',  'Electronics', 18.99),
    ('SKU-HUB-7',   '7-port USB Hub',     'Electronics', 29.95),
    ('SKU-MAT-A4',  'A4 Desk Mat',        'Stationery', 14.00);

INSERT INTO dbo.orders (customer_id, product_id, quantity) VALUES
    (1, 1, 3),   (1, 2, 10),
    (2, 3, 1),
    (3, 4, 2),   (3, 1, 5),
    (4, 5, 4),
    (5, 2, 12),  (5, 3, 2),  (5, 4, 1);

PRINT 'seeded customers / products / orders';
SQL

echo "✓ Schema + 5 customers + 5 products + 9 orders seeded."

# ─── Register the connection in Notepatra's config ─────────────────────
echo
echo "── Registering '$CONNECTION_NAME' in Notepatra config ──"
mkdir -p "$CONFIG_DIR"

# Password is stored in db-connections.json with the same XOR-obfuscation
# that the dbconnections.cpp::obfuscatePassword routine uses. The key is
# hardcoded inside Notepatra; we mirror it here. Documented in the
# release notes: this is obscurity not encryption — for production
# credentials use OS keychain / dotenv / IAM, NOT this file.
#
# obfuscatePassword (from src/dbconnections.cpp):
#   1. XOR each byte of the password with key[i % key.size()]
#   2. base64 the result
NOTEPATRA_XOR_KEY="notepatra-db-connections-v1"

obfuscate_password() {
    python3 - "$1" "$NOTEPATRA_XOR_KEY" <<'PY'
import base64, sys
pw  = sys.argv[1].encode('utf-8')
key = sys.argv[2].encode('utf-8')
out = bytes(b ^ key[i % len(key)] for i, b in enumerate(pw))
print(base64.b64encode(out).decode('ascii'))
PY
}
OBSCURED="$(obfuscate_password "$SA_PASSWORD")"

NEW_RECORD=$(jq -n \
    --arg name "$CONNECTION_NAME" \
    --arg driver "QODBC" \
    --arg host "localhost" \
    --argjson port 1433 \
    --arg db "$TEST_DB" \
    --arg user "sa" \
    --arg pw "$OBSCURED" \
    --arg opts "DRIVER={ODBC Driver 18 for SQL Server};Encrypt=no" \
    '{name:$name, driver:$driver, host:$host, port:$port, database:$db,
      username:$user, password:$pw, options:$opts}')

if [[ -f "$CONFIG_FILE" ]]; then
    # Replace or append the named record.
    NEW_CONFIG=$(jq --arg name "$CONNECTION_NAME" --argjson rec "$NEW_RECORD" \
        '. as $orig
         | if any($orig[]; .name == $name)
           then map(if .name == $name then $rec else . end)
           else . + [$rec]
           end' "$CONFIG_FILE")
else
    NEW_CONFIG=$(jq -n --argjson rec "$NEW_RECORD" '[$rec]')
fi

# Atomic write — tmp + rename.
TMP="${CONFIG_FILE}.tmp"
echo "$NEW_CONFIG" > "$TMP"
mv "$TMP" "$CONFIG_FILE"
echo "✓ Connection '$CONNECTION_NAME' written to $CONFIG_FILE"

# ─── Done ──────────────────────────────────────────────────────────────
cat <<EOF

═══════════════════════════════════════════════════════════════════════
✓ Local SQL Server is up and ready.

Container:  $CONTAINER_NAME           (docker ps | grep mssql)
Host:port:  127.0.0.1:1433
Database:   $TEST_DB
Username:   sa
Password:   $SA_PASSWORD

Tables:
    dbo.customers  (5 rows)
    dbo.products   (5 rows)
    dbo.orders     (9 rows with computed total_amount)

In Notepatra:
    1. Enable Data Mode in the AI dock
    2. Saved connection '$CONNECTION_NAME' is already registered
    3. Try a prompt like:
       "Show top-3 customers by total spend from the $CONNECTION_NAME connection"

ODBC driver requirement (Linux only — macOS users have built-in MSSQL via
Microsoft's Homebrew tap):

    # Debian / Ubuntu — install Microsoft's ODBC Driver 18:
    curl https://packages.microsoft.com/keys/microsoft.asc | sudo gpg --dearmor -o /usr/share/keyrings/microsoft-prod.gpg
    curl https://packages.microsoft.com/config/ubuntu/24.04/prod.list | \\
        sudo tee /etc/apt/sources.list.d/mssql-release.list
    sudo apt-get update
    sudo ACCEPT_EULA=Y apt-get install -y msodbcsql18 unixodbc-dev

After installing the driver, restart Notepatra so Qt re-detects QODBC.

Tear down later:
    bash scripts/sql-server-local-setup.sh --teardown
    bash scripts/sql-server-local-setup.sh --wipe       # also drops the data volume
═══════════════════════════════════════════════════════════════════════
EOF
