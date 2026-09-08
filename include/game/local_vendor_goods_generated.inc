// Generated data: the goods a local-realm merchant carries.
//
// WHY A TABLE AT ALL. Which items a particular merchant sells is npc_vendor, a
// server-side table the shipped world catalog does not carry, and the client's
// own Item.dbc holds only an item's class, subclass, display and inventory
// slot - no name, no price, no level. So there is nothing to derive a per-NPC
// stock list from, and this is a bounded documented fallback in the same shape
// as LocalTravelNetwork::builtinTransportRoutes(): a short table of real
// upstream item ids, every one of them checked against the player's own world
// data before it is offered, and superseded wholesale the moment a catalog
// carries a vendor list of its own (LocalNpcDefinition::vendorItems).
//
// PROVENANCE. Item ids, names and requiredlevel are verbatim item_template
// rows from https://github.com/azerothcore/azerothcore-wotlk
// commit 4e80596cdaa21fa31830522f6f2d7ed8750bfffd - the same dump the shipped catalog was
// compiled from. Prices are never taken from here: an item costs a multiple of
// the value the player's own catalog states for it, so a re-import re-prices
// the shop without touching this file.
//
// SELECTION. The staples a merchant of that kind stocks at every level tier:
// food and drink for each ten-level band, threads, vials, dyes and spices,
// the profession tools, arrows and shot, the class reagents, and the rogue
// poisons. All are quality-common, require no skill, spell or reputation, and
// exist in the shipped catalog (checked at generation time).
//
// WHICH MERCHANT CARRIES WHAT is real per-NPC data: creature_template.npcflag
// distinguishes VENDOR (0x80) from VENDOR_FOOD (0x200), VENDOR_AMMO (0x100),
// VENDOR_REAGENT (0x800) and VENDOR_POISON (0x400), and a merchant offers the
// union of the categories its own flags name. A barkeep sells food, a bowyer
// sells arrows, and neither sells the other's stock.
//
// Format: {itemId, category, requiredLevel}. Categories: 0 general, 1 food and
// drink, 2 ammunition, 3 class reagents, 4 rogue poisons.
// --- Food & drink ---
{  4540,1,  1},  // Tough Hunk of Bread          sell=1     cls=0/5
{  4541,1,  5},  // Freshly Baked Bread          sell=6     cls=0/5
{  4542,1, 15},  // Moist Cornbread              sell=25    cls=0/5
{  4544,1, 25},  // Mulgore Spice Bread          sell=50    cls=0/5
{  4601,1, 35},  // Soft Banana Bread            sell=100   cls=0/5
{  8950,1, 45},  // Homemade Cherry Pie          sell=200   cls=0/5
{ 27854,1, 55},  // Smoked Talbuk Venison        sell=280   cls=0/5
{   117,1,  1},  // Tough Jerky                  sell=1     cls=0/5
{  2287,1,  5},  // Haunch of Meat               sell=6     cls=0/5
{   414,1,  5},  // Dalaran Sharp                sell=6     cls=0/5
{   422,1, 15},  // Dwarven Mild                 sell=25    cls=0/5
{   159,1,  1},  // Refreshing Spring Water      sell=1     cls=0/5
{  1179,1,  5},  // Ice Cold Milk                sell=6     cls=0/5
{  1205,1, 15},  // Melon Juice                  sell=25    cls=0/5
{  1708,1, 25},  // Sweet Nectar                 sell=50    cls=0/5
{  1645,1, 35},  // Moonberry Juice              sell=100   cls=0/5
{  8766,1, 45},  // Morning Glory Dew            sell=200   cls=0/5
{ 28399,1, 60},  // Filtered Draenic Water       sell=280   cls=0/5
// --- General ---
{  2320,0,  0},  // Coarse Thread                sell=2     cls=7/5
{  2321,0,  0},  // Fine Thread                  sell=25    cls=7/5
{  4291,0,  0},  // Silken Thread                sell=125   cls=7/5
{  8343,0,  0},  // Heavy Silken Thread          sell=500   cls=7/5
{ 14341,0,  0},  // Rune Thread                  sell=1250  cls=7/5
{  2604,0,  0},  // Red Dye                      sell=12    cls=7/11
{  2605,0,  0},  // Green Dye                    sell=25    cls=7/11
{  3371,0,  0},  // Empty Vial                   sell=1     cls=7/11
{  3372,0,  0},  // Leaded Vial                  sell=10    cls=7/11
{  8925,0,  0},  // Crystal Vial                 sell=125   cls=7/11
{  2678,0,  0},  // Mild Spices                  sell=0     cls=7/11
{  3713,0,  0},  // Soothing Spices              sell=40    cls=7/11
{  4470,0,  0},  // Simple Wood                  sell=9     cls=7/11
{  4471,0,  0},  // Flint and Tinder             sell=33    cls=7/11
{  6256,0,  0},  // Fishing Pole                 sell=4     cls=2/20
{  6529,0,  0},  // Shiny Bauble                 sell=12    cls=0/8
{  5956,0,  1},  // Blacksmith Hammer            sell=3     cls=2/14
{  2901,0,  1},  // Mining Pick                  sell=16    cls=2/14
{  7005,0,  1},  // Skinning Knife               sell=16    cls=2/14
{  6217,0,  0},  // Copper Rod                   sell=24    cls=7/12
{  6339,0,  1},  // Runed Silver Rod             sell=24    cls=7/12
{ 20815,0,  1},  // Jeweler's Kit                sell=200   cls=7/11
// --- Ammunition ---
{  2512,2,  1},  // Rough Arrow                  sell=0     cls=6/2
{  2515,2, 10},  // Sharp Arrow                  sell=0     cls=6/2
{  3030,2, 25},  // Razor Arrow                  sell=0     cls=6/2
{ 11285,2, 40},  // Jagged Arrow                 sell=2     cls=6/2
{ 28053,2, 55},  // Wicked Arrow                 sell=2     cls=6/2
{  2516,2,  1},  // Light Shot                   sell=0     cls=6/3
{  2519,2, 10},  // Heavy Shot                   sell=0     cls=6/3
{  3033,2, 25},  // Solid Shot                   sell=0     cls=6/3
{ 11284,2, 40},  // Accurate Slugs               sell=1     cls=6/3
{ 28060,2, 55},  // Impact Shot                  sell=2     cls=6/3
// --- Class reagents ---
{ 17020,3,  0},  // Arcane Powder                sell=250   cls=15/1
{ 17021,3,  0},  // Wild Berries                 sell=175   cls=15/1
{ 17026,3,  0},  // Wild Thornroot               sell=250   cls=15/1
{ 17028,3,  0},  // Holy Candle                  sell=175   cls=15/1
{ 17029,3,  0},  // Sacred Candle                sell=250   cls=15/1
{ 17030,3,  0},  // Ankh                         sell=500   cls=5/0
{ 17031,3,  0},  // Rune of Teleportation        sell=250   cls=15/1
{ 17032,3,  0},  // Rune of Portals              sell=500   cls=15/1
{ 17033,3,  0},  // Symbol of Divinity           sell=500   cls=15/1
{ 17034,3,  0},  // Maple Seed                   sell=50    cls=15/1
{ 17035,3,  0},  // Stranglethorn Seed           sell=100   cls=15/1
{ 17036,3,  0},  // Ashwood Seed                 sell=200   cls=15/1
{ 17037,3,  0},  // Hornbeam Seed                sell=350   cls=15/1
{ 17038,3,  0},  // Ironwood Seed                sell=500   cls=15/1
{  5140,3,  0},  // Flash Powder                 sell=6     cls=15/1
// --- Rogue poisons ---
{  6947,4, 20},  // Instant Poison               sell=6     cls=0/8
{  3775,4, 20},  // Crippling Poison             sell=17    cls=0/8
{  5237,4, 24},  // Mind-numbing Poison          sell=10    cls=0/8
{  2892,4, 30},  // Deadly Poison                sell=25    cls=0/8
{  2893,4, 38},  // Deadly Poison II             sell=37    cls=0/8
