#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
python3 "$ROOT/tools/tests/item_instance_wire_contract_test.py"
python3 "$ROOT/tools/tests/local_reputation_contract_test.py"
python3 "$ROOT/tools/tests/local_profession_44_contract_test.py"
python3 "$ROOT/tools/tests/milestone4_regression_contract_test.py"
