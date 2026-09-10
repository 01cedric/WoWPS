# Local merchant stock

`import_vendor_catalog.py` compiles the pinned AzerothCore base `npc_vendor`,
`item_template` and `conditions` tables into static C++ arrays. No SQL database,
whole-world item preload or walking player bots are required at runtime.

The source archive `world_source_sql.tar.gz` supplies `item_template.sql`.
`vendor_source_sql.tar.gz` supplies the original `npc_vendor.sql` and
`conditions.sql`. Extract both into one directory, then run from the project root:

```sh
python3 tools/local_realm/import_vendor_catalog.py --sql-dir /path/to/sql \
  --output-dir include/game --report tools/local_realm/VENDOR_IMPORT_REPORT.json
```

The importer resolves negative vendor-list references and preserves the upstream
slot/item ordering. Current output: 23,327 purchasable gold offers for 1,758 NPC
entries, 1,711 item price rows, 400,608 bytes of read-only table data. The largest
shop contains 73 offers. Stock lookup uses the exact NPC entry; names, creature
level and category flags cannot create goods.

Original `BuyPrice` and `BuyCount` determine the price of a bundle. The native
and original FrameXML merchant buttons buy that bundle (for example 200 Rough Arrows for 10 copper),
instead of treating the bundle price as the cost of one item. A user-authored
catalog that changes `SellPrice` retains the older local pricing rule until that
catalog supplies its own price model.

Limited quantities use `maxcount` and `incrtime`. The authority shares depletion
between players by physical NPC spawn, independently of whether that NPC or its
terrain is currently rendered. Partial restock intervals survive later purchases.
Only depleted goods allocate state, capped at 4,096 records; fully replenished
records are reclaimed before additional allocation. A full ledger refuses a new
limited purchase rather than replenishing a different merchant. Restarting the
realm currently resets this ledger. LAN protocol 15 sends an authenticated
selected-NPC stock query and an owner-only stock/buyback snapshot. Up to 256
catalog offers fit in two pages of 128; the worst page is 1,237 bytes including
the 20-byte header and twelve buyback entries, below the 1,400-byte MTU bound.
Guests commit complete snapshots only, reject stale selection/revision pages,
and display the host's remaining quantities. Only one selected merchant is
retained per guest; queries refresh a three-second lease and host updates run
at most twice per second per selected peer, with query work rate-limited.

The import report explicitly lists excluded extended-currency, reputation-gated
and scripted/conditional offers. Their costs and eligibility are not implemented
by the local realm, so they are not exposed as unrestricted gold sales. Reputation
discounts and persistent depletion across app restarts also remain unimplemented.
An explicit custom catalog vendor list overrides the built-in table.

`tools/tests/run_vendor_stock_tests.sh` tests the production tables for Corina
Steele, Godric Rothgar and Andrew Krighton, bundle rounding, catalog overrides,
limited stock and bounded ledger behavior. Set `SANITIZE=1` for host ASan/UBSan.

The original `MerchantFrame.xml`/`.lua` are loaded from the player's MPQs. The
local API adapter publishes the selected NPC's stock to that frame, preserves
its two-column paging and coin widgets, and sends buy/sell/repair commands to
the authority. Quest merchants retain a gossip service link. The named NPC GUID
is carried in merchant commands so another closer vendor cannot substitute its
stock. The host validates identity, map, instance, talk range and living/friendly
state before any money or item changes.

Controller Square invokes the original purchase confirmation handler on a
merchant item. Square on a backpack item sells that stack while the merchant
window is open. High-price confirmation stays in the original popup; leaving
talk range closes the service. The twelve most recent sales form an owner-only
buyback ledger. Stable entry IDs prevent a delayed request from purchasing a
different row after another sale or buyback. The original buyback tab and compact
latest-sale button use each entry's actual sale price. Buying back checks the
selected friendly merchant, available gold and complete bag capacity before
changing either ledger or inventory. The thirteenth sale evicts the oldest row.
Save version 10 persists buyback alongside its character's bags and gold and
reads previous save versions 1–9 with an empty buyback ledger. Merchant buys,
sales and buybacks atomically save before success is acknowledged; write failure
restores bags, gold, buyback and any consumed limited stock in memory.
Equipment has no local durability wear, so repair is a
zero-cost/no-wear operation, not a complete durability simulation.

`run_merchant_authority_tests.sh` exercises the actual gameplay authority for
selected-NPC identity, quantity validation, atomic payment/inventory failure,
shared depleted stock after a streamed NPC is removed/re-added, and buyback
capacity, funds, replay rejection, stable IDs and the twelve-entry eviction rule.
`run_merchant_lan_tests.sh` exercises real UDP query/replies, maximum paged stock,
reordering/stale selection, authority service gates, save10/load9 and actual
disk-write failure rollback for buying, selling and buying back.
`run_merchant_framexml_tests.sh` accepts `WOWPS_RETAIL_FRAME_XML_DIR` pointing to
an external extracted 3.3.5 FrameXML directory and exercises its unchanged
`MerchantFrame.lua` after the local API adapter, including both original buyback
displays and controller activation. It verifies script behavior and
host widget state, not PS4 pixel output. No retail Lua files are bundled here.
