// Generated data: which shipped-catalog creature offers which service.
//
// PROVENANCE. Every row is a verbatim creature_template.npcflag from the same
// AzerothCore dump the shipped world catalog was compiled from - repository
// https://github.com/azerothcore/azerothcore-wotlk
// commit 4e80596cdaa21fa31830522f6f2d7ed8750bfffd, table creature_template
// (see assets/local_realm/catalog/IMPORT_REPORT.json sourceHashes.creature_template
// and assets/local_realm/AUTHORS-AZEROTHCORE.txt for the licence and credits).
// Nothing here is guessed: npcflag is a server-side column, and this file is a
// transcription of it, not an inference from names, positions or behaviour.
//
// WHY IT EXISTS AT ALL. The catalog importer reads npcflag but does not emit
// it, so LocalNpcDefinition::npcFlags is zero for every shipped NPC and every
// service - vendor, repair, trainer, innkeeper, banker, auctioneer, even the
// flight master - would be invisible. A catalog that carries the field wins
// over this table wholesale; see localServiceNpcRecord(). That is the same
// arrangement LocalTravelNetwork::builtinTransportRoutes() uses.
//
// SELECTION. Only creatures that (a) exist in the shipped catalog's npcs.pack,
// so they can actually spawn, and (b) carry at least one of the service bits
// this realm implements. 2733 of the catalog's 14496 creatures qualify.
// Flags are masked to those bits; GOSSIP and QUESTGIVER are dropped because
// the catalog already carries questGiver of its own.
//
// TRAINER SUBJECT. trainerSkill and trainerClass are resolved from
// creature_template.subname, which names it in plain words ("Blacksmithing
// Trainer", "Mage Trainer"). The skill ids are the SkillLine.dbc ids for the
// fourteen professions this realm models; 0 means the subname named something
// this realm does not model (Pet, Portal, Weapon Master), and such a
// trainer teaches nothing rather than teaching the wrong thing. The subname is
// kept in the trailing comment so every row can be checked by eye.
//
// Format: {entry, serviceFlags, trainerSkill, trainerClass}, sorted by entry.
{    54,0x001080,   0, 0},  // Corina Steele <Weaponsmith>
{    66,0x000080,   0, 0},  // Tharynn Bouden <Trade Supplies>
{    74,0x001080,   0, 0},  // Kurran Steele <Cloth & Leather Armor Merchant>
{    78,0x001080,   0, 0},  // Janos Hammerknuckle <Weaponsmith>
{   151,0x000180,   0, 0},  // Brog Hamfist <General Supplies>
{   152,0x000180,   0, 0},  // Brother Danil <General Supplies>
{   167,0x001080,   0, 0},  // Morhan Coppertongue <Metalsmith>
{   190,0x001080,   0, 0},  // Dermot Johns <Cloth & Leather Armor Merchant>
{   198,0x000030,   0, 8},  // Khelden Bremen <Mage Trainer>
{   222,0x001080,   0, 0},  // Nillen Andemar <Macecrafter>
{   225,0x001080,   0, 0},  // Gavin Gnarltree <Weaponsmith>
{   226,0x001080,   0, 0},  // Morg Gnarltree <Armorer>
{   227,0x000180,   0, 0},  // Mabel Solaj <General Goods>
{   228,0x001180,   0, 0},  // Avette Fellwood <Bowyer>
{   233,0x000280,   0, 0},  // Farmer Saldean
{   258,0x000080,   0, 0},  // Joshua Maclure <Vintner>
{   274,0x000280,   0, 0},  // Barkeep Hann <Bartender>
{   277,0x000280,   0, 0},  // Roberto Pupellyverbos <Merlot Connoisseur>
{   295,0x010280,   0, 0},  // Innkeeper Farley <Innkeeper>
{   328,0x000030,   0, 8},  // Zaldimar Wefhellt <Mage Trainer>
{   331,0x000030,   0, 8},  // Maginor Dumas <Master Mage>
{   340,0x000080,   0, 0},  // Kendor Kabonka <Master of Cooking Recipes>
{   352,0x002000,   0, 0},  // Dungar Longdrink <Gryphon Master>
{   372,0x000080,   0, 0},  // Karm Ironquill <Mining Supplies>
{   375,0x000030,   0, 5},  // Priestess Anetta <Priest Trainer>
{   376,0x000030,   0, 5},  // High Priestess Laurena <Priest Trainer>
{   377,0x000030,   0, 5},  // Priestess Josetta <Priest Trainer>
{   384,0x000080,   0, 0},  // Katie Hunter <Horse Breeder>
{   459,0x000030,   0, 9},  // Drusilla La Salle <Warlock Trainer>
{   460,0x000030,   0, 9},  // Alamar Grimm <Warlock Trainer>
{   461,0x000030,   0, 9},  // Demisette Cloyce <Warlock Trainer>
{   465,0x000280,   0, 0},  // Barkeep Dobbins <Bartender>
{   483,0x000280,   0, 0},  // Elaine Trias <Mistress of Cheese>
{   491,0x000180,   0, 0},  // Quartermaster Lewis <Quartermaster>
{   514,0x000050, 164, 0},  // Smith Argus <Blacksmithing Trainer>
{   523,0x002000,   0, 0},  // Thor <Gryphon Master>
{   543,0x000010,   0, 0},  // Nalesette Wildbringer <Pet Trainer>
{   734,0x000380,   0, 0},  // Corporal Bluth <Camp Trader>
{   777,0x000080,   0, 0},  // Amy Davenport <Tradeswoman>
{   789,0x001180,   0, 0},  // Kimberly Hiett <Fletcher>
{   790,0x000080,   0, 0},  // Karen Taylor <Blacksmithing & Mining Supplies>
{   791,0x000180,   0, 0},  // Lindsay Ashlock <General Supplies>
{   793,0x001080,   0, 0},  // Kara Adams <Shield Crafter>
{   812,0x000050, 182, 0},  // Alma Jainrose <Herbalism Trainer>
{   829,0x000180,   0, 0},  // Adlin Pridedrift <General Supplies>
{   836,0x001080,   0, 0},  // Durnan Furcutter <Cloth & Leather Armor Merchant>
{   837,0x000030,   0, 5},  // Branstock Khalder <Priest Trainer>
{   843,0x000080,   0, 0},  // Gina MacGregor <Trade Supplies>
{   844,0x000080,   0, 0},  // Antonio Perelli <Traveling Salesman>
{   894,0x000280,   0, 0},  // Homer Stonefield <Fruit Seller>
{   895,0x000030,   0, 3},  // Thorgas Grimson <Hunter Trainer>
{   896,0x001080,   0, 0},  // Veldan Lightfoot <Leather Armor Merchant>
{   906,0x000030,   0, 9},  // Maximillian Crowe <Warlock Trainer>
{   908,0x000050, 182, 0},  // Flora Silverwind <Herbalism Trainer>
{   911,0x000030,   0, 1},  // Llane Beshere <Warrior Trainer>
{   912,0x000030,   0, 1},  // Thran Khorman <Warrior Trainer>
{   913,0x000030,   0, 1},  // Lyria Du Lac <Warrior Trainer>
{   914,0x000030,   0, 1},  // Ander Germaine <Warrior Trainer>
{   915,0x000030,   0, 4},  // Jorik Kerridan <Rogue Trainer>
{   916,0x000030,   0, 4},  // Solm Hargrin <Rogue Trainer>
{   917,0x000030,   0, 4},  // Keryn Sylvius <Rogue Trainer>
{   918,0x000030,   0, 4},  // Osborne the Night Man <Rogue Trainer>
{   925,0x000030,   0, 2},  // Brother Sammuel <Paladin Trainer>
{   926,0x000030,   0, 2},  // Bromos Grummner <Paladin Trainer>
{   927,0x000030,   0, 2},  // Brother Wilhelm <Paladin Trainer>
{   928,0x000030,   0, 2},  // Lord Grayson Shadowbreaker <Paladin Trainer>
{   931,0x002000,   0, 0},  // Ariena Stormfeather <Gryphon Master>
{   944,0x000030,   0, 8},  // Marryk Nurribit <Mage Trainer>
{   945,0x001080,   0, 0},  // Rybrad Coldbank <Weaponsmith>
{   954,0x001080,   0, 0},  // Kat Sampson <Leather Armor Merchant>
{   955,0x000280,   0, 0},  // Sergeant De Vries <Morale Officer>
{   956,0x001080,   0, 0},  // Dorin Songblade <Armorer>
{   958,0x000880,   0, 0},  // Dawn Brightstar <Arcane Goods>
{   959,0x001080,   0, 0},  // Morley Eberlein <Clothier>
{   960,0x000080,   0, 0},  // Gunder Thornbush <Tradesman>
{   980,0x001080,   0, 0},  // Grimnal <Mail & Plate Merchant>
{   981,0x001080,   0, 0},  // Hartash <Weapon Merchant>
{   982,0x000280,   0, 0},  // Thultash <Food & Drink Vendor>
{   983,0x000880,   0, 0},  // Thultazor <Alchemy Supplies & Reagents>
{   984,0x001080,   0, 0},  // Thralosh <Cloth & Leather Armor Merchant>
{   985,0x000030,   0, 1},  // Malosh <Warrior Trainer>
{   986,0x000030,   0, 7},  // Haromm <Shaman Trainer>
{   987,0x000030,   0, 3},  // Ogromm <Hunter Trainer>
{   988,0x000030,   0, 9},  // Kartosh <Warlock Trainer>
{   989,0x000080,   0, 0},  // Banalash <Trade Supplies>
{  1103,0x000050, 197, 0},  // Eldrin <Tailoring Trainer>
{  1104,0x001080,   0, 0},  // Grundel Harkin <Armorer>
{  1146,0x001080,   0, 0},  // Vharr <Superior Weaponsmith>
{  1147,0x001080,   0, 0},  // Hragran <Cloth & Leather Armor Merchant>
{  1148,0x000080,   0, 0},  // Nerrist <Trade Goods>
{  1149,0x000180,   0, 0},  // Uthok <General Supplies>
{  1198,0x001180,   0, 0},  // Rallic Finn <Bowyer>
{  1213,0x001080,   0, 0},  // Godric Rothgar <Armorer & Shieldcrafter>
{  1214,0x001080,   0, 0},  // Aldren Cordon <Clothier>
{  1215,0x000050, 171, 0},  // Alchemist Mallory <Alchemy Trainer>
{  1218,0x000050, 182, 0},  // Herbalist Pomeroy <Herbalism Trainer>
{  1226,0x000030,   0, 5},  // Maxan Anvol <Priest Trainer>
{  1228,0x000030,   0, 8},  // Magis Sparkmantle <Mage Trainer>
{  1229,0x000030,   0, 1},  // Granis Swiftaxe <Warrior Trainer>
{  1231,0x000030,   0, 3},  // Grif Wildheart <Hunter Trainer>
{  1232,0x000030,   0, 2},  // Azar Stronghammer <Paladin Trainer>
{  1234,0x000030,   0, 4},  // Hogral Bakkan <Rogue Trainer>
{  1237,0x000280,   0, 0},  // Kazan Mogosh <Food & Drink Merchant>
{  1238,0x001080,   0, 0},  // Gamili Frosthide <Cloth & Leather Armor Merchant>
{  1240,0x001080,   0, 0},  // Boran Ironclink <Armorer>
{  1241,0x000050, 164, 0},  // Tognus Flintfire <Blacksmithing Trainer>
{  1243,0x001180,   0, 0},  // Hegnar Rumbleshot <Gunsmith>
{  1247,0x010280,   0, 0},  // Innkeeper Belm <Innkeeper>
{  1249,0x001080,   0, 0},  // Quartermaster Hudson <Armorer & Shieldcrafter>
{  1250,0x000180,   0, 0},  // Drake Lindgren <General & Trade Supplies>
{  1257,0x000880,   0, 0},  // Keldric Boucher <Alchemy Supplies & Reagents>
{  1261,0x000080,   0, 0},  // Veron Amberstill <Ram Breeder>
{  1263,0x000080,   0, 0},  // Yarlyn Amberstill
{  1273,0x001080,   0, 0},  // Grawn Thromwyn <Weaponsmith>
{  1275,0x000880,   0, 0},  // Kyra Boucher <Reagents>
{  1285,0x000180,   0, 0},  // Thurman Mullby <General Goods>
{  1286,0x000080,   0, 0},  // Edna Mullby <Trade Supplies>
{  1287,0x001180,   0, 0},  // Marda Weller <Weapons Merchant>
{  1289,0x001080,   0, 0},  // Gunther Weller <Weapons Merchant>
{  1291,0x001080,   0, 0},  // Carla Granger <Cloth Armor Merchant>
{  1292,0x000050, 393, 0},  // Maris Granger <Skinning Trainer>
{  1294,0x001080,   0, 0},  // Aldric Moore <Mail Armor Merchant>
{  1295,0x001080,   0, 0},  // Lara Moore <Leather Armor Merchant>
{  1296,0x001080,   0, 0},  // Felder Stover <Weaponsmith>
{  1297,0x001180,   0, 0},  // Lina Stover <Bow & Gun Merchant>
{  1298,0x001180,   0, 0},  // Frederick Stover <Bow & Arrow Merchant>
{  1299,0x001080,   0, 0},  // Lisbeth Schneider <Clothier>
{  1301,0x000280,   0, 0},  // Julia Gallina <Wine Vendor>
{  1302,0x000080,   0, 0},  // Bernard Gump <Florist>
{  1303,0x000080,   0, 0},  // Felicia Gump <Herbalism Supplies>
{  1304,0x000080,   0, 0},  // Darian Singh <Fireworks Vendor>
{  1305,0x000280,   0, 0},  // Jarel Moor <Bartender>
{  1307,0x000880,   0, 0},  // Charys Yserian <Arcane Trinkets Vendor>
{  1308,0x000880,   0, 0},  // Owen Vaughn <Reagents>
{  1309,0x001080,   0, 0},  // Wynne Larson <Robe Merchant>
{  1310,0x001080,   0, 0},  // Evan Larson <Hatter>
{  1311,0x000280,   0, 0},  // Joachim Brenlow <Bartender>
{  1312,0x001080,   0, 0},  // Ardwyn Cailen <Wand Merchant>
{  1313,0x000080,   0, 0},  // Maria Lumere <Alchemy Supplies>
{  1314,0x001080,   0, 0},  // Duncan Cullen <Light Armor Merchant>
{  1315,0x001080,   0, 0},  // Allan Hafgan <Staves Merchant>
{  1316,0x000080,   0, 0},  // Adair Gilroy <Librarian>
{  1317,0x000050, 333, 0},  // Lucan Cordell <Enchanting Trainer>
{  1318,0x000080,   0, 0},  // Jessara Cordell <Enchanting Supplies>
{  1319,0x001080,   0, 0},  // Bryan Cross <Shield Merchant>
{  1320,0x001080,   0, 0},  // Seoman Griffith <Leather Armor Merchant>
{  1321,0x000080,   0, 0},  // Alyssa Griffith <Bag Vendor>
{  1322,0x001080,   0, 0},  // Maxton Strang <Mail Armor Merchant>
{  1323,0x001080,   0, 0},  // Osric Strang <Heavy Armor Merchant>
{  1324,0x001080,   0, 0},  // Heinrich Stone <Blade Merchant>
{  1325,0x000080,   0, 0},  // Jasper Fel <Shady Dealer>
{  1326,0x000480,   0, 0},  // Sloan McCoy <Poison Supplies>
{  1328,0x000280,   0, 0},  // Elly Langston <Barmaid>
{  1333,0x001080,   0, 0},  // Gerik Koen <Two Handed Weapon Merchant>
{  1339,0x001080,   0, 0},  // Mayda Thane <Cobbler>
{  1341,0x001080,   0, 0},  // Wilhelm Strang <Mail Armor Merchant>
{  1346,0x000050, 197, 0},  // Georgio Bolero <Tailoring Trainer>
{  1347,0x000080,   0, 0},  // Alexandra Bolero <Tailoring Supplies>
{  1348,0x001080,   0, 0},  // Gregory Ardus <Staff & Mace Merchant>
{  1349,0x001080,   0, 0},  // Agustus Moulaine <Mail Armor Merchant>
{  1350,0x001080,   0, 0},  // Theresa Moulaine <Robe Vendor>
{  1351,0x000880,   0, 0},  // Brother Cassius <Reagents>
{  1355,0x000050, 185, 0},  // Cook Ghilm <Cooking Trainer>
{  1362,0x001080,   0, 0},  // Gothor Brumn <Armorer>
{  1381,0x001080,   0, 0},  // Krakk <Superior Armorer>
{  1382,0x000050, 185, 0},  // Mudduk <Superior Cook>
{  1385,0x000050, 165, 0},  // Brawn <Leatherworking Trainer>
{  1386,0x000050, 171, 0},  // Rogvar <Alchemy Trainer>
{  1387,0x002000,   0, 0},  // Thysta <Wind Rider Master>
{  1404,0x000030,   0, 3},  // Kragg <Hunter Trainer>
{  1407,0x001080,   0, 0},  // Sranda <Light Armor & Weapons Merchant>
{  1411,0x000030,   0, 4},  // Ian Strom <Rogue Trainer>
{  1430,0x000050, 185, 0},  // Tomas <Cook>
{  1441,0x001080,   0, 0},  // Brak Durnad <Weaponsmith>
{  1448,0x000180,   0, 0},  // Neal Allen <Engineering Supplies & General Goods>
{  1450,0x001080,   0, 0},  // Brahnmar <Armorer>
{  1452,0x000180,   0, 0},  // Gruham Rumdnul <General Supplies>
{  1453,0x000080,   0, 0},  // Dewin Shimmerdawn <Alchemy Supplies>
{  1454,0x001080,   0, 0},  // Jennabink Powerseam <Tailoring Supplies & Specialty Goods>
{  1456,0x000080,   0, 0},  // Kersok Prond <Tradesman>
{  1457,0x000480,   0, 0},  // Samor Festivus <Shady Dealer>
{  1458,0x000050, 182, 0},  // Telurinon Moonshadow <Herbalism Trainer>
{  1459,0x001180,   0, 0},  // Naela Trance <Bowyer>
{  1460,0x000080,   0, 0},  // Unger Statforth <Horse Breeder>
{  1461,0x001180,   0, 0},  // Murndan Derth <Gunsmith>
{  1462,0x001180,   0, 0},  // Edwina Monzor <Fletcher>
{  1463,0x000880,   0, 0},  // Falkan Armonis <Reagents>
{  1464,0x010280,   0, 0},  // Innkeeper Helbrek <Innkeeper>
{  1465,0x000080,   0, 0},  // Drac Roughcut <Tradesman>
{  1469,0x001180,   0, 0},  // Vrok Blunderblast <Gunsmith>
{  1470,0x000050, 171, 0},  // Ghak Healtouch <Alchemy Trainer>
{  1471,0x001080,   0, 0},  // Jannos Ironwill <Superior Macecrafter>
{  1473,0x000050, 182, 0},  // Kali Healtouch <Herbalism Trainer>
{  1474,0x000080,   0, 0},  // Rann Flamespinner <Tailoring Supplies>
{  1571,0x002000,   0, 0},  // Shellei Brondir <Gryphon Master>
{  1572,0x002000,   0, 0},  // Thorgrum Borrelson <Gryphon Master>
{  1573,0x002000,   0, 0},  // Gryth Thurden <Gryphon Master>
{  1632,0x000050, 165, 0},  // Adele Fielder <Leatherworking Trainer>
{  1645,0x001080,   0, 0},  // Quartermaster Hicks <Master Weaponsmith>
{  1650,0x000080,   0, 0},  // Terry Palin <Lumberjack>
{  1651,0x000050, 356, 0},  // Lee Brown <Fishing Trainer>
{  1668,0x001180,   0, 0},  // William MacGregor <Bowyer>
{  1669,0x001080,   0, 0},  // Defias Profiteer <Free Wheeling Merchant>
{  1670,0x000280,   0, 0},  // Mike Miller <Bread Merchant>
{  1671,0x000280,   0, 0},  // Lamar Veisilli <Fruit Seller>
{  1672,0x000080,   0, 0},  // Lohgan Eva <Tailoring Supplies>
{  1673,0x000880,   0, 0},  // Alyssa Eva <Reagents>
{  1676,0x000050, 202, 0},  // Finbus Geargrind <Engineering Trainer>
{  1678,0x000280,   0, 0},  // Vernon Hale <Fishing Supplies>
{  1680,0x000050, 356, 0},  // Matthew Hooper <Fishing Trainer>
{  1681,0x000050, 186, 0},  // Brock Stoneseeker <Mining Trainer>
{  1682,0x000180,   0, 0},  // Yanni Stoutheart <General Supplies>
{  1683,0x000050, 356, 0},  // Warg Deepwater <Fishing Trainer>
{  1684,0x000280,   0, 0},  // Khara Deepwater <Fishing Supplies>
{  1685,0x000180,   0, 0},  // Xandar Goodbeard <General Supplies>
{  1686,0x001080,   0, 0},  // Irene Sureshot <Gunsmith>
{  1687,0x001180,   0, 0},  // Cliff Hadin <Bowyer>
{  1690,0x001080,   0, 0},  // Thrawn Boltar <Blacksmithing Supplies>
{  1691,0x000180,   0, 0},  // Kreg Bilmn <General Supplies>
{  1692,0x000080,   0, 0},  // Golorn Frostbeard <Tradesman>
{  1694,0x000080,   0, 0},  // Loslor Rudge <Engineering Supplies>
{  1695,0x001080,   0, 0},  // Rendow <Leather Armor Merchant>
{  1697,0x000280,   0, 0},  // Keeg Gibn <Ale & Wine>
{  1698,0x001080,   0, 0},  // Frast Dokner <Apprentice Weaponsmith>
{  1699,0x000050, 185, 0},  // Gremlock Pilsnor <Cooking Trainer>
{  1700,0x000050, 356, 0},  // Paxton Ganter <Fishing Trainer>
{  1701,0x000050, 186, 0},  // Dank Drizzlecut <Mining Trainer>
{  1702,0x000050, 202, 0},  // Bronk Guzzlegear <Engineering Trainer>
{  1901,0x000030,   0, 1},  // Kelstrum Stonebreaker <Warrior Trainer>
{  2046,0x001080,   0, 0},  // Andrew Krighton <Armorer & Shieldcrafter>
{  2084,0x000180,   0, 0},  // Natheril Raincaller <General Goods>
{  2113,0x001080,   0, 0},  // Archibald Kava <Cloth & Leather Armor Merchant>
{  2114,0x000050, 182, 0},  // Faruza <Herbalism Trainer>
{  2115,0x000180,   0, 0},  // Joshua Kien <General Supplies>
{  2116,0x001080,   0, 0},  // Blacksmith Rand <Apprentice Armorer>
{  2117,0x001080,   0, 0},  // Harold Raims <Apprentice Weaponsmith>
{  2118,0x000080,   0, 0},  // Abigail Shiel <Trade Supplies>
{  2119,0x000030,   0, 1},  // Dannal Stern <Warrior Trainer>
{  2122,0x000030,   0, 4},  // David Trias <Rogue Trainer>
{  2123,0x000030,   0, 5},  // Dark Cleric Duesten <Priest Trainer>
{  2124,0x000030,   0, 8},  // Isabella <Mage Trainer>
{  2126,0x000030,   0, 9},  // Maximillion <Warlock Trainer>
{  2127,0x000030,   0, 9},  // Rupert Boch <Warlock Trainer>
{  2128,0x000030,   0, 8},  // Cain Firesong <Mage Trainer>
{  2129,0x000030,   0, 5},  // Dark Cleric Beryl <Priest Trainer>
{  2130,0x000030,   0, 4},  // Marion Call <Rogue Trainer>
{  2131,0x000030,   0, 1},  // Austil de Mon <Warrior Trainer>
{  2132,0x000050, 171, 0},  // Carolai Anise <Alchemy Trainer>
{  2134,0x000180,   0, 0},  // Mrs. Winters <General Supplies>
{  2135,0x001080,   0, 0},  // Abe Winters <Apprentice Armorer>
{  2136,0x001080,   0, 0},  // Oliver Dwor <Apprentice Weaponsmith>
{  2137,0x001080,   0, 0},  // Eliza Callen <Leather Armor Merchant>
{  2140,0x000180,   0, 0},  // Edwin Harly <General Supplies>
{  2226,0x002000,   0, 0},  // Karos Razok <Bat Handler>
{  2264,0x000080,   0, 0},  // Hillsbrad Tailor
{  2265,0x000080,   0, 0},  // Hillsbrad Apprentice Blacksmith
{  2299,0x002000,   0, 0},  // Borgus Stoutarm <Gryphon Master>
{  2303,0x000280,   0, 0},  // Lyranne Feathersong <Food & Drink>
{  2326,0x000050, 129, 0},  // Thamner Pol <Physician>
{  2327,0x000050, 129, 0},  // Shaina Fuller <First Aid Trainer>
{  2329,0x000050, 129, 0},  // Michelle Belle <Physician>
{  2352,0x010280,   0, 0},  // Innkeeper Anderson <Innkeeper>
{  2357,0x000080,   0, 0},  // Merideth Carlson <Horse Breeder>
{  2364,0x000280,   0, 0},  // Neema <Waitress>
{  2365,0x000280,   0, 0},  // Bront Coldcleave <Butcher>
{  2366,0x000280,   0, 0},  // Barkeep Kelly <Bartender>
{  2367,0x000050, 356, 0},  // Donald Rabonne <Fishing Trainer>
{  2380,0x000080,   0, 0},  // Nandar Branson <Alchemy Supplies>
{  2381,0x000080,   0, 0},  // Micha Yance <Trade Goods>
{  2383,0x000080,   0, 0},  // Lindea Rabonne <Fishing Supplies>
{  2388,0x010280,   0, 0},  // Innkeeper Shay <Innkeeper>
{  2389,0x002000,   0, 0},  // Zarise <Bat Handler>
{  2390,0x000050, 182, 0},  // Aranae Venomblood <Herbalism Trainer>
{  2391,0x000050, 171, 0},  // Serge Hinott <Alchemy Trainer>
{  2393,0x000080,   0, 0},  // Christoph Jeffcoat <Tradesman>
{  2394,0x000080,   0, 0},  // Mallen Swain <Tailoring Supplies>
{  2397,0x000080,   0, 0},  // Derak Nightfall <Cook>
{  2399,0x000050, 197, 0},  // Daryl Stack <Tailoring Trainer>
{  2401,0x000180,   0, 0},  // Kayren Soothallow <General Goods>
{  2404,0x001080,   0, 0},  // Blacksmith Verringtan
{  2409,0x002000,   0, 0},  // Felicia Maline <Gryphon Master>
{  2432,0x002000,   0, 0},  // Darla Harris <Gryphon Master>
{  2455,0x020000,   0, 0},  // Olivia Burnside <Banker>
{  2456,0x020000,   0, 0},  // Newton Burnside <Banker>
{  2457,0x020000,   0, 0},  // John Burnside <Banker>
{  2458,0x020000,   0, 0},  // Randolph Montague <Banker>
{  2459,0x020000,   0, 0},  // Mortimer Montague <Banker>
{  2460,0x020000,   0, 0},  // Barnum Stonemantle <Banker>
{  2461,0x020000,   0, 0},  // Bailey Stonemantle <Banker>
{  2480,0x000080,   0, 0},  // Bro'kin <Alchemy Supplies>
{  2481,0x000080,   0, 0},  // Bliztik <Alchemy Supplies>
{  2482,0x001080,   0, 0},  // Zarena Cromwind <Superior Weaponsmith>
{  2483,0x001080,   0, 0},  // Jaquilina Dramet <Superior Axecrafter>
{  2485,0x000030,   0, 0},  // Larimaine Purdue <Portal Trainer>
{  2489,0x000030,   0, 0},  // Milstaff Stormeye <Portal Trainer>
{  2492,0x000030,   0, 0},  // Lexington Mortaim <Portal Trainer>
{  2622,0x000480,   0, 0},  // Sly Garrett <Shady Goods>
{  2625,0x020000,   0, 0},  // Viznik Goldgrubber <Banker>
{  2626,0x000080,   0, 0},  // Old Man Heming <Fishing Supplies>
{  2627,0x000050, 197, 0},  // Grarnik Goodstitch <Tailoring Trainer>
{  2663,0x000080,   0, 0},  // Narkk <Pirate Supplies>
{  2664,0x000080,   0, 0},  // Kelsey Yance <Cook>
{  2668,0x000080,   0, 0},  // Danielle Zipstitch <Tailoring Supplies>
{  2669,0x000080,   0, 0},  // Sheri Zipstitch <Tailoring Supplies>
{  2670,0x000080,   0, 0},  // Xizk Goodstitch <Tailoring Supplies>
{  2672,0x000080,   0, 0},  // Cowardly Crosby <Tailoring Supplies>
{  2679,0x001080,   0, 0},  // Wenna Silkbeard <Special Goods Dealer>
{  2682,0x000080,   0, 0},  // Fradd Swiftgear <Engineering Supplies>
{  2683,0x000080,   0, 0},  // Namdo Bizzfizzle <Engineering Supplies>
{  2684,0x000080,   0, 0},  // Rizz Loosebolt <Engineering Supplies>
{  2685,0x000180,   0, 0},  // Mazk Snipeshot <Engineering Supplies>
{  2687,0x000080,   0, 0},  // Gnaz Blunderflame <Engineering Supplies>
{  2688,0x000080,   0, 0},  // Ruppo Zipcoil <Engineering Supplies>
{  2697,0x000080,   0, 0},  // Clyde Ranthal <Leatherworking Supplies>
{  2698,0x000080,   0, 0},  // George Candarte <Leatherworking Supplies>
{  2699,0x000080,   0, 0},  // Rikqiz <Leatherworking Supplies>
{  2704,0x000050,   0, 0},  // Hanashi <Weapon Master>
{  2798,0x000050, 129, 0},  // Pand Stonebinder <First Aid Trainer>
{  2803,0x000180,   0, 0},  // Malygen <General Goods>
{  2805,0x000880,   0, 0},  // Deneb Walker <Reagents>
{  2806,0x000380,   0, 0},  // Bale <General Goods>
{  2808,0x000180,   0, 0},  // Vikki Lonsav <General Goods>
{  2810,0x000080,   0, 0},  // Hammon Karwn <Superior Tradesman>
{  2812,0x000080,   0, 0},  // Drovnar Strongbrew <Alchemy Supplies>
{  2814,0x000280,   0, 0},  // Narj Deepslice <Butcher>
{  2816,0x000080,   0, 0},  // Androd Fadran <Leatherworking Supplies>
{  2818,0x000050, 185, 0},  // Slagg <Superior Butcher>
{  2819,0x000080,   0, 0},  // Tunkk <Leatherworking Supplies>
{  2820,0x000180,   0, 0},  // Graud <General Goods>
{  2821,0x000080,   0, 0},  // Keena <Trade Goods>
{  2832,0x000280,   0, 0},  // Nixxrax Fillamug <Food & Drink>
{  2834,0x000050, 356, 0},  // Myizz Luckycatch <Fishing Trainer>
{  2835,0x002000,   0, 0},  // Cedrik Prose <Gryphon Master>
{  2836,0x000050, 164, 0},  // Brikk Keencraft <Blacksmithing Trainer>
{  2837,0x000050, 171, 0},  // Jaxin Chong <Alchemy Trainer>
{  2838,0x000080,   0, 0},  // Crazk Sparks <Fireworks Merchant>
{  2839,0x001180,   0, 0},  // Haren Kanmae <Superior Bowyer>
{  2840,0x001080,   0, 0},  // Kizz Bluntstrike <Macecrafter>
{  2842,0x000280,   0, 0},  // Wigcik <Superior Fisherman>
{  2843,0x001080,   0, 0},  // Jutak <Blade Trader>
{  2844,0x001080,   0, 0},  // Hurklor <Blacksmithing Supplies>
{  2845,0x001080,   0, 0},  // Fargon Mortalak <Superior Armorer>
{  2846,0x000080,   0, 0},  // Blixrez Goodstitch <Leatherworking Supplies>
{  2847,0x001080,   0, 0},  // Jansen Underwood <Blacksmithing Supplies>
{  2848,0x000080,   0, 0},  // Glyx Brewright <Alchemy Supplies>
{  2849,0x001080,   0, 0},  // Qixdi Goodstitch <Cloth Armor & Accessories>
{  2851,0x002000,   0, 0},  // Urda <Wind Rider Master>
{  2856,0x000050, 182, 0},  // Angrun <Herbalism Trainer>
{  2858,0x002000,   0, 0},  // Gringer <Wind Rider Master>
{  2859,0x002000,   0, 0},  // Gyll <Gryphon Master>
{  2861,0x002000,   0, 0},  // Gorrik <Wind Rider Master>
{  2878,0x000030,   0, 0},  // Peria Lamenur <Pet Trainer>
{  2879,0x000030,   0, 0},  // Karrina Mekenda <Pet Trainer>
{  2908,0x000180,   0, 0},  // Grawl <General Goods>
{  2941,0x002000,   0, 0},  // Lanie Reed <Gryphon Master>
{  2995,0x002000,   0, 0},  // Tal <Wind Rider Master>
{  2996,0x020000,   0, 0},  // Torn <Banker>
{  2997,0x001180,   0, 0},  // Jyn Stonehoof <Weapons Merchant>
{  2998,0x000050, 164, 0},  // Karn Stonehoof <Blacksmithing Trainer>
{  2999,0x001080,   0, 0},  // Taur Stonehoof <Blacksmithing Supplies>
{  3000,0x001080,   0, 0},  // Gibbert <Weapon Merchant>
{  3001,0x000050, 186, 0},  // Brek Stonehoof <Mining Trainer>
{  3002,0x000080,   0, 0},  // Kurm Stonehoof <Mining Supplies>
{  3003,0x000280,   0, 0},  // Fyr Mistrunner <Bread Vendor>
{  3004,0x000050, 197, 0},  // Tepa <Tailoring Trainer>
{  3005,0x000080,   0, 0},  // Mahu <Leatherworking & Tailoring Supplies>
{  3007,0x000050, 165, 0},  // Una <Leatherworking Trainer>
{  3009,0x000050, 171, 0},  // Bena Winterhoof <Alchemy Trainer>
{  3010,0x000080,   0, 0},  // Mani Winterhoof <Alchemy Supplies>
{  3011,0x000050, 333, 0},  // Teg Dawnstrider <Enchanting Trainer>
{  3012,0x000080,   0, 0},  // Nata Dawnstrider <Enchanting Supplies>
{  3013,0x000050, 182, 0},  // Komin Winterhoof <Herbalism Trainer>
{  3014,0x000080,   0, 0},  // Nida Winterhoof <Herbalism Supplies>
{  3015,0x001180,   0, 0},  // Kuna Thunderhorn <Bowyer & Fletching Goods>
{  3016,0x000080,   0, 0},  // Tand <Basket Weaver>
{  3017,0x000280,   0, 0},  // Nan Mistrunner <Fruit Vendor>
{  3018,0x001180,   0, 0},  // Hogor Thunderhoof <Guns Merchant>
{  3019,0x001080,   0, 0},  // Delgo Ragetotem <Axe Merchant>
{  3020,0x001080,   0, 0},  // Etu Ragetotem <Mace & Staff Merchant>
{  3021,0x001080,   0, 0},  // Kard Ragetotem <Sword & Dagger Merchant>
{  3022,0x001080,   0, 0},  // Sunn Ragetotem <Staff Merchant>
{  3023,0x001080,   0, 0},  // Sura Wildmane <War Harness Vendor>
{  3025,0x000280,   0, 0},  // Kaga Mistrunner <Meat Vendor>
{  3026,0x000050, 185, 0},  // Aska Mistrunner <Cooking Trainer>
{  3027,0x000080,   0, 0},  // Naal Mistrunner <Cooking Supplies>
{  3028,0x000050, 356, 0},  // Kah Mistrunner <Fishing Trainer>
{  3029,0x000080,   0, 0},  // Sewa Mistrunner <Fishing Supplies>
{  3030,0x000030,   0, 7},  // Siln Skychaser <Shaman Trainer>
{  3031,0x000030,   0, 7},  // Tigor Skychaser <Shaman Trainer>
{  3032,0x000030,   0, 7},  // Beram Skychaser <Shaman Trainer>
{  3033,0x000030,   0,11},  // Turak Runetotem <Druid Trainer>
{  3034,0x000030,   0,11},  // Sheal Runetotem <Druid Trainer>
{  3036,0x000030,   0,11},  // Kym Wildmane <Druid Trainer>
{  3038,0x000030,   0, 3},  // Kary Thunderhorn <Hunter Trainer>
{  3039,0x000030,   0, 3},  // Holt Thunderhorn <Hunter Trainer>
{  3040,0x000030,   0, 3},  // Urek Thunderhorn <Hunter Trainer>
{  3041,0x000030,   0, 1},  // Torm Ragetotem <Warrior Trainer>
{  3042,0x000030,   0, 1},  // Sark Ragetotem <Warrior Trainer>
{  3043,0x000030,   0, 1},  // Ker Ragetotem <Warrior Trainer>
{  3044,0x0000b0,   0, 5},  // Miles Welsh <Priest Trainer>
{  3045,0x000030,   0, 5},  // Malakai Cross <Priest Trainer>
{  3046,0x000030,   0, 5},  // Father Cobb <Priest Trainer>
{  3047,0x000030,   0, 8},  // Archmage Shymm <Mage Trainer>
{  3048,0x000030,   0, 8},  // Ursyn Ghull <Mage Trainer>
{  3049,0x000030,   0, 8},  // Thurston Xane <Mage Trainer>
{  3053,0x001180,   0, 0},  // Synge <Gun Merchant>
{  3059,0x000030,   0, 1},  // Harutt Thunderhorn <Warrior Trainer>
{  3060,0x000030,   0,11},  // Gart Mistrunner <Druid Trainer>
{  3061,0x000030,   0, 3},  // Lanka Farshot <Hunter Trainer>
{  3062,0x000030,   0, 7},  // Meela Dawnstrider <Shaman Trainer>
{  3063,0x000030,   0, 1},  // Krang Stonehoof <Warrior Trainer>
{  3064,0x000030,   0,11},  // Gennia Runetotem <Druid Trainer>
{  3065,0x000030,   0, 3},  // Yaw Sharpmane <Hunter Trainer>
{  3066,0x000030,   0, 7},  // Narm Skychaser <Shaman Trainer>
{  3067,0x000050, 185, 0},  // Pyall Silentstride <Cook>
{  3069,0x000050, 165, 0},  // Chaw Stronghide <Leatherworking Trainer>
{  3072,0x000180,   0, 0},  // Kawnie Softbreeze <General Goods>
{  3073,0x001080,   0, 0},  // Marjak Keenblade <Weaponsmith>
{  3074,0x001080,   0, 0},  // Varia Hardhide <Leather Armor Merchant>
{  3075,0x001080,   0, 0},  // Bronk Steelrage <Armorer & Shieldcrafter>
{  3076,0x000180,   0, 0},  // Moorat Longstride <General Goods>
{  3077,0x001080,   0, 0},  // Mahnott Roughwound <Weaponsmith>
{  3078,0x001180,   0, 0},  // Kennah Hawkseye <Gunsmith>
{  3079,0x001080,   0, 0},  // Varg Windwhisper <Leather Armor Merchant>
{  3080,0x001080,   0, 0},  // Harant Ironbrace <Armorer & Shieldcrafter>
{  3081,0x000080,   0, 0},  // Wunna Darkmane <Trade Goods>
{  3085,0x000080,   0, 0},  // Gloria Femmel <Cooking Supplies>
{  3086,0x000280,   0, 0},  // Gretchen Vogel <Waitress>
{  3087,0x000050, 185, 0},  // Crystal Boughman <Cooking Trainer>
{  3088,0x001180,   0, 0},  // Henry Chapal <Gunsmith>
{  3089,0x000280,   0, 0},  // Sherman Femmel <Butcher>
{  3090,0x000480,   0, 0},  // Gerald Crawley <Poison Supplies>
{  3091,0x000080,   0, 0},  // Franklin Hamar <Tailoring Supplies>
{  3092,0x001080,   0, 0},  // Tagain <Cloth Armor Merchant>
{  3093,0x001080,   0, 0},  // Grod <Leather Armor Merchant>
{  3095,0x001080,   0, 0},  // Fela <Heavy Armor Merchant>
{  3096,0x000080,   0, 0},  // Captured Servant of Azora <Specialist Tailoring Supplies>
{  3097,0x001080,   0, 0},  // Bernard Brubaker <Leather Armor Merchant>
{  3133,0x000080,   0, 0},  // Herble Baubbletump <Engineering & Mining Supplies>
{  3134,0x000080,   0, 0},  // Kzixx <Rare Goods>
{  3135,0x000480,   0, 0},  // Malissa <Poison Supplies>
{  3136,0x000050, 164, 0},  // Clarise Gnarltree <Blacksmithing Trainer>
{  3137,0x000050, 186, 0},  // Matt Johnson <Mining Trainer>
{  3138,0x000280,   0, 0},  // Scott Carevin <Mushroom Seller>
{  3153,0x000030,   0, 1},  // Frang <Warrior Trainer>
{  3154,0x000030,   0, 3},  // Jen'shan <Hunter Trainer>
{  3155,0x000030,   0, 4},  // Rwag <Rogue Trainer>
{  3156,0x000030,   0, 9},  // Nartok <Warlock Trainer>
{  3157,0x000030,   0, 7},  // Shikrik <Shaman Trainer>
{  3158,0x000180,   0, 0},  // Duokna <General Goods>
{  3159,0x001080,   0, 0},  // Kzan Thornslash <Weaponsmith>
{  3160,0x001080,   0, 0},  // Huklah <Cloth & Leather Armor Merchant>
{  3161,0x001080,   0, 0},  // Rarc <Armorer & Shieldcrafter>
{  3162,0x001080,   0, 0},  // Burdrak Harglhelm <Leather Armor Merchant>
{  3163,0x001080,   0, 0},  // Uhgar <Weaponsmith>
{  3164,0x000180,   0, 0},  // Jark <General Goods>
{  3165,0x001180,   0, 0},  // Ghrawt <Bowyer>
{  3166,0x001080,   0, 0},  // Cutac <Cloth & Leather Armor Merchant>
{  3167,0x001080,   0, 0},  // Wuark <Armorer & Shieldcrafter>
{  3168,0x000080,   0, 0},  // Flakk <Trade Supplies>
{  3169,0x000030,   0, 1},  // Tarshaw Jaggedscar <Warrior Trainer>
{  3170,0x000030,   0, 4},  // Kaplak <Rogue Trainer>
{  3171,0x000030,   0, 3},  // Thotar <Hunter Trainer>
{  3172,0x000030,   0, 9},  // Dhugru Gorelust <Warlock Trainer>
{  3173,0x000030,   0, 7},  // Swart <Shaman Trainer>
{  3174,0x000050, 164, 0},  // Dwukk <Blacksmithing Trainer>
{  3175,0x000050, 186, 0},  // Krunn <Mining Trainer>
{  3177,0x001080,   0, 0},  // Turuk Amberstill <Dwarven Weaponsmith>
{  3178,0x000280,   0, 0},  // Stuart Fleming <Fishing Supplies>
{  3179,0x000050, 356, 0},  // Harold Riggs <Fishing Trainer>
{  3180,0x000080,   0, 0},  // Dark Iron Entrepreneur <Speciality Goods>
{  3181,0x000050, 129, 0},  // Fremal Doohickey <First Aid Trainer>
{  3184,0x000050, 171, 0},  // Miao'zan <Alchemy Trainer>
{  3185,0x000050, 182, 0},  // Mishiki <Herbalism Trainer>
{  3186,0x000180,   0, 0},  // K'waii <General Goods>
{  3187,0x000080,   0, 0},  // Tai'tasi <Trade Supplies>
{  3290,0x000050, 202, 0},  // Deek Fizzlebizz <Engineering Trainer>
{  3291,0x000280,   0, 0},  // Greishan Ironstove <Traveling Merchant>
{  3298,0x000280,   0, 0},  // Gabrielle Chase <Food & Drink>
{  3305,0x002000,   0, 0},  // Grisha <Wind Rider Master>
{  3306,0x000030,   0, 0},  // Keldas <Pet Trainer>
{  3309,0x020000,   0, 0},  // Karus <Banker>
{  3310,0x002000,   0, 0},  // Doras <Wind Rider Master>
{  3312,0x000280,   0, 0},  // Olvia <Meat Vendor>
{  3313,0x000180,   0, 0},  // Trak'gen <General Goods>
{  3314,0x001080,   0, 0},  // Urtharo <Weapon Merchant>
{  3315,0x001080,   0, 0},  // Tor'phan <Cloth & Leather Armor Merchant>
{  3316,0x001080,   0, 0},  // Handor <Cloth & Leather Armor Merchant>
{  3317,0x001080,   0, 0},  // Ollanus <Light Armor Merchant>
{  3318,0x020000,   0, 0},  // Koma <Banker>
{  3319,0x001080,   0, 0},  // Sana <Mail Armor Merchant>
{  3320,0x020000,   0, 0},  // Soran <Banker>
{  3321,0x001080,   0, 0},  // Morgum <Leather Armor Merchant>
{  3322,0x001180,   0, 0},  // Kaja <Guns & Ammunition>
{  3323,0x000880,   0, 0},  // Horthus <Reagents>
{  3324,0x000030,   0, 9},  // Grol'dar <Warlock Trainer>
{  3325,0x000030,   0, 9},  // Mirket <Warlock Trainer>
{  3326,0x000030,   0, 9},  // Zevrost <Warlock Trainer>
{  3327,0x000030,   0, 4},  // Gest <Rogue Trainer>
{  3328,0x000030,   0, 4},  // Ormok <Rogue Trainer>
{  3329,0x000280,   0, 0},  // Kor'jus <Mushroom Vendor>
{  3330,0x001080,   0, 0},  // Muragus <Staff Merchant>
{  3331,0x001080,   0, 0},  // Kareth <Blade Merchant>
{  3332,0x000050, 356, 0},  // Lumak <Fishing Trainer>
{  3333,0x000080,   0, 0},  // Shankys <Fishing Supplies>
{  3334,0x000480,   0, 0},  // Rekkul <Poison Supplies>
{  3335,0x000880,   0, 0},  // Hagrus <Reagents>
{  3342,0x000280,   0, 0},  // Shan'ti <Fruit Vendor>
{  3344,0x000030,   0, 7},  // Kardris Dreamseeker <Shaman Trainer>
{  3345,0x000050, 333, 0},  // Godan <Enchanting Trainer>
{  3346,0x000080,   0, 0},  // Kithas <Enchanting Supplies>
{  3347,0x000050, 171, 0},  // Yelmak <Alchemy Trainer>
{  3348,0x000080,   0, 0},  // Kor'geld <Alchemy Supplies>
{  3349,0x001080,   0, 0},  // Ukra'nor <Staff Merchant>
{  3350,0x000180,   0, 0},  // Asoran <General Goods>
{  3351,0x000880,   0, 0},  // Magenius <Reagents>
{  3352,0x000030,   0, 3},  // Ormak Grimshot <Hunter Trainer>
{  3353,0x000030,   0, 1},  // Grezz Ragefist <Warrior Trainer>
{  3354,0x000030,   0, 1},  // Sorek <Warrior Trainer>
{  3355,0x000050, 164, 0},  // Saru Steelfury <Blacksmithing Trainer>
{  3356,0x001080,   0, 0},  // Sumi <Blacksmithing Supplies>
{  3357,0x000050, 186, 0},  // Makaru <Mining Trainer>
{  3358,0x000080,   0, 0},  // Gorina <Mining Supplies>
{  3359,0x001080,   0, 0},  // Kiro <War Harness Maker>
{  3360,0x001080,   0, 0},  // Koru <Mace & Staves Vendor>
{  3361,0x001080,   0, 0},  // Shoma <Weapon Vendor>
{  3362,0x000080,   0, 0},  // Ogunaro Wolfrunner <Kennel Master>
{  3363,0x000050, 197, 0},  // Magar <Tailoring Trainer>
{  3364,0x000080,   0, 0},  // Borya <Tailoring Supplies>
{  3365,0x000050, 165, 0},  // Karolek <Leatherworking Trainer>
{  3366,0x000080,   0, 0},  // Tamar <Leatherworking Supplies>
{  3367,0x000080,   0, 0},  // Felika <Trade Supplies>
{  3368,0x000280,   0, 0},  // Borstan <Meat Vendor>
{  3369,0x000080,   0, 0},  // Gotri <Bag Vendor>
{  3373,0x000050, 129, 0},  // Arnok <First Aid Trainer>
{  3392,0x001080,   0, 0},  // Prospector Khazgorm <Explorers' League>
{  3399,0x000050, 185, 0},  // Zamja <Cooking Trainer>
{  3400,0x000080,   0, 0},  // Xen'to <Cooking Supplies>
{  3401,0x000030,   0, 4},  // Shenthul <Rogue Trainer>
{  3403,0x000030,   0, 7},  // Sian'tsu <Shaman Trainer>
{  3404,0x000050, 182, 0},  // Jandi <Herbalism Trainer>
{  3405,0x000080,   0, 0},  // Zeal'aya <Herbalism Supplies>
{  3406,0x000030,   0, 3},  // Xor'juul <Hunter Trainer>
{  3407,0x000030,   0, 3},  // Sian'dur <Hunter Trainer>
{  3408,0x000030,   0, 1},  // Zel'mak <Warrior Trainer>
{  3409,0x001180,   0, 0},  // Zendo'jian <Weapon Vendor>
{  3410,0x001180,   0, 0},  // Jin'sora <Bow Merchant>
{  3411,0x000280,   0, 0},  // Denni'ka <Butcher>
{  3413,0x000080,   0, 0},  // Sovik <Engineering Supplies>
{  3443,0x000080,   0, 0},  // Grub
{  3477,0x001080,   0, 0},  // Hraq <Blacksmithing Supplies>
{  3478,0x000050, 164, 0},  // Traugh <Blacksmithing Trainer>
{  3479,0x001080,   0, 0},  // Nargal Deatheye <Weaponsmith>
{  3480,0x000280,   0, 0},  // Moorane Hearthgrain <Baker>
{  3481,0x000180,   0, 0},  // Barg <General Supplies>
{  3482,0x000080,   0, 0},  // Tari'qa <Trade Supplies>
{  3483,0x001080,   0, 0},  // Jahan Hawkwing <Leather & Mail Armor Merchant>
{  3484,0x000050, 197, 0},  // Kil'hala <Tailoring Trainer>
{  3485,0x000080,   0, 0},  // Wrahk <Tailoring Supplies>
{  3486,0x001080,   0, 0},  // Halija Whitestrider <Clothier>
{  3487,0x000080,   0, 0},  // Kalyimah Stormcloud <Bags & Sacks>
{  3488,0x001180,   0, 0},  // Uthrok <Bowyer & Gunsmith>
{  3489,0x000280,   0, 0},  // Zargh <Butcher>
{  3490,0x000c80,   0, 0},  // Hula'mahi <Reagents, Herbs & Poison Supplies>
{  3491,0x001080,   0, 0},  // Ironzar <Weaponsmith>
{  3492,0x001080,   0, 0},  // Vexspindle <Cloth & Leather Armor Merchant>
{  3493,0x001080,   0, 0},  // Grazlix <Armorer & Shieldcrafter>
{  3494,0x000050, 202, 0},  // Tinkerwiz <Engineering Trainer>
{  3495,0x000080,   0, 0},  // Gagsprocket <Engineering Supplies>
{  3496,0x020000,   0, 0},  // Fuzruckle <Banker>
{  3497,0x000280,   0, 0},  // Kilxx <Fisherman>
{  3498,0x000180,   0, 0},  // Jazzik <General Supplies>
{  3499,0x000080,   0, 0},  // Ranik <Trade Supplies>
{  3500,0x000880,   0, 0},  // Tarhus <Reagents>
{  3518,0x000280,   0, 0},  // Thomas Miller <Baker>
{  3522,0x001080,   0, 0},  // Constance Brisboise <Apprentice Clothier>
{  3523,0x000050, 197, 0},  // Bowen Brisboise <Tailoring Trainer>
{  3528,0x001080,   0, 0},  // Pyrewood Armorer
{  3530,0x001080,   0, 0},  // Pyrewood Tailor
{  3532,0x001080,   0, 0},  // Pyrewood Leatherworker
{  3534,0x001080,   0, 0},  // Wallace the Blind <Weaponsmith>
{  3536,0x001080,   0, 0},  // Kris Legace <Freewheeling Tradeswoman>
{  3537,0x001080,   0, 0},  // Zixil <Merchant Supreme>
{  3539,0x001080,   0, 0},  // Ott <Weaponsmith>
{  3540,0x000280,   0, 0},  // Hal McAllister <Fish Merchant>
{  3541,0x000180,   0, 0},  // Sarah Raycroft <General Goods>
{  3542,0x000c80,   0, 0},  // Jaysin Lanyda <Poisons & Reagents>
{  3543,0x001080,   0, 0},  // Robert Aebischer <Superior Armorsmith>
{  3544,0x000280,   0, 0},  // Jason Lemieux <Mushroom Seller>
{  3545,0x000010,   0, 0},  // Claude Erksine <Pet Trainer>
{  3546,0x000280,   0, 0},  // Bernie Heisten <Food & Drink>
{  3547,0x000280,   0, 0},  // Hamlin Atkins <Mushroom Farmer>
{  3548,0x000080,   0, 0},  // Selina Weston <Alchemy & Herbalism Supplies>
{  3549,0x000050, 165, 0},  // Shelene Rhobart <Leatherworking Trainer>
{  3550,0x000080,   0, 0},  // Martine Tramblay <Fishing Supplies>
{  3551,0x000480,   0, 0},  // Patrice Dwyer <Poison Supplies>
{  3552,0x001080,   0, 0},  // Alexandre Lefevre <Leather Armor Merchant>
{  3553,0x001080,   0, 0},  // Sebastian Meloche <Armorer>
{  3554,0x001080,   0, 0},  // Andrea Boynton <Clothier>
{  3555,0x000050, 186, 0},  // Johan Focht <Mining Trainer>
{  3556,0x000080,   0, 0},  // Andrew Hilbert <Trade Supplies>
{  3557,0x000050, 164, 0},  // Guillaume Sorouy <Blacksmithing Trainer>
{  3561,0x000480,   0, 0},  // Kyrai <Poison Supplies>
{  3562,0x000880,   0, 0},  // Alaindia <Reagents>
{  3572,0x000080,   0, 0},  // Zizzek <Fishing Supplies>
{  3577,0x000280,   0, 0},  // Dalaran Brewmaster
{  3578,0x000080,   0, 0},  // Dalaran Miner
{  3587,0x000180,   0, 0},  // Lyrai <General Supplies>
{  3588,0x001080,   0, 0},  // Khardan Proudblade <Weaponsmith>
{  3589,0x001180,   0, 0},  // Keina <Bowyer>
{  3590,0x001080,   0, 0},  // Janna Brightmoon <Clothier>
{  3591,0x001080,   0, 0},  // Freja Nightwing <Leather Armor Merchant>
{  3592,0x001080,   0, 0},  // Andiss <Armorer & Shieldcrafter>
{  3593,0x000030,   0, 1},  // Alyissia <Warrior Trainer>
{  3594,0x000030,   0, 4},  // Frahun Shadewhisper <Rogue Trainer>
{  3595,0x000030,   0, 5},  // Shanda <Priest Trainer>
{  3596,0x000030,   0, 3},  // Ayanna Everstride <Hunter Trainer>
{  3597,0x000030,   0,11},  // Mardant Strongoak <Druid Trainer>
{  3598,0x000030,   0, 1},  // Kyra Windblade <Warrior Trainer>
{  3599,0x000030,   0, 4},  // Jannok Breezesong <Rogue Trainer>
{  3600,0x000030,   0, 5},  // Laurna Morninglight <Priest Trainer>
{  3601,0x000030,   0, 3},  // Dazalar <Hunter Trainer>
{  3602,0x000030,   0,11},  // Kal <Druid Trainer>
{  3603,0x000050, 171, 0},  // Cyndra Kindwhisper <Alchemy Trainer>
{  3604,0x000050, 182, 0},  // Malorne Bladeleaf <Herbalism Trainer>
{  3605,0x000050, 165, 0},  // Nadyia Maneweaver <Leatherworking Trainer>
{  3606,0x000050, 333, 0},  // Alanna Raveneye <Enchanting Trainer>
{  3607,0x000050, 356, 0},  // Androl Oakhand <Fishing Trainer>
{  3608,0x000180,   0, 0},  // Aldia <General Supplies>
{  3609,0x001080,   0, 0},  // Shalomon <Weaponsmith>
{  3610,0x001180,   0, 0},  // Jeena Featherbow <Bowyer>
{  3611,0x001080,   0, 0},  // Brannol Eaglemoon <Clothier>
{  3612,0x001080,   0, 0},  // Sinda <Leather Armor Merchant>
{  3613,0x001080,   0, 0},  // Meri Ironweave <Armorer & Shieldcrafter>
{  3614,0x000080,   0, 0},  // Narret Shadowgrove <Trade Supplies>
{  3615,0x002000,   0, 0},  // Devrak <Wind Rider Master>
{  3620,0x000010,   0, 0},  // Harruk <Pet Trainer>
{  3621,0x000280,   0, 0},  // Kurll <Food & Drink>
{  3622,0x000010,   0, 0},  // Grokor <Pet Trainer>
{  3624,0x000010,   0, 0},  // Zudd <Pet Trainer>
{  3658,0x001080,   0, 0},  // Lizzarik <Weapon Dealer>
{  3682,0x001080,   0, 0},  // Vrang Wildgore <Weaponsmith & Armorcrafter>
{  3683,0x021080,   0, 0},  // Kiknikle <Stylish Clothier>
{  3684,0x001080,   0, 0},  // Pizznukle <Leather Armor Merchant>
{  3685,0x000080,   0, 0},  // Harb Clawhoof <Kodo Mounts>
{  3688,0x000010,   0, 0},  // Reban Freerunner <Pet Trainer>
{  3689,0x000280,   0, 0},  // Laer Stepperunner <Food & Drink>
{  3690,0x000050, 762, 0},  // Kar Stormsinger <Riding Trainer>
{  3698,0x000010,   0, 0},  // Bolyun <Pet Trainer>
{  3700,0x000880,   0, 0},  // Jadenvis Seawatcher <Reagents>
{  3703,0x000050, 165, 0},  // Krulmoo Fullmoon <Leatherworking Trainer>
{  3704,0x000050, 197, 0},  // Mahani <Tailoring Trainer>
{  3705,0x000280,   0, 0},  // Gahroot <Butcher>
{  3706,0x000030,   0, 5},  // Tai'jin <Priest Trainer>
{  3707,0x000030,   0, 5},  // Ken'jai <Priest Trainer>
{  3708,0x000280,   0, 0},  // Gruna <Food & Drink>
{  3779,0x000080,   0, 0},  // Syurana <Trade Supplies>
{  3838,0x002000,   0, 0},  // Vesprystus <Hippogryph Master>
{  3841,0x002000,   0, 0},  // Caylais Moonfeather <Hippogryph Master>
{  3881,0x000280,   0, 0},  // Grimtak <Butcher>
{  3882,0x000280,   0, 0},  // Zlagk <Butcher>
{  3883,0x000280,   0, 0},  // Moodan Sungrain <Baker>
{  3884,0x000280,   0, 0},  // Jhawna Oatwind <Baker>
{  3933,0x000280,   0, 0},  // Hai'zan <Butcher>
{  3934,0x010280,   0, 0},  // Innkeeper Boorand Plainswind <Innkeeper>
{  3935,0x000280,   0, 0},  // Toddrick <Butcher>
{  3937,0x000280,   0, 0},  // Kira Songshine <Traveling Baker>
{  3948,0x000280,   0, 0},  // Honni Goldenoat <Baker>
{  3951,0x001180,   0, 0},  // Bhaldaran Ravenshade <Bowyer>
{  3952,0x001080,   0, 0},  // Aeolynn <Clothier>
{  3953,0x001080,   0, 0},  // Tandaan Lightmane <Leather Armor Merchant>
{  3954,0x000080,   0, 0},  // Dalria <Trade Goods>
{  3955,0x000080,   0, 0},  // Shandrina <Trade Goods>
{  3956,0x000080,   0, 0},  // Harklan Moongrove <Alchemy Supplies>
{  3958,0x000080,   0, 0},  // Lardan <Leatherworking Supplies>
{  3959,0x000280,   0, 0},  // Nantar <Baker>
{  3960,0x000280,   0, 0},  // Ulthaan <Butcher>
{  3961,0x000280,   0, 0},  // Maliynn <Food & Drink Vendor>
{  3962,0x000180,   0, 0},  // Haljan Oakheart <General Goods>
{  3963,0x000030,   0, 3},  // Danlaar Nightstride <Hunter Trainer>
{  3964,0x000050, 171, 0},  // Kylanna <Alchemy Trainer>
{  3965,0x000050, 182, 0},  // Cylania Rootstalker <Herbalism Trainer>
{  3967,0x000050, 165, 0},  // Aayndia Floralwind <Leatherworking Trainer>
{  3969,0x000480,   0, 0},  // Fahran Silentblade <Tools & Supplies>
{  3970,0x000880,   0, 0},  // Llana <Reagent Supplies>
{  4043,0x001080,   0, 0},  // Galthuk <Two-Handed Weapons Merchant>
{  4082,0x000180,   0, 0},  // Grawnal <General Goods>
{  4083,0x000880,   0, 0},  // Jeeda <Apprentice Witch Doctor>
{  4084,0x000380,   0, 0},  // Chylina <General Supplies>
{  4085,0x001080,   0, 0},  // Nizzik <Venture Armor Salesman>
{  4086,0x001080,   0, 0},  // Veenix <Venture Co. Merchant>
{  4087,0x000030,   0, 1},  // Arias'ta Bladesinger <Warrior Trainer>
{  4089,0x000030,   0, 1},  // Sildanair <Warrior Trainer>
{  4090,0x000030,   0, 5},  // Astarii Starseeker <Priest Trainer>
{  4091,0x000030,   0, 5},  // Jandria <Priest Trainer>
{  4092,0x000030,   0, 5},  // Lariia <Priest Trainer>
{  4138,0x000030,   0, 3},  // Jeen'ra Nightrunner <Hunter Trainer>
{  4146,0x000030,   0, 3},  // Jocaste <Hunter Trainer>
{  4155,0x020000,   0, 0},  // Idriana <Banker>
{  4156,0x000050, 356, 0},  // Astaia <Fishing Trainer>
{  4159,0x000050, 197, 0},  // Me'lynn <Tailoring Trainer>
{  4160,0x000050, 171, 0},  // Ainethil <Alchemy Trainer>
{  4163,0x000030,   0, 4},  // Syurna <Rogue Trainer>
{  4164,0x001080,   0, 0},  // Cylania <Night Elf Armorer>
{  4165,0x0000b0,   0, 0},  // Elissa Dumas <Portal Trainer>
{  4167,0x000280,   0, 0},  // Dendrythis <Food & Drink Vendor>
{  4168,0x000080,   0, 0},  // Elynna <Tailoring Supplies>
{  4169,0x000280,   0, 0},  // Jaeana <Meat Vendor>
{  4170,0x000180,   0, 0},  // Ellandrieth <General Goods>
{  4171,0x001080,   0, 0},  // Merelyssa <Blade Merchant>
{  4172,0x001080,   0, 0},  // Anadyia <Robe Vendor>
{  4173,0x001180,   0, 0},  // Landria <Bow Merchant>
{  4175,0x001080,   0, 0},  // Vinasia <Cloth Armor Merchant>
{  4177,0x001080,   0, 0},  // Melea <Mail Armor Merchant>
{  4180,0x001080,   0, 0},  // Ealyshia Dewwhisper <Two Handed Weapon Merchant>
{  4181,0x000280,   0, 0},  // Fyrenna <Food & Drink Vendor>
{  4182,0x000180,   0, 0},  // Dalmond <General Goods>
{  4183,0x001080,   0, 0},  // Naram Longclaw <Weaponsmith>
{  4184,0x001080,   0, 0},  // Geenia Sunshadow <Speciality Dress Maker>
{  4185,0x001080,   0, 0},  // Shaldyn <Clothier>
{  4186,0x001080,   0, 0},  // Mavralyn <Leather Armor & Leatherworking Supplies>
{  4187,0x001080,   0, 0},  // Harlon Thornguard <Armorer & Shieldsmith>
{  4188,0x001080,   0, 0},  // Illyanie <Cloth Armor Merchant>
{  4189,0x000080,   0, 0},  // Valdaron <Tailoring Supplies>
{  4190,0x000280,   0, 0},  // Kyndri <Baker>
{  4191,0x000280,   0, 0},  // Allyndia <Food & Drink Vendor>
{  4192,0x000280,   0, 0},  // Taldan <Drink Vendor>
{  4193,0x000050, 197, 0},  // Grondal Moonbreeze <Tailoring Trainer>
{  4194,0x000080,   0, 0},  // Ullanna <Trade Supplies>
{  4195,0x000280,   0, 0},  // Tiyani <Food & Drink Vendor>
{  4200,0x000280,   0, 0},  // Laird <Fish Vendor>
{  4203,0x001180,   0, 0},  // Ariyell Skyshadow <Weapon Merchant>
{  4204,0x000050, 182, 0},  // Firodren Mooncaller <Herbalism Trainer>
{  4205,0x000030,   0, 3},  // Dorion <Hunter Trainer>
{  4208,0x020000,   0, 0},  // Lairn <Banker>
{  4209,0x020000,   0, 0},  // Garryeth <Banker>
{  4210,0x000050, 185, 0},  // Alegorn <Cooking Trainer>
{  4211,0x000050, 129, 0},  // Dannelor <First Aid Trainer>
{  4212,0x000050, 165, 0},  // Telonis <Leatherworking Trainer>
{  4213,0x000050, 333, 0},  // Taladan <Enchanting Trainer>
{  4214,0x000030,   0, 4},  // Erion Shadewhisper <Rogue Trainer>
{  4215,0x000030,   0, 4},  // Anishar <Rogue Trainer>
{  4216,0x000080,   0, 0},  // Chardryn <Herbalism Supplies>
{  4217,0x0000b0,   0,11},  // Mathrengyl Bearwalker <Druid Trainer>
{  4218,0x000030,   0,11},  // Denatharion <Druid Trainer>
{  4219,0x000030,   0,11},  // Fylerian Nightwing <Druid Trainer>
{  4220,0x000880,   0, 0},  // Cyroen <Reagents>
{  4221,0x000280,   0, 0},  // Talaelar <Fish Vendor>
{  4222,0x000080,   0, 0},  // Voloren <Fishing Supplies>
{  4223,0x000080,   0, 0},  // Fyldan <Cooking Supplies>
{  4225,0x000080,   0, 0},  // Saenorion <Leatherworking Supplies>
{  4226,0x000080,   0, 0},  // Ulthir <Alchemy Supplies>
{  4228,0x000080,   0, 0},  // Vaean <Enchanting Supplies>
{  4229,0x000080,   0, 0},  // Mythrin'dir <Trade Supplies>
{  4230,0x000080,   0, 0},  // Yldan <Bag Merchant>
{  4231,0x001080,   0, 0},  // Kieran <Weapon Merchant>
{  4232,0x001080,   0, 0},  // Glorandiir <Axe Merchant>
{  4233,0x001080,   0, 0},  // Mythidan <Mace & Staff Merchant>
{  4234,0x001080,   0, 0},  // Andrus <Staff Merchant>
{  4235,0x001080,   0, 0},  // Turian <Thrown Weapons Merchant>
{  4236,0x001080,   0, 0},  // Cyridan <Leather Armor Merchant>
{  4240,0x001080,   0, 0},  // Caynrus <Shield Merchant>
{  4241,0x000180,   0, 0},  // Mydrannul <General Goods>
{  4254,0x000050, 186, 0},  // Geofram Bouldertoe <Mining Trainer>
{  4256,0x000080,   0, 0},  // Golnir Bouldertoe <Mining Supplies>
{  4258,0x000050, 164, 0},  // Bengus Deepforge <Blacksmithing Trainer>
{  4259,0x001080,   0, 0},  // Thurgrum Deepforge <Blacksmithing Supplies>
{  4265,0x000080,   0, 0},  // Nyoma <Cooking Supplies>
{  4266,0x000280,   0, 0},  // Danlyia <Food & Drink Vendor>
{  4267,0x002000,   0, 0},  // Daelyshia <Hippogryph Master>
{  4305,0x000280,   0, 0},  // Kriggon Talsone <Fisherman>
{  4307,0x000280,   0, 0},  // Heldan Galesong <Fisherman>
{  4312,0x002000,   0, 0},  // Tharm <Wind Rider Master>
{  4314,0x002000,   0, 0},  // Gorkas <Wind Rider Master>
{  4317,0x002000,   0, 0},  // Nyse <Wind Rider Master>
{  4319,0x002000,   0, 0},  // Thyssiana <Hippogryph Master>
{  4320,0x000010,   0, 0},  // Caelyb <Pet Trainer>
{  4321,0x002000,   0, 0},  // Baldruc <Gryphon Master>
{  4407,0x002000,   0, 0},  // Teloren <Hippogryph Master>
{  4453,0x000080,   0, 0},  // Wizzle Brassbolts
{  4549,0x020000,   0, 0},  // William Montague <Banker>
{  4550,0x020000,   0, 0},  // Ophelia Montague <Banker>
{  4551,0x002000,   0, 0},  // Michael Garrett <Bat Handler>
{  4552,0x000050, 185, 0},  // Eunice Burch <Cooking Trainer>
{  4553,0x000080,   0, 0},  // Ronald Burch <Cooking Supplies>
{  4554,0x000280,   0, 0},  // Tawny Grisette <Mushroom Vendor>
{  4555,0x000380,   0, 0},  // Eleanor Rusk <General Goods>
{  4556,0x001080,   0, 0},  // Gordon Wendham <Weapons Merchant>
{  4557,0x001080,   0, 0},  // Louis Warren <Weapons Merchant>
{  4558,0x001080,   0, 0},  // Lauren Newcomb <Light Armor Merchant>
{  4559,0x001080,   0, 0},  // Timothy Weldon <Heavy Armor Merchant>
{  4560,0x001080,   0, 0},  // Walter Ellingson <Heavy Armor Merchant>
{  4561,0x000080,   0, 0},  // Daniel Bartlett <Trade Supplies>
{  4562,0x000880,   0, 0},  // Thomas Mordan <Reagents>
{  4563,0x000030,   0, 9},  // Kaal Soulreaper <Warlock Trainer>
{  4564,0x000030,   0, 9},  // Luther Pickman <Warlock Trainer>
{  4565,0x000030,   0, 9},  // Richard Kerwin <Warlock Trainer>
{  4566,0x000030,   0, 8},  // Kaelystia Hatebringer <Mage Trainer>
{  4567,0x000030,   0, 8},  // Pierce Shackleton <Mage Trainer>
{  4568,0x000030,   0, 8},  // Anastasia Hartwell <Mage Trainer>
{  4569,0x001080,   0, 0},  // Charles Seaton <Blade Merchant>
{  4570,0x001080,   0, 0},  // Sydney Upton <Staff Merchant>
{  4571,0x000280,   0, 0},  // Morley Bates <Fungus Vendor>
{  4573,0x000050, 356, 0},  // Armand Cromwell <Fishing Trainer>
{  4574,0x000080,   0, 0},  // Lizbeth Cromwell <Fishing Supplies>
{  4575,0x000880,   0, 0},  // Hannah Akeley <Reagents>
{  4576,0x000050, 197, 0},  // Josef Gregorian <Tailoring Trainer>
{  4577,0x000080,   0, 0},  // Millie Gregorian <Tailoring Supplies>
{  4578,0x000050, 197, 0},  // Josephine Lister <Master Shadoweave Tailor>
{  4580,0x001080,   0, 0},  // Lucille Castleton <Robe Vendor>
{  4581,0x000080,   0, 0},  // Salazar Bloch <Book Dealer>
{  4582,0x000030,   0, 4},  // Carolyn Ward <Rogue Trainer>
{  4583,0x000030,   0, 4},  // Miles Dexter <Rogue Trainer>
{  4584,0x000030,   0, 4},  // Gregory Charles <Rogue Trainer>
{  4585,0x000480,   0, 0},  // Ezekiel Graves <Poison Supplies>
{  4587,0x000080,   0, 0},  // Elizabeth Van Talen <Engineering Supplies>
{  4588,0x000050, 165, 0},  // Arthur Moore <Leatherworking Trainer>
{  4589,0x000080,   0, 0},  // Joseph Moore <Leatherworking Supplies>
{  4590,0x000080,   0, 0},  // Jonathan Chambers <Bag Vendor>
{  4591,0x000050, 129, 0},  // Mary Edras <First Aid Trainer>
{  4592,0x001080,   0, 0},  // Nathaniel Steenwick <Thrown Weapons Merchant>
{  4593,0x000030,   0, 1},  // Christoph Walker <Warrior Trainer>
{  4594,0x000030,   0, 1},  // Angela Curthas <Warrior Trainer>
{  4595,0x000030,   0, 1},  // Baltus Fowler <Warrior Trainer>
{  4596,0x000050, 164, 0},  // James Van Brunt <Blacksmithing Trainer>
{  4597,0x001080,   0, 0},  // Samuel Van Brunt <Blacksmithing Supplies>
{  4598,0x000050, 186, 0},  // Brom Killian <Mining Trainer>
{  4599,0x000080,   0, 0},  // Sarah Killian <Mining Supplies>
{  4600,0x001080,   0, 0},  // Geoffrey Hartwell <Weapon Merchant>
{  4601,0x001080,   0, 0},  // Francis Eliot <Weapon Merchant>
{  4602,0x001080,   0, 0},  // Benijah Fenner <Weapon Merchant>
{  4603,0x001180,   0, 0},  // Nicholas Atwood <Gun Merchant>
{  4604,0x001180,   0, 0},  // Abigail Sawyer <Bow Merchant>
{  4606,0x000030,   0, 5},  // Aelthalyste <Priest Trainer>
{  4607,0x000030,   0, 5},  // Father Lankester <Priest Trainer>
{  4608,0x000030,   0, 5},  // Father Lazarus <Priest Trainer>
{  4610,0x000080,   0, 0},  // Algernon <Alchemy Supplies>
{  4611,0x000050, 171, 0},  // Doctor Herbert Halsey <Alchemy Trainer>
{  4614,0x000050, 182, 0},  // Martha Alliestar <Herbalism Trainer>
{  4615,0x000080,   0, 0},  // Katrina Alliestar <Herbalism Supplies>
{  4616,0x000050, 333, 0},  // Lavinia Crowe <Enchanting Trainer>
{  4617,0x000080,   0, 0},  // Thaddeus Webb <Enchanting Supplies>
{  4730,0x000080,   0, 0},  // Lelanai <Saber Handler>
{  4731,0x000080,   0, 0},  // Zachariah Post <Undead Horse Merchant>
{  4732,0x000050, 762, 0},  // Randal Hunter <Riding Trainer>
{  4752,0x000050, 762, 0},  // Kildar <Riding Trainer>
{  4753,0x000050, 762, 0},  // Jartsam <Riding Trainer>
{  4772,0x000050, 762, 0},  // Ultham Ironhorn <Riding Trainer>
{  4773,0x000050, 762, 0},  // Velma Warnam <Riding Trainer>
{  4775,0x000080,   0, 0},  // Felicia Doan <Trade Supplies>
{  4782,0x000280,   0, 0},  // Truk Wildbeard <Bartender>
{  4875,0x000280,   0, 0},  // Turhaw <Butcher>
{  4876,0x000180,   0, 0},  // Jawn Highmesa <General Goods>
{  4877,0x000080,   0, 0},  // Jandia <Trade Supplies>
{  4878,0x000880,   0, 0},  // Montarr <Lorekeeper>
{  4879,0x000280,   0, 0},  // Ogg'marr <Butcher>
{  4883,0x001080,   0, 0},  // Krak <Armorer>
{  4884,0x001080,   0, 0},  // Zulrg <Weaponsmith>
{  4885,0x000080,   0, 0},  // Gregor MacVince <Horse Breeder>
{  4886,0x001080,   0, 0},  // Hans Weston <Armorer & Shieldsmith>
{  4888,0x001080,   0, 0},  // Marie Holdston <Weaponsmith>
{  4889,0x001180,   0, 0},  // Torq Ironblast <Gunsmith>
{  4890,0x001080,   0, 0},  // Piter Verance <Weaponsmith & Armorer>
{  4891,0x000280,   0, 0},  // Dwane Wertle <Chef>
{  4892,0x001180,   0, 0},  // Jensen Farran <Bowyer>
{  4893,0x000280,   0, 0},  // Bartender Lillian <Bartender>
{  4894,0x000280,   0, 0},  // Craig Nollward <Cook>
{  4896,0x000180,   0, 0},  // Charity Mipsy <General Goods>
{  4897,0x000080,   0, 0},  // Helenia Olden <Trade Supplies>
{  4898,0x000050, 182, 0},  // Brant Jasperbloom <Herbalism Trainer>
{  4899,0x000080,   0, 0},  // Uma Bartulm <Herbalism & Alchemy Supplies>
{  4900,0x000050, 171, 0},  // Alchemist Narett <Alchemy Trainer>
{  4954,0x000280,   0, 0},  // Uttnar <Butcher>
{  4981,0x000280,   0, 0},  // Ben Trias <Apprentice of Cheese>
{  5049,0x000080,   0, 0},  // Lyesa Steelbrow <Tabard Vendor>
{  5052,0x000080,   0, 0},  // Edward Remington <Guild Tabard Designer>
{  5099,0x020000,   0, 0},  // Soleil Stonemantle <Banker>
{  5100,0x000080,   0, 0},  // Fillius Fizzlespinner <Trade Supplies>
{  5101,0x000180,   0, 0},  // Bryllia Ironbrand <General Goods>
{  5102,0x001080,   0, 0},  // Dolman Steelfury <Weapon Merchant>
{  5103,0x001080,   0, 0},  // Grenil Steelfury <Weapon Merchant>
{  5106,0x001080,   0, 0},  // Bromiir Ormsen <Heavy Armor Merchant>
{  5107,0x001080,   0, 0},  // Mangorn Flinthammer <Heavy Armor Merchant>
{  5108,0x001080,   0, 0},  // Raena Flinthammer <Light Armor Merchant>
{  5109,0x000280,   0, 0},  // Myra Tyrngaarde <Bread Vendor>
{  5110,0x000880,   0, 0},  // Barim Jurgenstaad <Reagents>
{  5111,0x010280,   0, 0},  // Innkeeper Firebrew <Innkeeper>
{  5112,0x000280,   0, 0},  // Gwenna Firebrew <Barmaid>
{  5113,0x000030,   0, 1},  // Kelv Sternhammer <Warrior Trainer>
{  5114,0x000030,   0, 1},  // Bilban Tosslespanner <Warrior Trainer>
{  5115,0x000030,   0, 3},  // Daera Brightspear <Hunter Trainer>
{  5116,0x000030,   0, 3},  // Olmin Burningbeard <Hunter Trainer>
{  5117,0x000030,   0, 3},  // Regnus Thundergranite <Hunter Trainer>
{  5119,0x001080,   0, 0},  // Hegnar Swiftaxe <Axe Merchant>
{  5120,0x001080,   0, 0},  // Brenwyn Wintersteel <Blade Merchant>
{  5121,0x001080,   0, 0},  // Kelomir Ironhand <Maces & Staves>
{  5122,0x001180,   0, 0},  // Skolmin Goldfury <Bow Merchant>
{  5123,0x001180,   0, 0},  // Bretta Goldfury <Gun Merchant>
{  5124,0x000280,   0, 0},  // Sognar Cliffbeard <Meat Vendor>
{  5125,0x001080,   0, 0},  // Dolkin Craghelm <Mail Armor Merchant>
{  5126,0x001080,   0, 0},  // Olthran Craghelm <Heavy Armor Merchant>
{  5127,0x000050, 165, 0},  // Fimble Finespindle <Leatherworking Trainer>
{  5128,0x000080,   0, 0},  // Bombus Finespindle <Leatherworking Supplies>
{  5129,0x001080,   0, 0},  // Lissyphus Finespindle <Light Armor Merchant>
{  5132,0x000080,   0, 0},  // Pithwick <Bag Vendor>
{  5133,0x001080,   0, 0},  // Harick Boulderdrum <Wands Merchant>
{  5137,0x000050, 182, 0},  // Reyna Stonebranch <Herbalism Trainer>
{  5138,0x000080,   0, 0},  // Gwina Stonebranch <Herbalism Supplies>
{  5140,0x000280,   0, 0},  // Edris Barleybeard <Barmaid>
{  5141,0x000030,   0, 5},  // Theodrus Frostbeard <Priest Trainer>
{  5142,0x000030,   0, 5},  // Braenna Flintcrag <Priest Trainer>
{  5143,0x000030,   0, 5},  // Toldren Deepiron <Priest Trainer>
{  5144,0x000030,   0, 8},  // Bink <Mage Trainer>
{  5145,0x000030,   0, 8},  // Juli Stormkettle <Mage Trainer>
{  5146,0x000030,   0, 8},  // Nittlebur Sparkfizzle <Mage Trainer>
{  5147,0x000030,   0, 2},  // Valgar Highforge <Paladin Trainer>
{  5148,0x000030,   0, 2},  // Beldruk Doombrow <Paladin Trainer>
{  5149,0x000030,   0, 2},  // Brandur Ironhammer <Paladin Trainer>
{  5150,0x000050, 129, 0},  // Nissa Firestone <First Aid Trainer>
{  5151,0x000880,   0, 0},  // Ginny Longberry <Reagents>
{  5152,0x001080,   0, 0},  // Bingus <Weapon Merchant>
{  5153,0x000050, 197, 0},  // Jormund Stonebrow <Tailoring Trainer>
{  5154,0x000080,   0, 0},  // Poranna Snowbraid <Tailoring Supplies>
{  5155,0x001080,   0, 0},  // Ingrys Stonebrow <Cloth Armor Merchant>
{  5156,0x001080,   0, 0},  // Maeva Snowbraid <Robe Merchant>
{  5157,0x000050, 333, 0},  // Gimble Thistlefuzz <Enchanting Trainer>
{  5158,0x000080,   0, 0},  // Tilli Thistlefuzz <Enchanting Supplies>
{  5159,0x000050, 185, 0},  // Daryl Riknussun <Cooking Trainer>
{  5160,0x000080,   0, 0},  // Emrul Riknussun <Cooking Supplies>
{  5161,0x000050, 356, 0},  // Grimnur Stonebrand <Fishing Trainer>
{  5162,0x000080,   0, 0},  // Tansy Puddlefizz <Fishing Supplies>
{  5163,0x000080,   0, 0},  // Burbik Gearspanner <Trade Supplies>
{  5164,0x000050, 164, 0},  // Grumnus Steelshaper <Armorsmith Trainer>
{  5165,0x000030,   0, 4},  // Hulfdan Blackbeard <Rogue Trainer>
{  5166,0x000030,   0, 4},  // Ormyr Flinteye <Rogue Trainer>
{  5167,0x000030,   0, 4},  // Fenthwick <Rogue Trainer>
{  5169,0x000480,   0, 0},  // Tynnus Venomsprout <Shady Dealer>
{  5170,0x001080,   0, 0},  // Hjoldir Stoneblade <Blade Merchant>
{  5171,0x000030,   0, 9},  // Thistleheart <Warlock Trainer>
{  5172,0x000030,   0, 9},  // Briarthorn <Warlock Trainer>
{  5173,0x0000b0,   0, 9},  // Alexander Calder <Warlock Trainer>
{  5174,0x000050, 202, 0},  // Springspindle Fizzlegear <Engineering Trainer>
{  5175,0x000080,   0, 0},  // Gearcutter Cogspinner <Engineering Supplies>
{  5177,0x000050, 171, 0},  // Tally Berryfizz <Alchemy Trainer>
{  5178,0x000080,   0, 0},  // Soolie Berryfizz <Alchemy Supplies>
{  5188,0x000080,   0, 0},  // Garyl <Tabard Vendor>
{  5189,0x000080,   0, 0},  // Thrumn <Tabard Vendor>
{  5190,0x000080,   0, 0},  // Merill Pleasance <Tabard Vendor>
{  5191,0x000080,   0, 0},  // Shalumon <Tabard Vendor>
{  5193,0x000080,   0, 0},  // Rebecca Laughlin <Tabard Designer>
{  5392,0x000050, 186, 0},  // Yarr Hammerstone <Mining Trainer>
{  5411,0x001080,   0, 0},  // Krinkle Goodsteel <Blacksmithing Supplies>
{  5479,0x000030,   0, 1},  // Wu Shen <Warrior Trainer>
{  5480,0x000030,   0, 1},  // Ilsa Corbin <Warrior Trainer>
{  5482,0x000050, 185, 0},  // Stephen Ryback <Cooking Trainer>
{  5483,0x000080,   0, 0},  // Erika Tate <Cooking Supplies>
{  5484,0x000030,   0, 5},  // Brother Benjamin <Priest Trainer>
{  5489,0x000030,   0, 5},  // Brother Joshua <Priest Trainer>
{  5491,0x000030,   0, 2},  // Arthur the Faithful <Paladin Trainer>
{  5492,0x000030,   0, 2},  // Katherine the Pure <Paladin Trainer>
{  5493,0x000050, 356, 0},  // Arnold Leland <Fishing Trainer>
{  5494,0x000080,   0, 0},  // Catherine Leland <Fishing Supplies>
{  5495,0x000030,   0, 9},  // Ursula Deline <Warlock Trainer>
{  5496,0x000030,   0, 9},  // Sandahl <Warlock Trainer>
{  5497,0x000030,   0, 8},  // Jennea Cannon <Mage Trainer>
{  5498,0x000030,   0, 8},  // Elsharin <Mage Trainer>
{  5499,0x000050, 171, 0},  // Lilyssia Nightbreeze <Alchemy Trainer>
{  5501,0x000030,   0, 3},  // Kaerbrus <Hunter Trainer>
{  5502,0x000050, 182, 0},  // Shylamiir <Herbalism Trainer>
{  5503,0x000080,   0, 0},  // Eldraeith <Herbalism Supplies>
{  5504,0x000030,   0,11},  // Sheldras Moontree <Druid Trainer>
{  5505,0x000030,   0,11},  // Theridran <Druid Trainer>
{  5506,0x000030,   0,11},  // Maldryn <Druid Trainer>
{  5508,0x001080,   0, 0},  // Strumner Flintheel <Armorer>
{  5509,0x001080,   0, 0},  // Kathrum Axehand <Axe Merchant>
{  5510,0x001180,   0, 0},  // Thulman Flintcrag <Guns Vendor>
{  5511,0x000050, 164, 0},  // Therum Deepforge <Blacksmithing Trainer>
{  5512,0x001080,   0, 0},  // Kaita Deepforge <Blacksmithing Supplies>
{  5513,0x000050, 186, 0},  // Gelman Stonehand <Mining Trainer>
{  5514,0x000080,   0, 0},  // Brooke Stonebraid <Mining Supplies>
{  5515,0x000030,   0, 3},  // Einris Brightspear <Hunter Trainer>
{  5516,0x000030,   0, 3},  // Ulfir Ironbeard <Hunter Trainer>
{  5517,0x000030,   0, 3},  // Thorfin Stoneshield <Hunter Trainer>
{  5518,0x000050, 202, 0},  // Lilliam Sparkspindle <Engineering Trainer>
{  5519,0x000080,   0, 0},  // Billibub Cogspinner <Engineering Supplies>
{  5564,0x000050, 165, 0},  // Simon Tanner <Leatherworking Trainer>
{  5565,0x000080,   0, 0},  // Jillian Tanner <Leatherworking Supplies>
{  5566,0x000050, 182, 0},  // Tannysa <Herbalism Trainer>
{  5570,0x000280,   0, 0},  // Bruuk Barleybeard <Bartender>
{  5594,0x000080,   0, 0},  // Alchemist Pestlezugg <Alchemy Supplies>
{  5611,0x000280,   0, 0},  // Barkeep Morag
{  5612,0x000030,   0, 9},  // Gimrizz Shadowcog <Warlock Trainer>
{  5620,0x000280,   0, 0},  // Bartender Wental <Food & Drink>
{  5688,0x010280,   0, 0},  // Innkeeper Renee <Innkeeper>
{  5690,0x000050, 356, 0},  // Clyde Kellen <Fishing Trainer>
{  5695,0x000050, 333, 0},  // Vance Undergloom <Enchanting Trainer>
{  5698,0x000080,   0, 0},  // Joanna Whitehall
{  5748,0x000080,   0, 0},  // Killian Sanatha <Fisherman>
{  5754,0x001080,   0, 0},  // Zane Bradford <Wand Vendor>
{  5757,0x000080,   0, 0},  // Lilly <Enchanting Supplies>
{  5758,0x000080,   0, 0},  // Leo Sarn <Enchanting Supplies>
{  5759,0x000050, 129, 0},  // Nurse Neela <First Aid Trainer>
{  5783,0x000080,   0, 0},  // Kalldan Felmoon <Specialist Leatherworking Supplies>
{  5784,0x000050, 165, 0},  // Waldor <Leatherworking Trainer>
{  5812,0x001080,   0, 0},  // Tumi <Heavy Armor Merchant>
{  5814,0x010280,   0, 0},  // Innkeeper Thulbek <Innkeeper>
{  5816,0x001080,   0, 0},  // Katis <Wand Merchant>
{  5817,0x000080,   0, 0},  // Shimra <Trade Supplies>
{  5819,0x001080,   0, 0},  // Mirelle Tremayne <Heavy Armor Merchant>
{  5820,0x001080,   0, 0},  // Gillian Moore <Leather Armor Merchant>
{  5821,0x001080,   0, 0},  // Sheldon Von Croy <Cloth Armor Merchant>
{  5848,0x000080,   0, 0},  // Malgin Barleybrew <Bael'dun Morale Officer>
{  5870,0x000280,   0, 0},  // Krond <Butcher>
{  5871,0x000280,   0, 0},  // Larhka <Beverage Merchant>
{  5880,0x000030,   0, 8},  // Un'Thuwa <Mage Trainer>
{  5882,0x000030,   0, 8},  // Pephredo <Mage Trainer>
{  5883,0x000030,   0, 8},  // Enyo <Mage Trainer>
{  5884,0x000030,   0, 8},  // Mai'ah <Mage Trainer>
{  5885,0x000030,   0, 8},  // Deino <Mage Trainer>
{  5886,0x000280,   0, 0},  // Gwyn Farrow <Mushroom Merchant>
{  5938,0x000050, 356, 0},  // Uthan Stillwater <Fishing Trainer>
{  5939,0x000050, 129, 0},  // Vira Younghoof <First Aid Trainer>
{  5940,0x000080,   0, 0},  // Harn Longcast <Fishing Supplies>
{  5941,0x000050, 356, 0},  // Lau'Tiki <Fishing Trainer>
{  5942,0x000080,   0, 0},  // Zansoa <Fishing Supplies>
{  5943,0x000050, 129, 0},  // Rawrk <First Aid Trainer>
{  5944,0x000080,   0, 0},  // Yonada <Tailoring & Leatherworking Supplies>
{  5957,0x000030,   0, 0},  // Birgitte Cranston <Portal Trainer>
{  5958,0x000030,   0, 0},  // Thuul <Portal Trainer>
{  5994,0x000030,   0, 5},  // Zayus <High Priest>
{  6014,0x000030,   0, 5},  // X'yera <Priest Trainer>
{  6018,0x000030,   0, 5},  // Ur'kyo <Priest Trainer>
{  6026,0x002000,   0, 0},  // Breyk <Wind Rider Master>
{  6028,0x001180,   0, 0},  // Burkrum <Heavy Armor Merchant>
{  6091,0x000280,   0, 0},  // Dellylah <Food & Drink Vendor>
{  6094,0x000050, 129, 0},  // Byancie <First Aid Trainer>
{  6272,0x010280,   0, 0},  // Innkeeper Janene <Innkeeper>
{  6286,0x000050, 185, 0},  // Zarrin <Cook>
{  6287,0x000050, 393, 0},  // Radnaal Maneweaver <Skinning Trainer>
{  6288,0x000050, 393, 0},  // Jayla <Skinning Trainer>
{  6289,0x000050, 393, 0},  // Rand Rhobart <Skinning Trainer>
{  6290,0x000050, 393, 0},  // Yonn Deepcut <Skinning Trainer>
{  6291,0x000050, 393, 0},  // Balthus Stoneflayer <Skinning Trainer>
{  6292,0x000050, 393, 0},  // Eladriel <Skinning Trainer>
{  6295,0x000050, 393, 0},  // Wilma Ranthal <Skinning Trainer>
{  6297,0x000050, 186, 0},  // Kurdram Stonehammer <Mining Trainer>
{  6298,0x000080,   0, 0},  // Thelgrum Stonehammer <Mining Supplies>
{  6299,0x000050, 164, 0},  // Delfrum Flintbeard <Blacksmithing Trainer>
{  6300,0x001080,   0, 0},  // Elisa Steelhand <Blacksmithing Supplies>
{  6301,0x000080,   0, 0},  // Gorbold Steelhand <Trade Supplies>
{  6306,0x000050, 393, 0},  // Helene Peltskinner <Skinning Trainer>
{  6367,0x000080,   0, 0},  // Donni Anthania <Crazy Cat Lady>
{  6387,0x000050, 393, 0},  // Dranh <Skinning Trainer>
{  6495,0x000280,   0, 0},  // Riznek <Drink Vendor>
{  6496,0x000280,   0, 0},  // Brivelthwerp <Ice Cream Vendor>
{  6548,0x000080,   0, 0},  // Magus Tirth
{  6567,0x000080,   0, 0},  // Ghok'kah <Tailoring Supplies>
{  6568,0x000080,   0, 0},  // Vizzklick <Tailoring Supplies>
{  6574,0x000080,   0, 0},  // Jun'ha <Tailoring Supplies>
{  6576,0x000080,   0, 0},  // Brienna Starglow <Tailoring Supplies>
{  6706,0x002000,   0, 0},  // Baritanas Skyriver <Hippogryph Master>
{  6707,0x000030,   0, 4},  // Fahrad <Grand Master Rogue>
{  6726,0x002000,   0, 0},  // Thalon <Wind Rider Master>
{  6727,0x010280,   0, 0},  // Innkeeper Brianna <Innkeeper>
{  6730,0x000080,   0, 0},  // Jinky Twizzlefixxit <Engineering Supplies>
{  6731,0x000080,   0, 0},  // Harlown Darkweave <Leatherworking Supplies>
{  6734,0x010280,   0, 0},  // Innkeeper Hearthstove <Innkeeper>
{  6735,0x010280,   0, 0},  // Innkeeper Saelienne <Innkeeper>
{  6736,0x010280,   0, 0},  // Innkeeper Keldamyr <Innkeeper>
{  6737,0x010080,   0, 0},  // Innkeeper Shaussiy <Innkeeper>
{  6738,0x010280,   0, 0},  // Innkeeper Kimlya <Innkeeper>
{  6739,0x010280,   0, 0},  // Innkeeper Bates <Innkeeper>
{  6740,0x010280,   0, 0},  // Innkeeper Allison <Innkeeper>
{  6741,0x010280,   0, 0},  // Innkeeper Norman <Innkeeper>
{  6746,0x010280,   0, 0},  // Innkeeper Pala <Innkeeper>
{  6747,0x010280,   0, 0},  // Innkeeper Kauth <Innkeeper>
{  6777,0x000080,   0, 0},  // Zan Shivsproket <Speciality Engineer>
{  6779,0x000480,   0, 0},  // Smudge Thunderwood <Poison Supplies>
{  6790,0x010280,   0, 0},  // Innkeeper Trelayne <Innkeeper>
{  6791,0x010280,   0, 0},  // Innkeeper Wiley <Innkeeper>
{  6807,0x010280,   0, 0},  // Innkeeper Skindle <Innkeeper>
{  6928,0x010280,   0, 0},  // Innkeeper Grosk <Innkeeper>
{  6929,0x010280,   0, 0},  // Innkeeper Gryshka <Innkeeper>
{  6930,0x010280,   0, 0},  // Innkeeper Karakul <Innkeeper>
{  7087,0x000050, 393, 0},  // Killian Hagey <Skinning Trainer>
{  7088,0x000050, 393, 0},  // Thuwd <Skinning Trainer>
{  7089,0x000050, 393, 0},  // Mooranta <Skinning Trainer>
{  7230,0x000050, 164, 0},  // Shayis Steelfury <Armorsmith Trainer>
{  7231,0x000010,   0, 0},  // Kelgruk Bloodaxe <Weaponsmith Trainer>
{  7232,0x000050, 164, 0},  // Borgus Steelhand <Weaponsmith Trainer>
{  7311,0x000030,   0, 8},  // Uthel'nay <Mage Trainer>
{  7312,0x000030,   0, 8},  // Dink <Mage Trainer>
{  7315,0x000030,   0, 1},  // Darnath Bladesinger <Warrior Trainer>
{  7406,0x000050, 202, 0},  // Oglethorpe Obnoticus <Gnomish Engineering Trainer>
{  7485,0x000280,   0, 0},  // Nargatt <Food & Drink>
{  7564,0x000080,   0, 0},  // Marin Noggenfogger
{  7683,0x000080,   0, 0},  // Alessandro Luca <Blue Moon Odds & Ends>
{  7714,0x010280,   0, 0},  // Innkeeper Byula <Innkeeper>
{  7731,0x010280,   0, 0},  // Innkeeper Jayka <Innkeeper>
{  7733,0x010280,   0, 0},  // Innkeeper Fizzgrimble <Innkeeper>
{  7736,0x010280,   0, 0},  // Innkeeper Shyria <Innkeeper>
{  7737,0x010280,   0, 0},  // Innkeeper Greul <Innkeeper>
{  7744,0x010280,   0, 0},  // Innkeeper Thulfram <Innkeeper>
{  7772,0x000080,   0, 0},  // Kalin Windflight
{  7775,0x000080,   0, 0},  // Gregan Brewspewer
{  7799,0x020000,   0, 0},  // Gimblethorn <Banker>
{  7823,0x002000,   0, 0},  // Bera Stonehammer <Gryphon Master>
{  7824,0x002000,   0, 0},  // Bulkrek Ragefist <Wind Rider Master>
{  7852,0x001080,   0, 0},  // Pratt McGrubben <Leatherworking Supplies>
{  7854,0x000080,   0, 0},  // Jangdor Swiftstrider <Leatherworking Supplies>
{  7866,0x000050, 165, 0},  // Peter Galen <Dragonscale Leatherworking Trainer>
{  7867,0x000050, 165, 0},  // Thorkaf Dragoneye <Dragonscale Leatherworking Trainer>
{  7868,0x000050, 165, 0},  // Sarah Tanner <Elemental Leatherworking Trainer>
{  7869,0x000050, 165, 0},  // Brumn Winterhoof <Elemental Leatherworking Trainer>
{  7870,0x000050, 165, 0},  // Caryssia Moonhunter <Tribal Leatherworking Trainer>
{  7871,0x000050, 165, 0},  // Se'Jib <Tribal Leatherworking Trainer>
{  7879,0x000080,   0, 0},  // Quintis Jonespyre
{  7940,0x000080,   0, 0},  // Darnall <Tailoring Supplies>
{  7941,0x000280,   0, 0},  // Mardrack Greenwell <Food & Drink>
{  7942,0x000180,   0, 0},  // Faralorn <General Supplies>
{  7943,0x000280,   0, 0},  // Harklane <Fish Vendor>
{  7944,0x000050, 202, 0},  // Tinkmaster Overspark <Gnomish Engineering Trainer>
{  7945,0x000080,   0, 0},  // Savanne <Fishing Supplies>
{  7946,0x000050, 356, 0},  // Brannock <Fishing Trainer>
{  7947,0x000080,   0, 0},  // Vivianna <Trade Supplies>
{  7948,0x000050, 171, 0},  // Kylanna Windwhisper <Alchemy Trainer>
{  7949,0x000050, 333, 0},  // Xylinnia Starshine <Enchanting Trainer>
{  7952,0x000080,   0, 0},  // Zjolnir <Raptor Handler>
{  7953,0x000050, 762, 0},  // Xar'Ti <Riding Trainer>
{  7954,0x000050,   0, 0},  // Binjy Featherwhistle <Mechanostrider Pilot>
{  7955,0x000080,   0, 0},  // Milli Featherwhistle <Mechanostrider Merchant>
{  7976,0x001180,   0, 0},  // Thalgus Thunderfist <Weapon Merchant>
{  7978,0x000280,   0, 0},  // Bimble Longberry <Fruit Vendor>
{  8018,0x002000,   0, 0},  // Guthrum Thunderfist <Gryphon Master>
{  8019,0x002000,   0, 0},  // Fyldren Moonfeather <Hippogryph Master>
{  8020,0x002000,   0, 0},  // Shyn <Wind Rider Master>
{  8119,0x020000,   0, 0},  // Zikkel <Banker>
{  8123,0x020000,   0, 0},  // Rickle Goldgrubber <Banker>
{  8124,0x020000,   0, 0},  // Qizzik <Banker>
{  8125,0x000280,   0, 0},  // Dirge Quikcleave <Butcher>
{  8126,0x000050, 202, 0},  // Nixx Sprocketspring <Goblin Engineering Trainer>
{  8128,0x000050, 186, 0},  // Pikkle <Mining Trainer>
{  8129,0x001080,   0, 0},  // Wrinkle Goodsteel <Superior Armorer>
{  8131,0x001180,   0, 0},  // Blizrik Buckshot <Gunsmith>
{  8137,0x000280,   0, 0},  // Gikkix <Fisherman>
{  8139,0x000180,   0, 0},  // Jabbey <General Goods>
{  8140,0x000030,   0, 2},  // Brother Karman <Paladin Trainer>
{  8141,0x000030,   0, 1},  // Captain Evencane <Warrior Trainer>
{  8142,0x000030,   0,11},  // Jannos Lighthoof <Druid Trainer>
{  8143,0x000280,   0, 0},  // Loorana <Food & Drink>
{  8144,0x000050, 393, 0},  // Kulleg Stonehorn <Skinning Trainer>
{  8145,0x000080,   0, 0},  // Sheendra Tallgrass <Trade Supplies>
{  8146,0x000050, 182, 0},  // Ruw <Herbalism Trainer>
{  8150,0x000280,   0, 0},  // Janet Hommers <Food & Drink>
{  8152,0x000280,   0, 0},  // Harnor <Food & Drink>
{  8153,0x000050, 165, 0},  // Narv Hidecrafter <Leatherworking Trainer>
{  8157,0x000080,   0, 0},  // Logannas <Alchemy Supplies>
{  8158,0x000080,   0, 0},  // Bronk <Alchemy Supplies>
{  8159,0x001080,   0, 0},  // Worb Strongstitch <Light Armor Merchant>
{  8160,0x000080,   0, 0},  // Nioma <Leatherworking Supplies>
{  8161,0x001080,   0, 0},  // Harggan <Blacksmithing Supplies>
{  8176,0x001080,   0, 0},  // Gharash <Blacksmithing Supplies>
{  8177,0x000080,   0, 0},  // Rartar <Alchemy Supplies>
{  8178,0x000080,   0, 0},  // Nina Lightbrew <Alchemy Supplies>
{  8305,0x000080,   0, 0},  // Kixxle <Potions & Herbs>
{  8306,0x000050, 185, 0},  // Duhng <Cook>
{  8307,0x000280,   0, 0},  // Tarban Hearthgrain <Baker>
{  8308,0x000030,   0, 3},  // Alenndaar Lapidaar <Hunter Trainer>
{  8356,0x020000,   0, 0},  // Chesmu <Banker>
{  8357,0x020000,   0, 0},  // Atepa <Banker>
{  8358,0x001080,   0, 0},  // Hewa <Cloth Armor Merchant>
{  8359,0x001080,   0, 0},  // Ahanu <Leather Armor Merchant>
{  8360,0x001080,   0, 0},  // Elki <Mail Armor Merchant>
{  8361,0x000880,   0, 0},  // Chepi <Reagents>
{  8362,0x000180,   0, 0},  // Kuruk <General Goods>
{  8363,0x000080,   0, 0},  // Shadi Mistrunner <Trade Supplies>
{  8364,0x000080,   0, 0},  // Pakwa <Bag Vendor>
{  8398,0x001080,   0, 0},  // Ohanko <Two Handed Weapon Merchant>
{  8401,0x000080,   0, 0},  // Halpa <Prairie Dog Vendor>
{  8403,0x000080,   0, 0},  // Jeremiah Payson <Cockroach Vendor>
{  8404,0x000080,   0, 0},  // Xan'tish <Snake Vendor>
{  8508,0x000080,   0, 0},  // Gretta Ganter <Fishing Supplies>
{  8609,0x002000,   0, 0},  // Alexandra Constantine <Gryphon Master>
{  8610,0x002000,   0, 0},  // Kroum <Wind Rider Master>
{  8661,0x200000,   0, 0},  // Auctioneer Beardo
{  8665,0x000080,   0, 0},  // Shylenai <Owl Trainer>
{  8666,0x000080,   0, 0},  // Lil Timmy <Boy with kittens>
{  8669,0x200000,   0, 0},  // Auctioneer Tolon
{  8670,0x200000,   0, 0},  // Auctioneer Chilton
{  8671,0x200000,   0, 0},  // Auctioneer Buckler
{  8672,0x200000,   0, 0},  // Auctioneer Leeka
{  8673,0x200000,   0, 0},  // Auctioneer Thathung
{  8674,0x200000,   0, 0},  // Auctioneer Stampi
{  8678,0x000080,   0, 0},  // Jubie Gadgetspring <Engineering Supplies>
{  8679,0x000080,   0, 0},  // Knaz Blunderflame <Engineering Supplies>
{  8681,0x000080,   0, 0},  // Outfitter Eric <Speciality Tailoring Supplies>
{  8719,0x200000,   0, 0},  // Auctioneer Fitch
{  8720,0x200000,   0, 0},  // Auctioneer Redmuse
{  8721,0x200000,   0, 0},  // Auctioneer Epitwee
{  8722,0x200000,   0, 0},  // Auctioneer Gullem
{  8723,0x200000,   0, 0},  // Auctioneer Golothas
{  8724,0x200000,   0, 0},  // Auctioneer Wabang
{  8736,0x000050, 202, 0},  // Buzzek Bracketswing <Engineering Trainer>
{  8738,0x000050, 202, 0},  // Vazario Linkgrease <Goblin Engineering Trainer>
{  8878,0x001080,   0, 0},  // Muuran <Superior Macecrafter>
{  8931,0x010280,   0, 0},  // Innkeeper Heather <Innkeeper>
{  8934,0x000080,   0, 0},  // Christopher Hewen <Trade Supplies>
{  9087,0x000080,   0, 0},  // Bashana Runetotem
{  9099,0x000280,   0, 0},  // Sraaz <Pie Vendor>
{  9179,0x001080,   0, 0},  // Jazzrik <Blacksmithing Supplies>
{  9356,0x010280,   0, 0},  // Innkeeper Shul'kar <Innkeeper>
{  9465,0x000030,   0,11},  // Golhine the Hooded <Druid Trainer>
{  9499,0x000080,   0, 0},  // Plugger Spazzring
{  9501,0x010280,   0, 0},  // Innkeeper Adegwa <Innkeeper>
{  9544,0x001080,   0, 0},  // Yuka Screwspigot
{  9548,0x001180,   0, 0},  // Cawind Trueaim <Gunsmith & Bowyer>
{  9549,0x001180,   0, 0},  // Borand <Bowyer>
{  9551,0x001180,   0, 0},  // Starn <Gunsmith & Bowyer>
{  9552,0x001180,   0, 0},  // Zanara <Bowyer>
{  9553,0x001180,   0, 0},  // Nadia Vernon <Bowyer>
{  9555,0x001180,   0, 0},  // Mu'uta <Bowyer>
{  9584,0x000050, 197, 0},  // Jalane Ayrole <Master Shadoweave Tailor>
{  9636,0x000080,   0, 0},  // Kireena <Trade Goods>
{  9676,0x000080,   0, 0},  // Tink Sprocketwhistle <Engineering Supplies>
{  9856,0x200000,   0, 0},  // Auctioneer Grimful
{  9857,0x200000,   0, 0},  // Auctioneer Grizzlin
{  9858,0x200000,   0, 0},  // Auctioneer Kresky
{  9859,0x200000,   0, 0},  // Auctioneer Lympkin
{ 10086,0x000010,   0, 0},  // Hesuwa Thunderhorn <Pet Trainer>
{ 10088,0x000010,   0, 0},  // Xao'tsu <Pet Trainer>
{ 10089,0x000030,   0, 0},  // Silvaria <Pet Trainer>
{ 10090,0x000030,   0, 0},  // Belia Thundergranite <Pet Trainer>
{ 10118,0x000080,   0, 0},  // Nessa Shadowsong <Fishing Supplies>
{ 10216,0x000080,   0, 0},  // Gubber Blump
{ 10293,0x001080,   0, 0},  // Dulciea Frostmoon <Cloth Armor Merchant>
{ 10361,0x001080,   0, 0},  // Gruul Darkblade <Weaponsmith>
{ 10369,0x001180,   0, 0},  // Trayexir <Weapon Merchant>
{ 10378,0x002000,   0, 0},  // Omusa Thunderhorn <Wind Rider Master>
{ 10379,0x001080,   0, 0},  // Altsoba Ragetotem <Weapon Merchant>
{ 10380,0x001080,   0, 0},  // Sanuye Runetotem <Leather Armor Merchant>
{ 10583,0x002000,   0, 0},  // Gryfe <Flight Master>
{ 10618,0x000080,   0, 0},  // Rivern Frostwind <Wintersaber Trainer>
{ 10856,0x001080,   0, 0},  // Argent Quartermaster Hasana <The Argent Dawn>
{ 10857,0x001080,   0, 0},  // Argent Quartermaster Lightspark <The Argent Dawn>
{ 10897,0x002000,   0, 0},  // Sindrayl <Hippogryph Master>
{ 10930,0x000030,   0, 3},  // Dargh Trueaim <Hunter Trainer>
{ 10993,0x000050, 202, 0},  // Twizwick Sprocketgrind <Engineering Trainer>
{ 11017,0x000050, 202, 0},  // Roxxik <Engineering Trainer>
{ 11025,0x000050, 202, 0},  // Mukdrak <Engineering Trainer>
{ 11031,0x000050, 202, 0},  // Franklin Lloyd <Engineering Trainer>
{ 11037,0x000050, 202, 0},  // Jenna Lemkenilli <Engineering Trainer>
{ 11038,0x000380,   0, 0},  // Caretaker Alen <The Argent Dawn>
{ 11052,0x000050, 197, 0},  // Timothy Worthington <Tailoring Trainer>
{ 11056,0x000080,   0, 0},  // Alchemist Arbington
{ 11057,0x000080,   0, 0},  // Apothecary Dithers
{ 11072,0x000050, 333, 0},  // Kitta Firewind <Enchanting Trainer>
{ 11073,0x000050, 333, 0},  // Annora <Enchanting Trainer>
{ 11074,0x000050, 333, 0},  // Hgarth <Enchanting Trainer>
{ 11097,0x000050, 165, 0},  // Drakk Stonehand <Leatherworking Trainer>
{ 11098,0x000050, 165, 0},  // Hahrana Ironhide <Leatherworking Trainer>
{ 11103,0x010280,   0, 0},  // Innkeeper Lyshaerya <Innkeeper>
{ 11106,0x010280,   0, 0},  // Innkeeper Sikewa <Innkeeper>
{ 11116,0x010280,   0, 0},  // Innkeeper Abeqwa <Innkeeper>
{ 11118,0x010280,   0, 0},  // Innkeeper Vizzie <Innkeeper>
{ 11137,0x001080,   0, 0},  // Xai'ander <Weaponsmith>
{ 11138,0x002000,   0, 0},  // Maethrya <Hippogryph Master>
{ 11139,0x002000,   0, 0},  // Yugrek <Wind Rider Master>
{ 11146,0x000050, 164, 0},  // Ironus Coldsteel <Weaponsmith Trainer>
{ 11177,0x000050, 164, 0},  // Okothos Ironrager <Armorsmith>
{ 11178,0x000050, 164, 0},  // Borgosh Corebender <Weaponsmith>
{ 11182,0x001080,   0, 0},  // Nixxrak <Heavy Armor Merchant>
{ 11183,0x001080,   0, 0},  // Blixxrak <Light Armor Merchant>
{ 11184,0x001180,   0, 0},  // Wixxrak <Weaponsmith & Gunsmith>
{ 11185,0x000080,   0, 0},  // Xizzer Fizzbolt <Engineering Supplies>
{ 11186,0x000080,   0, 0},  // Lunnix Sprocketslip <Mining Supplies>
{ 11187,0x000280,   0, 0},  // Himmik <Food & Drink>
{ 11188,0x000080,   0, 0},  // Evie Whirlbrew <Alchemy Supplies>
{ 11189,0x000080,   0, 0},  // Qia <Trade Supplies>
{ 11278,0x001080,   0, 0},  // Magnus Frostwake
{ 11287,0x000080,   0, 0},  // Baker Masterson
{ 11397,0x000030,   0, 5},  // Nara Meideros <Priest Trainer>
{ 11401,0x000030,   0, 5},  // Priestess Alathea <Priest Trainer>
{ 11406,0x000030,   0, 5},  // High Priest Rohan <Priest Trainer>
{ 11536,0x000080,   0, 0},  // Quartermaster Miranda Breechlock <The Argent Dawn>
{ 11555,0x000080,   0, 0},  // Gorn One Eye
{ 11557,0x0000d0,   0, 0},  // Meilosh
{ 11703,0x001080,   0, 0},  // Graw Cornerstone <Mail Armor Merchant>
{ 11865,0x000050,   0, 0},  // Buliwyf Stonehand <Weapon Master>
{ 11866,0x000050,   0, 0},  // Ilyenia Moonfire <Weapon Master>
{ 11867,0x000050,   0, 0},  // Woo Ping <Weapon Master>
{ 11868,0x000050,   0, 0},  // Sayoc <Weapon Master>
{ 11869,0x000050,   0, 0},  // Ansekhwa <Weapon Master>
{ 11870,0x000050,   0, 0},  // Archibald <Weapon Master>
{ 11874,0x000080,   0, 0},  // Masat T'andr <Superior Leatherworker>
{ 11899,0x002000,   0, 0},  // Shardi <Wind Rider Master>
{ 11900,0x002000,   0, 0},  // Brakkar <Wind Rider Master>
{ 11901,0x002000,   0, 0},  // Andruk <Wind Rider Master>
{ 12019,0x000280,   0, 0},  // Dargon <Food & Drink Merchant>
{ 12021,0x000180,   0, 0},  // Daeolyn Summerleaf <General Goods>
{ 12022,0x000080,   0, 0},  // Lorelae Wintersong <Trade Supplies>
{ 12023,0x001080,   0, 0},  // Kharedon <Light Armor Merchant>
{ 12024,0x001080,   0, 0},  // Meliri <Weaponsmith>
{ 12025,0x000050, 182, 0},  // Malvor <Herbalism Trainer>
{ 12026,0x000280,   0, 0},  // My'lanna <Food & Drink Merchant>
{ 12027,0x000180,   0, 0},  // Tukk <General Goods>
{ 12028,0x000080,   0, 0},  // Lah'Mawhani <Trade Supplies>
{ 12029,0x001080,   0, 0},  // Narianna <Bowyer>
{ 12030,0x000050, 393, 0},  // Malux <Skinning Trainer>
{ 12031,0x000080,   0, 0},  // Mai'Lahii <Fishing Supplies>
{ 12032,0x000050, 356, 0},  // Lui'Mala <Fishing Trainer>
{ 12033,0x000080,   0, 0},  // Wulan <Cooking Supplies>
{ 12042,0x000030,   0,11},  // Loganaar <Druid Trainer>
{ 12043,0x000080,   0, 0},  // Kulwia <Trade Supplies>
{ 12045,0x001080,   0, 0},  // Hae'Wilani <Axecrafter>
{ 12196,0x010280,   0, 0},  // Innkeeper Kaylisk <Innkeeper>
{ 12384,0x000080,   0, 0},  // Augustus the Touched
{ 12577,0x002000,   0, 0},  // Jarrodenus <Hippogryph Master>
{ 12578,0x002000,   0, 0},  // Mishellena <Hippogryph Master>
{ 12596,0x002000,   0, 0},  // Bibilfaz Featherwhistle <Gryphon Master>
{ 12616,0x002000,   0, 0},  // Vhulgra <Wind Rider Master>
{ 12617,0x002000,   0, 0},  // Khaelyn Steelwing <Gryphon Master>
{ 12636,0x002000,   0, 0},  // Georgia <Bat Handler>
{ 12740,0x002000,   0, 0},  // Faustron <Wind Rider Master>
{ 12778,0x001080,   0, 0},  // Lieutenant Rachel Vaccar <Outland Armor Quartermaster>
{ 12781,0x000080,   0, 0},  // Master Sergeant Biggins <Accessories Quartermaster>
{ 12783,0x000080,   0, 0},  // Lieutenant Karter <War Mount Quartermaster>
{ 12784,0x001080,   0, 0},  // Lieutenant Jackspring <Legacy Weapon Quartermaster>
{ 12785,0x001080,   0, 0},  // Sergeant Major Clate <Legacy Armor Quartermaster>
{ 12788,0x001080,   0, 0},  // Legionnaire Teena <Outland Armor Quartermaster>
{ 12793,0x000080,   0, 0},  // Brave Stonehide <Accessories Quartermaster>
{ 12794,0x000080,   0, 0},  // Stone Guard Zarg <Legacy Weapon Quartermaster>
{ 12795,0x000080,   0, 0},  // First Sergeant Hola'mahi <Legacy Armor Quartermaster>
{ 12796,0x000080,   0, 0},  // Raider Bork <War Mount Quartermaster>
{ 12919,0x000080,   0, 0},  // Nat Pagle
{ 12941,0x000080,   0, 0},  // Jase Farlane <Trade Supplies>
{ 12942,0x001080,   0, 0},  // Leonard Porter <Leatherworking Supplies>
{ 12943,0x000080,   0, 0},  // Werg Thickblade <Leatherworking Supplies>
{ 12944,0x000080,   0, 0},  // Lokhtos Darkbargainer <The Thorium Brotherhood>
{ 12956,0x000080,   0, 0},  // Zannok Hidepiercer <Leatherworking Supplies>
{ 12957,0x000080,   0, 0},  // Blimo Gadgetspring <Trade Supplies>
{ 12958,0x000080,   0, 0},  // Gigget Zipcoil <Trade Supplies>
{ 12959,0x000380,   0, 0},  // Nergal <General Goods>
{ 12960,0x000180,   0, 0},  // Christi Galvanis <General Goods>
{ 12961,0x000050, 356, 0},  // Kil'Hiwana <Fishing Trainer>
{ 12962,0x000280,   0, 0},  // Wik'Tar <Fish Merchant & Supplies>
{ 13018,0x000080,   0, 0},  // Nipsy
{ 13084,0x000050,   0, 0},  // Bixi Wobblebonk <Weapon Master>
{ 13177,0x002000,   0, 0},  // Vahgruk <Wind Rider Master>
{ 13217,0x001080,   0, 0},  // Thanthaldis Snowgleam <Stormpike Supply Officer>
{ 13219,0x001080,   0, 0},  // Jekyll Flandring <Frostwolf Supply Officer>
{ 13283,0x000030,   0, 4},  // Lord Tony Romano <Rogue Trainer>
{ 13417,0x000030,   0, 7},  // Sagorne Creststrider <Shaman Trainer>
{ 13476,0x000880,   0, 0},  // Balai Lok'Wein <Potions, Scrolls & Reagents>
{ 13917,0x020000,   0, 0},  // Izzy Coppergrab <Banker>
{ 14301,0x001180,   0, 0},  // Brinna Valanaar <Bowyer>
{ 14322,0x000080,   0, 0},  // Stomper Kreeg <The Drunk>
{ 14337,0x001080,   0, 0},  // Field Repair Bot 74A
{ 14371,0x001080,   0, 0},  // Shen'dralar Provisioner <House of Shen'dralar>
{ 14401,0x000010,   0, 0},  // Master Elemental Shaper Krixix
{ 14437,0x000080,   0, 0},  // Gorzeeki Wildeyes
{ 14450,0x000080,   0, 0},  // Orphan Matron Nightingale
{ 14522,0x000080,   0, 0},  // Ur'dan
{ 14624,0x001380,   0, 0},  // Master Smith Burninate <The Thorium Brotherhood>
{ 14637,0x000080,   0, 0},  // Zorbin Fandazzle
{ 14731,0x010280,   0, 0},  // Lard <Innkeeper>
{ 14737,0x001080,   0, 0},  // Smith Slagtree <Blacksmithing Supplies>
{ 14738,0x000080,   0, 0},  // Otho Moji'ko <Cooking Supplies>
{ 14739,0x000880,   0, 0},  // Mystic Yayo'jin <Reagents>
{ 14740,0x0000d0, 356, 0},  // Katoom the Angler <Fishing Trainer & Supplies>
{ 14753,0x001080,   0, 0},  // Illiyana Moonblaze <Silverwing Supply Officer>
{ 14754,0x001080,   0, 0},  // Kelm Hargunth <Warsong Supply Officer>
{ 14921,0x001080,   0, 0},  // Rin'wosho the Trader <Zandalar Supplies & Repair>
{ 14961,0x000280,   0, 0},  // Mirvyna Jinglepocket <Smokywood Pastures>
{ 14962,0x000280,   0, 0},  // Dillord Copperpinch <Smokywood Pastures>
{ 14963,0x000280,   0, 0},  // Gapp Jinglepocket <Smokywood Pastures>
{ 14964,0x000280,   0, 0},  // Hecht Copperpinch <Smokywood Pastures>
{ 15124,0x000280,   0, 0},  // Targot Jinglepocket <Smokywood Pastures>
{ 15125,0x000280,   0, 0},  // Kosco Copperpinch <Smokywood Pastures>
{ 15126,0x001080,   0, 0},  // Rutherford Twing <Defilers Supply Officer>
{ 15127,0x001080,   0, 0},  // Samuel Hawke <League of Arathor Supply Officer>
{ 15165,0x000080,   0, 0},  // Haughty Modiste <Fashion Designer>
{ 15174,0x010380,   0, 0},  // Calandrath <Innkeeper>
{ 15175,0x000c80,   0, 0},  // Khur Hornstriker <Reagents>
{ 15176,0x001080,   0, 0},  // Vargus <Blacksmith>
{ 15177,0x002000,   0, 0},  // Cloud Skydancer <Hippogryph Master>
{ 15178,0x002000,   0, 0},  // Runk Windtamer <Wind Rider Master>
{ 15179,0x000080,   0, 0},  // Mishta <Trade Supplies>
{ 15279,0x000030,   0, 8},  // Julia Sunstriker <Mage Trainer>
{ 15280,0x000030,   0, 2},  // Jesthenis Sunstriker <Paladin Trainer>
{ 15283,0x000030,   0, 9},  // Summoner Teli'Larien <Warlock Trainer>
{ 15284,0x000030,   0, 5},  // Matron Arena <Priest Trainer>
{ 15285,0x000030,   0, 4},  // Pathstalker Kariel <Rogue Trainer>
{ 15287,0x000180,   0, 0},  // Shara Sunwing <General Supplies>
{ 15289,0x001080,   0, 0},  // Raelis Dawnstar <Weaponsmith>
{ 15291,0x001080,   0, 0},  // Jainthess Thelryn <Cloth & Leather Merchant>
{ 15292,0x001080,   0, 0},  // Faraden Thelryn <Armorsmith>
{ 15293,0x000080,   0, 0},  // Aendel Windspear
{ 15315,0x001080,   0, 0},  // Mylini Frostmoon <Weapon Merchant>
{ 15350,0x000080,   0, 0},  // Horde Warbringer
{ 15351,0x000080,   0, 0},  // Alliance Brigadier General
{ 15397,0x010380,   0, 0},  // Marniel Amberlight <Innkeeper>
{ 15400,0x0010d0, 164, 0},  // Arathel Sunforge <Blacksmithing Trainer>
{ 15419,0x000080,   0, 0},  // Kania <Enchanting Supplies>
{ 15433,0x010280,   0, 0},  // Innkeeper Delaniel <Innkeeper>
{ 15501,0x000050, 755, 0},  // Aleinia <Jewelcrafting Trainer>
{ 15513,0x000030,   0, 3},  // Ranger Sallina <Hunter Trainer>
{ 15659,0x200000,   0, 0},  // Auctioneer Jaxon
{ 15675,0x200000,   0, 0},  // Auctioneer Stockton
{ 15676,0x200000,   0, 0},  // Auctioneer Yarly
{ 15677,0x200000,   0, 0},  // Auctioneer Graves
{ 15678,0x200000,   0, 0},  // Auctioneer Silva'las
{ 15679,0x200000,   0, 0},  // Auctioneer Cazarez
{ 15681,0x200000,   0, 0},  // Auctioneer O'reely
{ 15682,0x200000,   0, 0},  // Auctioneer Cain
{ 15683,0x200000,   0, 0},  // Auctioneer Naxxremis
{ 15684,0x200000,   0, 0},  // Auctioneer Tricket
{ 15686,0x200000,   0, 0},  // Auctioneer Rhyker
{ 16015,0x000880,   0, 0},  // Vi'el <Exotic Reagent Merchant>
{ 16160,0x000050, 333, 0},  // Magistrix Eredania <Enchanting Trainer>
{ 16161,0x000050, 171, 0},  // Arcanist Sheynathren <Alchemy Trainer>
{ 16186,0x001080,   0, 0},  // Vara <Cloth & Leather Merchant>
{ 16187,0x000180,   0, 0},  // Quartermaster Lymel <General Goods>
{ 16189,0x002000,   0, 0},  // Skymaster Sunwing <Dragonhawk Master>
{ 16191,0x000080,   0, 0},  // Sathren Azuredawn <General Goods>
{ 16192,0x002000,   0, 0},  // Skymistress Gloaming <Dragonhawk Master>
{ 16224,0x000080,   0, 0},  // Rathis Tomber <Trade Supplies>
{ 16227,0x002000,   0, 0},  // Bragok <Flight Master>
{ 16253,0x0000d0, 185, 0},  // Master Chef Mouldier <Cooking Trainer & Supplies>
{ 16256,0x010280,   0, 0},  // Jessica Chambers <Innkeeper>
{ 16257,0x001080,   0, 0},  // Geron <Weapon Merchant>
{ 16258,0x001080,   0, 0},  // Farsil <Armor & Shield Merchant>
{ 16259,0x000180,   0, 0},  // Sheri <General Goods>
{ 16260,0x001080,   0, 0},  // Areyn <General Goods>
{ 16261,0x001080,   0, 0},  // Sathiel <Trade Supplies>
{ 16262,0x000080,   0, 0},  // Landraelanis <Tradesman>
{ 16263,0x001080,   0, 0},  // Paelarin <Bowyer>
{ 16264,0x000080,   0, 0},  // Winaestra <Hawkstrider Breeder>
{ 16266,0x000030,   0, 9},  // Celoenus <Warlock Trainer>
{ 16268,0x000480,   0, 0},  // Eralan <Poison Supplies>
{ 16269,0x000030,   0, 8},  // Garridel <Mage Trainer>
{ 16270,0x000030,   0, 3},  // Hannovia <Hunter Trainer>
{ 16271,0x000010,   0, 0},  // Telenus <Pet Trainer>
{ 16272,0x000050, 129, 0},  // Kanaria <First Aid Trainer>
{ 16273,0x000050, 393, 0},  // Mathreyn <Skinning Trainer>
{ 16274,0x001080,   0, 0},  // Narina <Bowyer>
{ 16275,0x000030,   0, 2},  // Noellene <Paladin Trainer>
{ 16276,0x000030,   0, 5},  // Ponaris <Priest Trainer>
{ 16277,0x000050, 185, 0},  // Quarelestra <Cooking Trainer>
{ 16278,0x000050, 165, 0},  // Sathein <Leatherworking Trainer>
{ 16279,0x000030,   0, 4},  // Tannaria <Rogue Trainer>
{ 16280,0x000050, 762, 0},  // Perascamin <Riding Trainer>
{ 16366,0x0000d0, 197, 0},  // Sempstress Ambershine <Tailoring Trainer>
{ 16367,0x0000d0, 182, 0},  // Botanist Tyniarrel <Herbalism Trainer & Supplies>
{ 16376,0x001080,   0, 0},  // Craftsman Wilhelm <Brotherhood of the Light>
{ 16388,0x001080,   0, 0},  // Koren <The Blacksmith>
{ 16442,0x000280,   0, 0},  // Vinemaster Suntouched <Wine & Spirits Merchant>
{ 16443,0x000080,   0, 0},  // Zalene Firstlight <Food & Drink>
{ 16444,0x000180,   0, 0},  // Halis Dawnstrider <General Goods>
{ 16458,0x010280,   0, 0},  // Innkeeper Faralia <Innkeeper>
{ 16499,0x000030,   0, 3},  // Keilnei <Hunter Trainer>
{ 16500,0x000030,   0, 8},  // Valaatu <Mage Trainer>
{ 16501,0x000030,   0, 2},  // Aurelon <Paladin Trainer>
{ 16502,0x000030,   0, 5},  // Zalduun <Priest Trainer>
{ 16503,0x000030,   0, 1},  // Kore <Warrior Trainer>
{ 16528,0x001080,   0, 0},  // Provisioner Vredigar
{ 16542,0x010280,   0, 0},  // Innkeeper Kalarin <Innkeeper>
{ 16543,0x000080,   0, 0},  // Garon Hutchins
{ 16553,0x010280,   0, 0},  // Caregiver Chellan <Innkeeper>
{ 16583,0x0010d0, 164, 0},  // Rohok <Master Blacksmithing Trainer>
{ 16585,0x000280,   0, 0},  // Cookie One-Eye <Food & Drink>
{ 16587,0x002000,   0, 0},  // Barley <Wind Rider Master>
{ 16588,0x000cd0, 171, 0},  // Apothecary Antonivich <Master Alchemy Trainer>
{ 16602,0x010380,   0, 0},  // Floyd Pinkus <Innkeeper>
{ 16610,0x000080,   0, 0},  // Kredis <Tabard Vendor>
{ 16611,0x000080,   0, 0},  // Zalle <Reagents>
{ 16612,0x000880,   0, 0},  // Velanni <Alchemy Supplies & Reagents>
{ 16613,0x000080,   0, 0},  // Parnis <Tradesman>
{ 16615,0x020000,   0, 0},  // Novia <Banker>
{ 16616,0x020000,   0, 0},  // Periel <Banker>
{ 16617,0x020000,   0, 0},  // Daenice <Banker>
{ 16618,0x010280,   0, 0},  // Innkeeper Velandra <Innkeeper>
{ 16619,0x001180,   0, 0},  // Celana <Bowyer>
{ 16620,0x001180,   0, 0},  // Mathaleron <Gunsmith>
{ 16621,0x000050,   0, 0},  // Ileda <Weapon Master>
{ 16623,0x001080,   0, 0},  // Zyandrel <Cloth Armor Merchant>
{ 16624,0x000080,   0, 0},  // Gelanthis <Jewelcrafting Supplies>
{ 16625,0x001080,   0, 0},  // Keeli <Mail Armor Merchant>
{ 16626,0x001080,   0, 0},  // Tynna <Plate Armor Merchant>
{ 16627,0x200000,   0, 0},  // Ithillan <Auctioneer>
{ 16628,0x200000,   0, 0},  // Caidori <Auctioneer>
{ 16629,0x200000,   0, 0},  // Tandron <Auctioneer>
{ 16631,0x001080,   0, 0},  // Andra <Clothier>
{ 16632,0x001080,   0, 0},  // Oss <Wand Vendor>
{ 16633,0x000050, 333, 0},  // Sedana <Enchanting Trainer>
{ 16635,0x000080,   0, 0},  // Lyna <Enchanting Supplies>
{ 16636,0x001080,   0, 0},  // Zathanna <Wand Vendor>
{ 16637,0x001080,   0, 0},  // Welethelon <Blunt Weapon Merchant>
{ 16638,0x000080,   0, 0},  // Deynna <Tailoring Supplies>
{ 16640,0x000050, 197, 0},  // Keelen Sheets <Tailoring Trainer>
{ 16641,0x000080,   0, 0},  // Melaris <Alchemy Supplies>
{ 16642,0x000050, 171, 0},  // Camberon <Alchemy Trainer>
{ 16644,0x000050, 182, 0},  // Botanist Nathera <Herbalism Trainer>
{ 16646,0x000030,   0, 9},  // Alamma <Warlock Trainer>
{ 16647,0x000030,   0, 9},  // Talionia <Warlock Trainer>
{ 16648,0x000030,   0, 9},  // Zanien <Warlock Trainer>
{ 16650,0x000080,   0, 0},  // Bithrus <Fireworks Vendor>
{ 16651,0x000030,   0, 8},  // Zaedana <Mage Trainer>
{ 16652,0x000030,   0, 8},  // Quithas <Mage Trainer>
{ 16653,0x000030,   0, 8},  // Inethven <Mage Trainer>
{ 16654,0x000030,   0, 0},  // Narinth <Portal Trainer>
{ 16655,0x000030,   0,11},  // Harene Plainwalker <Druid Trainer>
{ 16657,0x000080,   0, 0},  // Feera <Engineering Supplies>
{ 16658,0x000030,   0, 5},  // Aldrae <Priest Trainer>
{ 16659,0x000030,   0, 5},  // Lotheolan <Priest Trainer>
{ 16660,0x000030,   0, 5},  // Belestra <Priest Trainer>
{ 16662,0x000050, 129, 0},  // Alestus <First Aid Trainer>
{ 16663,0x000050, 186, 0},  // Belil <Mining Trainer>
{ 16664,0x000080,   0, 0},  // Zelan <Mining Supplies>
{ 16666,0x001080,   0, 0},  // Feledis <Axe Vendor>
{ 16667,0x000050, 202, 0},  // Danwe <Engineering Trainer>
{ 16669,0x000050, 164, 0},  // Bemarrin <Blacksmithing Trainer>
{ 16670,0x001080,   0, 0},  // Eriden <Blacksmithing Supplies>
{ 16672,0x000030,   0, 3},  // Tana <Hunter Trainer>
{ 16673,0x000030,   0, 3},  // Oninath <Hunter Trainer>
{ 16674,0x000030,   0, 3},  // Zandine <Hunter Trainer>
{ 16675,0x000010,   0, 0},  // Halthenis <Pet Trainer>
{ 16676,0x000050, 185, 0},  // Sylann <Cook>
{ 16677,0x000080,   0, 0},  // Quelis <Cooking Supplies>
{ 16678,0x001080,   0, 0},  // Rahein <Blade Vendor>
{ 16679,0x000030,   0, 2},  // Osselan <Paladin Trainer>
{ 16680,0x000030,   0, 2},  // Ithelis <Paladin Trainer>
{ 16681,0x000030,   0, 2},  // Champion Bachi <Paladin Trainer>
{ 16683,0x000480,   0, 0},  // Darlia <Poison Supplies>
{ 16684,0x000030,   0, 4},  // Zelanis <Rogue Trainer>
{ 16685,0x000030,   0, 4},  // Elara <Rogue Trainer>
{ 16686,0x000030,   0, 4},  // Nerisen <Rogue Trainer>
{ 16688,0x000050, 165, 0},  // Lynalis <Leatherworking Trainer>
{ 16689,0x000080,   0, 0},  // Zaralda <Leatherworking Supplies>
{ 16690,0x000080,   0, 0},  // Rathin <Bag Vendor>
{ 16691,0x001080,   0, 0},  // Noraelath <Leather Armor Merchant>
{ 16692,0x000050, 393, 0},  // Tyn <Skinning Trainer>
{ 16693,0x001080,   0, 0},  // Winthren <Shield Merchant>
{ 16705,0x000080,   0, 0},  // Altaa <Alchemy Supplies>
{ 16706,0x000880,   0, 0},  // Musal <Alchemy Supplies & Reagents>
{ 16707,0x200000,   0, 0},  // Eoch <Auctioneer>
{ 16708,0x000080,   0, 0},  // Dekin <Fishing Supplies>
{ 16709,0x000080,   0, 0},  // Cuzi <Bag Vendor>
{ 16710,0x020000,   0, 0},  // Kellag <Banker>
{ 16712,0x000010,   0, 0},  // Ganaar <Pet Trainer>
{ 16713,0x000080,   0, 0},  // Arras <Blacksmithing Supplies>
{ 16714,0x001080,   0, 0},  // Ven <Blade Vendor>
{ 16715,0x001180,   0, 0},  // Avelii <Bowyer>
{ 16716,0x001080,   0, 0},  // Gornii <Cloth Armor Merchant>
{ 16718,0x000080,   0, 0},  // Phea <Cooking Supplies>
{ 16719,0x000050, 185, 0},  // Mumman <Cook>
{ 16721,0x000030,   0,11},  // Shalannius <Druid Trainer>
{ 16722,0x000080,   0, 0},  // Egomis <Enchanting Supplies>
{ 16723,0x000050, 171, 0},  // Lucc <Alchemy Trainer>
{ 16724,0x000050, 164, 0},  // Miall <Blacksmithing Trainer>
{ 16725,0x000050, 333, 0},  // Nahogg <Enchanting Trainer>
{ 16726,0x000050, 202, 0},  // Ockil <Engineering Trainer>
{ 16728,0x000050, 165, 0},  // Akham <Leatherworking Trainer>
{ 16729,0x000050, 197, 0},  // Refik <Tailoring Trainer>
{ 16731,0x000050, 129, 0},  // Nus <First Aid Trainer>
{ 16732,0x000180,   0, 0},  // Onnis <General Goods>
{ 16735,0x001180,   0, 0},  // Muhaa <Gunsmith>
{ 16736,0x000050, 182, 0},  // Cemmorhan <Herbalism Trainer>
{ 16738,0x000030,   0, 3},  // Deremiis <Hunter Trainer>
{ 16739,0x010280,   0, 0},  // Caregiver Breel <Innkeeper>
{ 16747,0x001080,   0, 0},  // Mahri <Leather Armor Merchant>
{ 16748,0x000080,   0, 0},  // Haferet <Leatherworking Supplies>
{ 16749,0x000030,   0, 8},  // Edirah <Mage Trainer>
{ 16750,0x001080,   0, 0},  // Yil <Mail Armor Merchant>
{ 16751,0x000080,   0, 0},  // Merran <Mining Supplies>
{ 16752,0x000050, 186, 0},  // Muaat <Mining Trainer>
{ 16753,0x001080,   0, 0},  // Gotaan <Plate Armor Merchant>
{ 16755,0x000030,   0, 0},  // Lunaraa <Portal Trainer>
{ 16756,0x000030,   0, 5},  // Caedmos <Priest Trainer>
{ 16757,0x000080,   0, 0},  // Bildine <Reagents>
{ 16761,0x000030,   0, 2},  // Baatun <Paladin Trainer>
{ 16762,0x001080,   0, 0},  // Treall <Shield Merchant>
{ 16763,0x000050, 393, 0},  // Remere <Skinning Trainer>
{ 16765,0x001080,   0, 0},  // Ellomin <Blunt Weapon Merchant>
{ 16766,0x000080,   0, 0},  // Issca <Tabard Vendor>
{ 16767,0x000080,   0, 0},  // Neii <Tailoring Supplies>
{ 16768,0x000080,   0, 0},  // Nurguni <Tradesman>
{ 16771,0x000030,   0, 1},  // Ahonan <Warrior Trainer>
{ 16773,0x000050,   0, 0},  // Handiir <Weapon Master>
{ 16774,0x000050, 356, 0},  // Erett <Fishing Trainer>
{ 16780,0x000050, 356, 0},  // Drathen <Fishing Trainer>
{ 16782,0x000080,   0, 0},  // Yatheon <Engineering Supplies>
{ 16798,0x000280,   0, 0},  // Provisioner Anir <Food & Drink Vendor>
{ 16822,0x002000,   0, 0},  // Flightmaster Krill Bitterhue <Gryphon Master>
{ 16823,0x0010d0, 164, 0},  // Humphry <Master Blacksmithing Trainer>
{ 16826,0x010380,   0, 0},  // Sid Limbardi <Innkeeper>
{ 16829,0x000c80,   0, 0},  // Magus Zabraxis <Reagents>
{ 16860,0x000080,   0, 0},  // Jilanne
{ 16917,0x001080,   0, 0},  // Aurok <Armorsmith>
{ 16918,0x001080,   0, 0},  // Jel <Cloth & Leather Merchant>
{ 16919,0x001180,   0, 0},  // Mura <Weaponsmith>
{ 16920,0x000180,   0, 0},  // Ryosh <General Supplies>
{ 17005,0x000050,   0, 0},  // Duelist Larenis <Weapon Master>
{ 17089,0x000030,   0, 7},  // Firmanvaar <Shaman Trainer>
{ 17101,0x0000d0, 356, 0},  // Diktynna <Fishing Trainer & Supplies>
{ 17105,0x000030,   0, 8},  // Emissary Valustraa <Mage Trainer>
{ 17110,0x000030,   0, 3},  // Acteon <Hunter Trainer>
{ 17120,0x000030,   0, 1},  // Behomat <Warrior Trainer>
{ 17121,0x000030,   0, 2},  // Kavaan <Paladin Trainer>
{ 17122,0x000030,   0, 3},  // Vord <Hunter Trainer>
{ 17204,0x000030,   0, 7},  // Farseer Nobundo <Shaman Trainer>
{ 17212,0x000030,   0, 7},  // Tuluun <Shaman Trainer>
{ 17214,0x000050, 129, 0},  // Anchorite Fateema <First Aid Trainer>
{ 17215,0x000050, 171, 0},  // Daedal <Alchemy Trainer>
{ 17219,0x000030,   0, 7},  // Sulaa <Shaman Trainer>
{ 17222,0x0000d0, 202, 0},  // Artificer Daelo <Engineering Trainer>
{ 17245,0x0010d0, 164, 0},  // Blacksmith Calypso <Blacksmithing Trainer & Supplies>
{ 17246,0x0000d0, 185, 0},  // "Cookie" McWeaksauce <Cooking Trainer & Supplies>
{ 17277,0x000280,   0, 0},  // Provisioner Valine <Food & Drink>
{ 17412,0x001080,   0, 0},  // Phaedra <Weapon Merchant>
{ 17421,0x000180,   0, 0},  // Clopper Wizbang <Explorers' League>
{ 17424,0x000050, 129, 0},  // Anchorite Paetheus <First Aid Trainer>
{ 17434,0x000050, 182, 0},  // Morae <Herbalism Trainer>
{ 17441,0x000050, 393, 0},  // Gurf <Skinning Trainer>
{ 17442,0x000050, 165, 0},  // Moordo <Leatherworking Trainer>
{ 17446,0x000180,   0, 0},  // Parkat Steelfur <General Goods>
{ 17480,0x000030,   0, 1},  // Ruada <Warrior Trainer>
{ 17481,0x000030,   0, 8},  // Semid <Mage Trainer>
{ 17482,0x000030,   0, 5},  // Guvan <Priest Trainer>
{ 17483,0x000030,   0, 2},  // Tullas <Paladin Trainer>
{ 17484,0x000010,   0, 0},  // Buruk <Pet Trainer>
{ 17486,0x000080,   0, 0},  // Ziz <Tradesman>
{ 17487,0x000050, 197, 0},  // Erin Kelly <Tailoring Trainer>
{ 17488,0x000050, 186, 0},  // Dulvi <Mining Trainer>
{ 17489,0x000180,   0, 0},  // Logan Daniel <General Goods>
{ 17490,0x000080,   0, 0},  // Ergh of the Stillpine <Trade Supplies>
{ 17504,0x000030,   0, 1},  // Kazi <Warrior Trainer>
{ 17505,0x000030,   0, 3},  // Killac <Hunter Trainer>
{ 17509,0x000030,   0, 2},  // Jol <Paladin Trainer>
{ 17510,0x000030,   0, 5},  // Izmir <Priest Trainer>
{ 17511,0x000030,   0, 5},  // Fallat <Priest Trainer>
{ 17512,0x000080,   0, 0},  // Arred <Jewelcrafting Supplies>
{ 17513,0x000030,   0, 8},  // Harnan <Mage Trainer>
{ 17514,0x000030,   0, 8},  // Bati <Mage Trainer>
{ 17518,0x001880,   0, 0},  // Ythyar
{ 17519,0x000030,   0, 7},  // Hobahken <Shaman Trainer>
{ 17520,0x000030,   0, 7},  // Gurrag <Shaman Trainer>
{ 17553,0x010280,   0, 0},  // Caregiver Topher Loaal <Innkeeper>
{ 17554,0x002000,   0, 0},  // Laando <Hippogryph Master>
{ 17555,0x002000,   0, 0},  // Stephanos <Hippogryph Master>
{ 17584,0x000080,   0, 0},  // Torallius the Pack Handler <Elekk Breeder>
{ 17585,0x000080,   0, 0},  // Quartermaster Urgronn <Thrallmar Quartermaster>
{ 17598,0x000180,   0, 0},  // Renn'az <Ammunition Vendor>
{ 17627,0x200000,   0, 0},  // Jenath <Auctioneer>
{ 17628,0x200000,   0, 0},  // Vynna <Auctioneer>
{ 17629,0x200000,   0, 0},  // Feynna <Auctioneer>
{ 17630,0x010280,   0, 0},  // Innkeeper Jovia <Innkeeper>
{ 17631,0x020000,   0, 0},  // Ceera <Banker>
{ 17632,0x020000,   0, 0},  // Elana <Banker>
{ 17633,0x020000,   0, 0},  // Hatheon <Banker>
{ 17634,0x000050, 202, 0},  // K. Lee Smallfry <Master Engineering Trainer>
{ 17637,0x000050, 202, 0},  // Mack Diver <Master Engineering Trainer>
{ 17655,0x001080,   0, 0},  // Blacksmith Frances <Blacksmithing Supplies>
{ 17656,0x000080,   0, 0},  // Heron Skygaze <Food & Drink>
{ 17657,0x000080,   0, 0},  // Logistics Officer Ulrike <Honor Hold Quartermaster>
{ 17667,0x001180,   0, 0},  // Beega <Bowyer>
{ 17773,0x020000,   0, 0},  // Ossco <Banker>
{ 17844,0x000030,   0, 0},  // Vindicator Aesom <Triumvirate of the Hand>
{ 17904,0x001080,   0, 0},  // Fedryen Swiftspear <Cenarion Expedition Quartermaster>
{ 17929,0x001080,   0, 0},  // Kioni <Cloth & Leather Merchant>
{ 17930,0x001080,   0, 0},  // Nabek <Weapons & Armor Merchant>
{ 17983,0x000050, 182, 0},  // Heur <Herbalism Trainer>
{ 18005,0x000080,   0, 0},  // Haalrun <Alchemy Supplies>
{ 18006,0x000c80,   0, 0},  // Noraani <Reagent Merchant>
{ 18009,0x000080,   0, 0},  // Puluu
{ 18010,0x001080,   0, 0},  // Maktu <Armor Merchant>
{ 18011,0x001180,   0, 0},  // Zurai <Merchant>
{ 18015,0x000080,   0, 0},  // Gambarinka <Tradesman>
{ 18017,0x000880,   0, 0},  // Seer Janidi <Reagents>
{ 18018,0x0000d0, 356, 0},  // Zurjaya <Fishing Trainer>
{ 18019,0x000080,   0, 0},  // Timothy Daniels <The Specialist>
{ 18243,0x000880,   0, 0},  // Lorti <Merchant>
{ 18245,0x010280,   0, 0},  // Merajit <Innkeeper>
{ 18251,0x010080,   0, 0},  // Caregiver Abidaar <Innkeeper>
{ 18255,0x000080,   0, 0},  // Apprentice Darius
{ 18266,0x000080,   0, 0},  // Warrant Officer Tracy Proudwell
{ 18267,0x000080,   0, 0},  // Battlecryer Blackeye
{ 18277,0x000080,   0, 0},  // Kinamisa <Leatherworking Supplies>
{ 18278,0x001080,   0, 0},  // Pilot Marsha <Engineering Supplies>
{ 18347,0x000080,   0, 0},  // Olirea <Fishing Supplies>
{ 18348,0x200000,   0, 0},  // Fanin <Auctioneer>
{ 18349,0x200000,   0, 0},  // Iressa <Auctioneer>
{ 18350,0x020000,   0, 0},  // Jaela <Banker>
{ 18382,0x001080,   0, 0},  // Mycah <Sporeggar Quartermaster>
{ 18426,0x001080,   0, 0},  // Terellia <Trade Supplies>
{ 18427,0x000080,   0, 0},  // Fazu <Tradesman>
{ 18484,0x000080,   0, 0},  // Wind Trader Lathrai
{ 18542,0x010080,   0, 0},  // Alexston Chrome <Tavern of Time>
{ 18664,0x000080,   0, 0},  // Aged Dalaran Wizard
{ 18672,0x001080,   0, 0},  // Thomas Yance <Travelling Salesman>
{ 18747,0x000050, 186, 0},  // Krugosh <Master Mining Trainer>
{ 18748,0x000050, 182, 0},  // Ruak Stronghorn <Master Herbalism Trainer>
{ 18749,0x0000d0, 197, 0},  // Dalinna <Master Tailoring Trainer>
{ 18751,0x0000d0, 755, 0},  // Kalaen <Master Jewelcrafting Trainer>
{ 18752,0x0000d0, 202, 0},  // Zebig <Master Engineering Trainer>
{ 18753,0x0000d0, 333, 0},  // Felannia <Master Enchanting Trainer>
{ 18754,0x0000d0, 165, 0},  // Barim Spilthoof <Master Leatherworking Trainer>
{ 18755,0x000050, 393, 0},  // Moorutu <Master Skinning Trainer>
{ 18756,0x000080,   0, 0},  // Haris Pilton <Socialite>
{ 18761,0x200000,   0, 0},  // Darise <Auctioneer>
{ 18771,0x0000d0, 165, 0},  // Brumman <Master Leatherworking Trainer>
{ 18772,0x0000d0, 197, 0},  // Hama <Master Tailoring Trainer>
{ 18773,0x0000d0, 333, 0},  // Johan Barnes <Master Enchanting Trainer>
{ 18774,0x0000d0, 755, 0},  // Tatiana <Master Jewelcrafting Trainer>
{ 18775,0x0000d0, 202, 0},  // Lebowski <Master Engineering Trainer>
{ 18776,0x000050, 182, 0},  // Rorelien <Master Herbalism Trainer>
{ 18777,0x000050, 393, 0},  // Jelena Nightsky <Master Skinning Trainer>
{ 18779,0x000050, 186, 0},  // Hurnak Grimmord <Master Mining Trainer>
{ 18785,0x002000,   0, 0},  // Kuma <Hippogryph Master>
{ 18788,0x002000,   0, 0},  // Munci <Hippogryph Master>
{ 18789,0x002000,   0, 0},  // Furgu <Hippogryph Master>
{ 18791,0x002000,   0, 0},  // Du'ga <Wind Rider Master>
{ 18802,0x000cd0, 171, 0},  // Alchemist Gribble <Master Alchemy Trainer>
{ 18804,0x000050,   0, 0},  // Prospector Nachlan <Explorers' League>
{ 18807,0x002000,   0, 0},  // Kerna <Wind Rider Master>
{ 18808,0x002000,   0, 0},  // Gursha <Wind Rider Master>
{ 18809,0x002000,   0, 0},  // Furnan Skysoar <Gryphon Master>
{ 18810,0x000180,   0, 0},  // Otonambusi <General Goods>
{ 18811,0x000180,   0, 0},  // Meriaad <General Goods>
{ 18821,0x000080,   0, 0},  // Quartermaster Jaffrey Noreliqe
{ 18822,0x000080,   0, 0},  // Quartermaster Davian Vaclav
{ 18905,0x010280,   0, 0},  // Innkeeper Bazil Olof'tazun <Innkeeper>
{ 18906,0x010280,   0, 0},  // Caregiver Ophera Windfury <Innkeeper>
{ 18907,0x010380,   0, 0},  // Innkeeper Coryth Stoktron <Innkeeper>
{ 18908,0x010080,   0, 0},  // Innkeeper Kerp <Innkeeper>
{ 18911,0x000090,   0, 0},  // Juno Dufrain <Master Fishing Trainer>
{ 18926,0x001080,   0, 0},  // Sleyin <Weapon Vendor>
{ 18929,0x000280,   0, 0},  // Kyrenna <Cheese Vendor>
{ 18930,0x002000,   0, 0},  // Vlagga Freyfeather <Wind Rider Master>
{ 18931,0x002000,   0, 0},  // Amish Wildhammer <Gryphon Master>
{ 18937,0x002000,   0, 0},  // Amerun Leafshade <Hippogryph Master>
{ 18938,0x002000,   0, 0},  // Krexcil <Flight Master>
{ 18939,0x002000,   0, 0},  // Brubeck Stormfoot <Gryphon Master>
{ 18940,0x002000,   0, 0},  // Nutral <Flight Master>
{ 18942,0x002000,   0, 0},  // Innalia <Wind Rider Master>
{ 18947,0x000080,   0, 0},  // Solanin <Bag Vendor>
{ 18951,0x000080,   0, 0},  // Erilia <Enchanting Vendor>
{ 18953,0x002000,   0, 0},  // Unoke Tenderhoof <Wind Rider Master>
{ 18954,0x000080,   0, 0},  // Sailor Melinan <Drink Vendor>
{ 18957,0x010280,   0, 0},  // Innkeeper Grilka <Innkeeper>
{ 18959,0x000180,   0, 0},  // Dod'ss <General Goods>
{ 18960,0x000080,   0, 0},  // Rungor <Trade Goods>
{ 18962,0x001080,   0, 0},  // Bar Talet <Bowyer>
{ 18987,0x0000d0, 185, 0},  // Gaston <Chef>
{ 18988,0x0000d0, 185, 0},  // Baxter <Chef>
{ 18990,0x000050, 129, 0},  // Burko <Medic>
{ 18991,0x000050, 129, 0},  // Aresella <Medic>
{ 18993,0x0000d0, 185, 0},  // Naka <Cooking Supplies>
{ 18997,0x001080,   0, 0},  // Fallesh Sunfallow <Weapon Merchant>
{ 18998,0x000880,   0, 0},  // Lursa Sunfallow <Reagents>
{ 19001,0x001080,   0, 0},  // Talaara <Weapon Merchant>
{ 19004,0x000880,   0, 0},  // Vodesiin <Reagents>
{ 19011,0x001080,   0, 0},  // Osrok the Immovable <Armorsmith & Blacksmithing Supplies>
{ 19012,0x001080,   0, 0},  // Sparik <Armorsmith & Blacksmithing Supplies>
{ 19013,0x000c80,   0, 0},  // Vanteg <Reagents & Poison Supplies>
{ 19014,0x000080,   0, 0},  // Ogir <Reagents & Poison Supplies>
{ 19015,0x000080,   0, 0},  // Mathar G'ochar <Trade Supplies>
{ 19017,0x000080,   0, 0},  // Borto <Trade Supplies>
{ 19020,0x000180,   0, 0},  // Matron Qualia <General Goods>
{ 19021,0x000080,   0, 0},  // Nancila <General Goods>
{ 19034,0x020000,   0, 0},  // Mendorn <Banker>
{ 19038,0x000280,   0, 0},  // Supply Officer Mills <Rations>
{ 19042,0x000080,   0, 0},  // Leeli Longhaggle <Trade Goods>
{ 19043,0x001080,   0, 0},  // Ahemen <Staff Vendor>
{ 19045,0x000280,   0, 0},  // Oloraak <Fish Merchant>
{ 19046,0x010000,   0, 0},  // Minalei <Innkeeper>
{ 19047,0x001080,   0, 0},  // Lissaf <Blade Merchant>
{ 19049,0x000480,   0, 0},  // Karokka <Poison Supplies>
{ 19050,0x000280,   0, 0},  // Garul <Food & Drink>
{ 19052,0x000050, 171, 0},  // Lorokeem <Master Alchemy Trainer>
{ 19053,0x000180,   0, 0},  // Fabian Lanzonelli <General Goods & Bags>
{ 19056,0x001080,   0, 0},  // Cecil Meyers <Blacksmithing Supplies>
{ 19063,0x000050, 755, 0},  // Hamanar <Master Jewelcrafting Trainer>
{ 19065,0x000080,   0, 0},  // Inessera <Jewelcrafting Supplies Vendor>
{ 19074,0x000080,   0, 0},  // Skreah <Alchemy Supplies>
{ 19180,0x000050, 393, 0},  // Seymour <Master Skinning Trainer>
{ 19182,0x000280,   0, 0},  // Shaarubo <Bartender>
{ 19184,0x000050, 129, 0},  // Mildred Fletcher <Physician>
{ 19185,0x000050, 185, 0},  // Jack Trapper <Cook>
{ 19186,0x0002d0,   0, 0},  // Kylene <Barmaid>
{ 19187,0x000050, 165, 0},  // Darmari <Master Leatherworking Trainer>
{ 19194,0x000080,   0, 0},  // Ernie Packwell <Trade Goods>
{ 19195,0x000080,   0, 0},  // Jim Saltit <Cooking Supplies>
{ 19196,0x000080,   0, 0},  // Cro Threadstrong <Leatherworking Supplies>
{ 19197,0x000180,   0, 0},  // Eral <General Goods>
{ 19213,0x000080,   0, 0},  // Eiin <Specialty Tailoring Supplies>
{ 19223,0x000280,   0, 0},  // Granny Smith <Fruit Seller>
{ 19227,0x000080,   0, 0},  // Griftah <Amazing Amulets>
{ 19232,0x010280,   0, 0},  // Innkeeper Haelthol <Innkeeper>
{ 19234,0x000080,   0, 0},  // Yurial Soulwater <Enchanting Supplies>
{ 19235,0x000080,   0, 0},  // Amshesha Stilldark <Reagents>
{ 19236,0x001080,   0, 0},  // Quelama Lightblade <Wand Vendor>
{ 19238,0x001080,   0, 0},  // Urumir Stavebright <Staff Vendor>
{ 19239,0x001480,   0, 0},  // Mahir Redstroke <Dagger Vendor>
{ 19240,0x001080,   0, 0},  // Selanam the Blade <Sword Vendor>
{ 19243,0x000180,   0, 0},  // Nalama the Merchant <General Goods>
{ 19244,0x000080,   0, 0},  // Trader Endernor <Trade Goods>
{ 19245,0x000080,   0, 0},  // Vinemaster Alamaro <Wine Vendor>
{ 19246,0x020000,   0, 0},  // Berudan Keysworn <Banker>
{ 19251,0x000050, 333, 0},  // Enchantress Volali <Enchanting Trainer>
{ 19252,0x000050, 333, 0},  // High Enchanter Bardolan <Master Enchanting Trainer>
{ 19296,0x010280,   0, 0},  // Innkeeper Biribi <Innkeeper>
{ 19314,0x001380,   0, 0},  // Supply Officer Shandria <General Goods & Repairs>
{ 19315,0x001080,   0, 0},  // Supply Officer Isabel <General Goods & Repairs>
{ 19317,0x002000,   0, 0},  // Drek'Gol <Wind Rider Master>
{ 19318,0x020000,   0, 0},  // Gromden <Banker>
{ 19319,0x010000,   0, 0},  // Innkeeper Darg Bloodclaw <Innkeeper>
{ 19321,0x000080,   0, 0},  // Quartermaster Endarin <Aldor Quartermaster>
{ 19330,0x000080,   0, 0},  // Lisrythe Bloodwatch <Fence>
{ 19331,0x000080,   0, 0},  // Quartermaster Enuril <Scryer Quartermaster>
{ 19333,0x001080,   0, 0},  // Grokom Deatheye <Weaponsmith>
{ 19338,0x020000,   0, 0},  // L'lura Goldspun <Banker>
{ 19339,0x001080,   0, 0},  // Korthul <Bowyer & Gunsmith>
{ 19340,0x000030,   0, 0},  // Mi'irku Farstep <Portal Trainer>
{ 19341,0x000050, 164, 0},  // Grutah <Master Blacksmithing Trainer>
{ 19342,0x001080,   0, 0},  // Krek Cragcrush <Blacksmithing Supplies>
{ 19343,0x000080,   0, 0},  // Trop Rendlimb <General Goods>
{ 19345,0x000080,   0, 0},  // Kalara <Trade Goods>
{ 19348,0x000280,   0, 0},  // Targrom <Food & Drink>
{ 19351,0x001180,   0, 0},  // Daggle Ironshaper <Guns & Ammunition>
{ 19352,0x010080,   0, 0},  // Dreg Cloudsweeper <Innkeeper>
{ 19369,0x000050, 185, 0},  // Celie Steelwing <Cook>
{ 19370,0x001080,   0, 0},  // Ordinn Thunderfist <Hammersmith>
{ 19371,0x000080,   0, 0},  // Dalin Stouthammer <Cooking Supplies>
{ 19372,0x000080,   0, 0},  // Oran Blusterbrew <Trade Goods>
{ 19373,0x001080,   0, 0},  // Mari Stonehand <Armorsmith>
{ 19374,0x000180,   0, 0},  // Salle Sunforge <General Goods>
{ 19383,0x001080,   0, 0},  // Captured Gnome <Item Repair>
{ 19435,0x000280,   0, 0},  // Dark Cleric Malod <Healing & Sustenance>
{ 19436,0x001180,   0, 0},  // Supply Master Broog <Supplies>
{ 19450,0x000080,   0, 0},  // Pol Snowhoof <Trade Goods>
{ 19451,0x000280,   0, 0},  // Quartermaster Gorman <Rations>
{ 19452,0x001180,   0, 0},  // Quartermaster Drake <Supplies>
{ 19470,0x010080,   0, 0},  // Gholah <Innkeeper>
{ 19471,0x000080,   0, 0},  // Old Orok <Fruit & Fungus>
{ 19472,0x000080,   0, 0},  // Threlc <Fishmonger>
{ 19473,0x001080,   0, 0},  // Raiza <Throwing Weapons & Ammunition>
{ 19474,0x000080,   0, 0},  // Karnaze <General Goods>
{ 19478,0x000050, 129, 0},  // Fera Palerunner <First Aid Trainer>
{ 19479,0x001080,   0, 0},  // Orgatha <Axesmith>
{ 19495,0x010080,   0, 0},  // Innkeeper Shaunessy <Innkeeper>
{ 19497,0x000080,   0, 0},  // Caoileann <Trade Goods>
{ 19498,0x000080,   0, 0},  // Tanaide <General Goods>
{ 19499,0x001080,   0, 0},  // Cahill <Weaponsmith>
{ 19531,0x010080,   0, 0},  // Eyonix <Innkeeper>
{ 19533,0x000080,   0, 0},  // Dealer Aljaan <Trade Goods>
{ 19534,0x000080,   0, 0},  // Dealer Digriz <General Goods>
{ 19535,0x000080,   0, 0},  // Dealer Zijaad <Arcane Goods>
{ 19536,0x001080,   0, 0},  // Dealer Jadyan <Exotic Weapons>
{ 19537,0x000080,   0, 0},  // Dealer Malij <Enchanting Supplies>
{ 19538,0x000080,   0, 0},  // Dealer Senzik <Gems & Jewelcrafting Supplies>
{ 19539,0x0000d0, 755, 0},  // Jazdalaad <Master Jewelcrafting Trainer>
{ 19540,0x0000d0, 333, 0},  // Asarnan <Master Enchanting Trainer>
{ 19558,0x002000,   0, 0},  // Amilya Airheart <Wind Rider Master>
{ 19559,0x000280,   0, 0},  // Mondul <Food & Drink>
{ 19560,0x000180,   0, 0},  // Lukra <General Goods>
{ 19561,0x001180,   0, 0},  // Hagash the Blind <Bowyer>
{ 19562,0x000080,   0, 0},  // Peon Bolgar <Trade Goods>
{ 19571,0x010000,   0, 0},  // Innkeeper Remi Dodoso <Innkeeper>
{ 19572,0x000280,   0, 0},  // Gant <Food & Drink>
{ 19573,0x000080,   0, 0},  // Dash <Trade Supplies>
{ 19574,0x000080,   0, 0},  // Kizzie <General Supplies>
{ 19575,0x001080,   0, 0},  // Qiff <Engineering Supplies>
{ 19576,0x000050, 202, 0},  // Xyrol <Master Engineering Trainer>
{ 19581,0x002000,   0, 0},  // Maddix <Flight Master>
{ 19583,0x002000,   0, 0},  // Grennik <Flight Master>
{ 19617,0x000280,   0, 0},  // Boots <B.O.O.M. Master Mixologist>
{ 19625,0x000080,   0, 0},  // Alorya <General Goods>
{ 19649,0x000180,   0, 0},  // Dorni <General Goods>
{ 19661,0x000080,   0, 0},  // Viggz Shinesparked <Engineering Supplies>
{ 19662,0x001080,   0, 0},  // Aaron Hollman <Blacksmithing Supplies>
{ 19663,0x000080,   0, 0},  // Madame Ruby <Enchanting Supplies>
{ 19664,0x000280,   0, 0},  // Muffin Man Moser <Bread Merchant>
{ 19678,0x000880,   0, 0},  // Fantei <Reagents>
{ 19679,0x000080,   0, 0},  // "Slim" <Shady Dealer>
{ 19694,0x001180,   0, 0},  // Loolruna <Armorer>
{ 19718,0x000080,   0, 0},  // Provisioner Tsaalt
{ 19722,0x000080,   0, 0},  // Muheru the Weaver <Tailoring Supplies>
{ 19772,0x000080,   0, 0},  // Spirit Sage Gartok
{ 19773,0x000080,   0, 0},  // Spirit Sage Zran
{ 19775,0x000050, 755, 0},  // Kalinda <Jewelcrafting Trainer>
{ 19778,0x000050, 755, 0},  // Farii <Jewelcrafting Trainer>
{ 19836,0x000180,   0, 0},  // Mixie Farshot <Gun Merchant>
{ 19837,0x000080,   0, 0},  // Daga Ramba <Potions>
{ 19879,0x001080,   0, 0},  // Horvon the Armorer <Armorsmith>
{ 19932,0x000080,   0, 0},  // Andormu <Keepers of Time>
{ 20028,0x000080,   0, 0},  // Doba <Cooking Supplies>
{ 20080,0x000380,   0, 0},  // Galgrom <Provisioner>
{ 20081,0x000c80,   0, 0},  // Bortega <Reagents & Poison Supplies>
{ 20082,0x001080,   0, 0},  // Yarley <Armorer>
{ 20092,0x000080,   0, 0},  // Dealer Hazzin <General Provisioner>
{ 20096,0x000080,   0, 0},  // Uriku <Cooking Supplies>
{ 20097,0x000280,   0, 0},  // Nula the Butcher <Cooking Supplies>
{ 20112,0x001080,   0, 0},  // Wind Trader Tuluman <Weapon Merchant>
{ 20121,0x000480,   0, 0},  // Fingin <Poison Supplies>
{ 20124,0x000050, 164, 0},  // Kradu Grimblade <Weaponsmith Trainer>
{ 20125,0x000050, 164, 0},  // Zula Slagfury <Armorsmith Trainer>
{ 20194,0x000080,   0, 0},  // Dealer Dunar <General Provisioner>
{ 20231,0x001080,   0, 0},  // Supply Officer Pestle <General Goods & Repairs>
{ 20234,0x002000,   0, 0},  // Runetog Wildhammer <Gryphon Master>
{ 20240,0x000080,   0, 0},  // Trader Narasu <Kurenai Quartermaster>
{ 20241,0x000080,   0, 0},  // Provisioner Nasela <Mag'har Quartermaster>
{ 20242,0x001080,   0, 0},  // Karaaz <Consortium Quartermaster>
{ 20249,0x000080,   0, 0},  // Cymbre Starsong <General Goods>
{ 20250,0x000080,   0, 0},  // Rashere Pridehoof <Trade Goods>
{ 20377,0x000080,   0, 0},  // Barkeep Kelly <Bartender>
{ 20378,0x000080,   0, 0},  // Chef Jessen <Speciality Meat & Slop>
{ 20406,0x000030,   0, 2},  // Champion Cyssa Dawnrose <Paladin Trainer>
{ 20407,0x000030,   0, 7},  // Farseer Umbrua <Shaman Trainer>
{ 20463,0x001080,   0, 0},  // Apprentice Andrethan
{ 20494,0x000080,   0, 0},  // Dama Wildmane <Wind Rider Keeper>
{ 20500,0x000050, 762, 0},  // Olrokk <Riding Trainer>
{ 20510,0x000080,   0, 0},  // Brunn Flamebeard <Gryphon Keeper>
{ 20511,0x000050, 762, 0},  // Ilsa Blusterbrew <Riding Trainer>
{ 20515,0x002000,   0, 0},  // Harpax <Flight Master>
{ 20613,0x001080,   0, 0},  // Arodis Sunblade <Keeper of Sha'tari Artifacts>
{ 20616,0x001080,   0, 0},  // Asuur <Keeper of Sha'tari Artifacts>
{ 20762,0x002000,   0, 0},  // Gur'zil <Wind Rider Master>
{ 20791,0x000030,   0, 0},  // Iorioa <Portal Trainer>
{ 20807,0x000080,   0, 0},  // Scribe Saalyn <Aldor Inscriptions>
{ 20808,0x000080,   0, 0},  // Scribe Veredis <Scryer Inscriptions>
{ 20890,0x001080,   0, 0},  // Siflaed Coldhammer <Tinker>
{ 20891,0x000080,   0, 0},  // Skraa <Trade Goods>
{ 20892,0x000080,   0, 0},  // Ruogo <General Goods>
{ 20893,0x000080,   0, 0},  // Morula <Food & Drink>
{ 20914,0x000050, 762, 0},  // Aalun <Riding Trainer>
{ 20915,0x000080,   0, 0},  // Noko Moonwhisper <Reagents>
{ 20916,0x000080,   0, 0},  // Xerintha Ravenoak <Food & Drink>
{ 20917,0x001080,   0, 0},  // Zinyen Swiftstrider <Weapon Vendor>
{ 20980,0x000080,   0, 0},  // Dealer Rashaad <Exotic Creatures>
{ 20981,0x000080,   0, 0},  // Dealer Najeeb <Spare Parts>
{ 20986,0x000080,   0, 0},  // Dealer Tariq <Shady Dealer>
{ 20989,0x000080,   0, 0},  // Dealer Sadaqat <Potent Potables>
{ 21019,0x000080,   0, 0},  // Sixx <Moth Keeper>
{ 21082,0x000080,   0, 0},  // Krugash <General Goods>
{ 21083,0x000080,   0, 0},  // Erool <Trade Goods>
{ 21084,0x000080,   0, 0},  // Braagor <Butcher>
{ 21085,0x000080,   0, 0},  // Ragar <Leatherworking Supplies>
{ 21086,0x001080,   0, 0},  // Ruka <Smashing Weapons>
{ 21087,0x000050, 165, 0},  // Grikka <Master Leatherworking Trainer>
{ 21088,0x010000,   0, 0},  // Matron Varah <Innkeeper>
{ 21107,0x002000,   0, 0},  // Rip Pedalslam <Gryphon Master>
{ 21110,0x010080,   0, 0},  // Fizit "Doc" Clocktock <Innkeeper>
{ 21111,0x000080,   0, 0},  // Bembil Knockhammer <General Goods>
{ 21112,0x001080,   0, 0},  // Bossi Pentapiston <Engineering Supplies>
{ 21113,0x000080,   0, 0},  // Sassa Weldwell <Trade Goods>
{ 21145,0x000280,   0, 0},  // Little Azimi <Food Vendor>
{ 21172,0x000180,   0, 0},  // Sarinei Whitestar <General Goods>
{ 21183,0x001080,   0, 0},  // Oronok Torn-heart <Hermit & Trader>
{ 21209,0x001000,   0, 0},  // Dumphry <Master Blacksmithing Trainer>
{ 21432,0x000080,   0, 0},  // Almaador <Sha'tari Quartermaster>
{ 21474,0x001080,   0, 0},  // Coreiel <Blade Merchant>
{ 21483,0x000080,   0, 0},  // Tasaldan <Ammunition>
{ 21484,0x000080,   0, 0},  // Embelar <Food & Drink>
{ 21485,0x001080,   0, 0},  // Aldraan <Blade Merchant>
{ 21487,0x000080,   0, 0},  // Cendrii <Food & Drink>
{ 21488,0x000080,   0, 0},  // Banro <Ammunition>
{ 21493,0x000050, 202, 0},  // Kablamm Farflinger <Transportation Engineer>
{ 21494,0x000050, 202, 0},  // Smiles O'Byron <Engineer>
{ 21643,0x000080,   0, 0},  // Alurmi <Keepers of Time Quartermaster>
{ 21655,0x000080,   0, 0},  // Nakodu <Lower City Quartermaster>
{ 21732,0x020000,   0, 0},  // Nandirx <Banker>
{ 21733,0x020000,   0, 0},  // Karzo <Banker>
{ 21734,0x020000,   0, 0},  // Zixxy <Banker>
{ 21744,0x010080,   0, 0},  // Roldemar <Innkeeper>
{ 21746,0x010280,   0, 0},  // Caretaker Aluuro <Innkeeper>
{ 21766,0x002000,   0, 0},  // Alieshor <Flight Master>
{ 21905,0x000080,   0, 0},  // Veynna Dawnstar <Keeper of Sha'tari Heirlooms>
{ 21906,0x000080,   0, 0},  // Kelara <Keeper of Sha'tari Heirlooms>
{ 22099,0x001080,   0, 0},  // Wyrmcult Provisioner
{ 22208,0x000080,   0, 0},  // Nasmara Moonsong <Mooncloth Specialist>
{ 22212,0x000080,   0, 0},  // Andrion Darkspinner <Shadoweave Specialist>
{ 22213,0x000080,   0, 0},  // Gidge Spellweaver <Spellfire Specialist>
{ 22216,0x002000,   0, 0},  // Fhyn Leafshadow <Flight Master>
{ 22225,0x001080,   0, 0},  // Reagan Mancuso <Gem Vendor>
{ 22227,0x001080,   0, 0},  // Markus Scylan <Gem Vendor>
{ 22264,0x001080,   0, 0},  // Ogri'la Steelshaper <Blacksmithing Supplies>
{ 22266,0x000080,   0, 0},  // Ogri'la Grubgiver <Food Vendor>
{ 22270,0x000080,   0, 0},  // Ogri'la Merchant <General Goods>
{ 22271,0x000080,   0, 0},  // Ogri'la Trader <Trade Goods>
{ 22455,0x002000,   0, 0},  // Sky-Master Maxxor <Flight Master>
{ 22476,0x001080,   0, 0},  // Aundro <Blacksmithing Supplies>
{ 22477,0x000050, 129, 0},  // Anchorite Ensham <First Aid Trainer>
{ 22479,0x000080,   0, 0},  // Sab'aoth <Reagents & Poison Supplies>
{ 22485,0x002000,   0, 0},  // Halu <Hippogryph Master>
{ 22491,0x001080,   0, 0},  // Kerpow Blastwrench <Weapon Merchant>
{ 22922,0x010000,   0, 0},  // Innkeeper Aelerya <Innkeeper>
{ 22931,0x002000,   0, 0},  // Gorrim <Emerald Circle Flight Master>
{ 22935,0x002000,   0, 0},  // Suralais Farwind <Hippogryph Master>
{ 23007,0x001080,   0, 0},  // Paulsta'ats <Consortium Quartermaster>
{ 23103,0x000030,   0, 8},  // Derek the Undying <Mage Trainer>
{ 23110,0x000080,   0, 0},  // Ogri'la Keg King <Drink Vendor>
{ 23112,0x000080,   0, 0},  // Mingo <Reagents>
{ 23127,0x000030,   0, 7},  // Farseer Javad <Shaman Trainer>
{ 23128,0x000030,   0, 2},  // Master Pyreanor <Paladin Trainer>
{ 23143,0x000080,   0, 0},  // Horus <Innkeeper>
{ 23144,0x001080,   0, 0},  // Gug <Armorsmith>
{ 23145,0x000c80,   0, 0},  // Rumpus <Reagents & Poison Supplies>
{ 23157,0x000880,   0, 0},  // Aluyen <Reagents>
{ 23159,0x001080,   0, 0},  // Okuno <Ashtongue Deathsworn Quartermaster>
{ 23208,0x000080,   0, 0},  // Skyguard Pyrotechnician
{ 23263,0x000280,   0, 0},  // Brendan Turner <Meat Vendor>
{ 23363,0x000080,   0, 0},  // Sahaak <Keeper of Scrolls>
{ 23367,0x001080,   0, 0},  // Grella <Skyguard Quartermaster>
{ 23373,0x001080,   0, 0},  // Mortog Steamhead
{ 23381,0x001080,   0, 0},  // Tydormu <Keeper of Lost Artifacts>
{ 23428,0x000080,   0, 0},  // Jho'nass <Ogri'la Quartermaster>
{ 23437,0x000080,   0, 0},  // Indormi <Keeper of Ancient Gem Lore>
{ 23483,0x000080,   0, 0},  // Arcanist Xorith <Scryer Apothecary>
{ 23484,0x000080,   0, 0},  // Haldor the Compulsive <Aldor Apothecary>
{ 23489,0x000080,   0, 0},  // Drake Dealer Hurlunk
{ 23534,0x000030,   0, 9},  // Babagaya Shadowcleft <Warlock Trainer>
{ 23560,0x000880,   0, 0},  // Provisioner Ameenah <Reagents>
{ 23571,0x001080,   0, 0},  // Razbo Rustgear <Weapon & Armor Merchant>
{ 23573,0x000280,   0, 0},  // Krixil Slogswitch <Food & Drink>
{ 23612,0x002000,   0, 0},  // Dyslix Silvergrub <Flight Master>
{ 23699,0x000280,   0, 0},  // Kevin Browning <Coffee Aficionado>
{ 23724,0x001080,   0, 0},  // Samir
{ 23731,0x010280,   0, 0},  // Innkeeper Hazel Lagras <Innkeeper>
{ 23732,0x000480,   0, 0},  // Sorely Twitchblade <Poison Supplier>
{ 23734,0x000050, 129, 0},  // Anchorite Yazmina <Grand Master First Aid Trainer>
{ 23735,0x001080,   0, 0},  // Bartleby Armorfist <Armorsmith>
{ 23736,0x002000,   0, 0},  // Pricilla Winterwind <Gryphon Master>
{ 23737,0x000280,   0, 0},  // Coot "The Stranger" Albertson <Bartender>
{ 23748,0x000280,   0, 0},  // Kurzel <Food & Drink>
{ 23802,0x000180,   0, 0},  // Wink Sprinklesprankle <General Goods & Trade Supplies>
{ 23859,0x002000,   0, 0},  // Greer Orehammer <Gryphon Master>
{ 23862,0x001180,   0, 0},  // Finlay Fletcher <Ammunition>
{ 23896,0x000280,   0, 0},  // "Dirty" Michael Crowe <Fish Merchant>
{ 23897,0x001080,   0, 0},  // Zungam
{ 23908,0x001080,   0, 0},  // Jhet Ironbeard <Blacksmithing Supplies>
{ 23937,0x010280,   0, 0},  // Innkeeper Celeste Goodhutch <Innkeeper>
{ 23995,0x010280,   0, 0},  // Axle <Innkeeper>
{ 24028,0x001180,   0, 0},  // Talu Frosthoof <Bowyer>
{ 24032,0x002000,   0, 0},  // Celea Frozenmane <Wind Rider Master>
{ 24033,0x010280,   0, 0},  // Bori Wintertotem <Innkeeper>
{ 24052,0x001080,   0, 0},  // Eldrim Mounder <Blacksmith>
{ 24053,0x000080,   0, 0},  // Helga Rumsbane <General Goods>
{ 24054,0x000080,   0, 0},  // Balar Rumsbane <Trade Goods>
{ 24057,0x010080,   0, 0},  // Christina Daniels <Innkeeper>
{ 24061,0x002000,   0, 0},  // James Ormsby <Gryphon Master>
{ 24141,0x000180,   0, 0},  // Stephen Barone <General Goods>
{ 24147,0x000080,   0, 0},  // Tara Cooper <Trade Goods>
{ 24148,0x000480,   0, 0},  // David Marks <Poison Vendor>
{ 24149,0x010280,   0, 0},  // Basil Osgood <Innkeeper>
{ 24155,0x002000,   0, 0},  // Tobias Sarkhoff <Bat Handler>
{ 24188,0x001180,   0, 0},  // Samuel Rosemond <Provisioner>
{ 24208,0x010280,   0, 0},  // "Little" Logok <Innkeeper>
{ 24291,0x000080,   0, 0},  // Anton
{ 24313,0x000c80,   0, 0},  // Celina Summers <Reagents and Poisons>
{ 24330,0x001080,   0, 0},  // Orson Locke <Sharp Blades>
{ 24333,0x000280,   0, 0},  // Bartender Jason Goodhutch <Drinks>
{ 24341,0x000080,   0, 0},  // Barnabas Frye <Trade Goods>
{ 24342,0x010280,   0, 0},  // Timothy Holland <Innkeeper>
{ 24343,0x000280,   0, 0},  // Brock Olson <Butcher>
{ 24347,0x001080,   0, 0},  // Alexis Walker <Armorsmith>
{ 24348,0x000180,   0, 0},  // Patrick Hall <General Goods>
{ 24349,0x000c80,   0, 0},  // Jessica Evans <Reagents and Poisons>
{ 24356,0x000080,   0, 0},  // Glen Roberts <Tradesman>
{ 24357,0x000c80,   0, 0},  // Maethor Skyshadow <Poison & Reagents>
{ 24366,0x002000,   0, 0},  // Nizzle <Gryphon Master>
{ 24539,0x000080,   0, 0},  // "Silvermoon" Harry
{ 24795,0x002000,   0, 0},  // Surristrasz
{ 24834,0x000280,   0, 0},  // Galley Chief Grace <The Lady Mehley>
{ 24843,0x001880,   0, 0},  // Engineer Combs <The Lady Mehley>
{ 24851,0x002000,   0, 0},  // Kiz Coilspanner <Flight Master>
{ 24868,0x000050, 202, 0},  // Niobe Whizzlespark <Master Engineering Trainer>
{ 24993,0x000080,   0, 0},  // Galley Chief Mariss <The Moonspray>
{ 24995,0x001880,   0, 0},  // Merchant Fallel Stargazer <The Moonspray>
{ 25010,0x001880,   0, 0},  // Engineer Brightbuckle <The Bravery>
{ 25012,0x000080,   0, 0},  // Galley Chief Gathers <The Bravery>
{ 25019,0x001880,   0, 0},  // Merchant Felagunne <Feathermoon Ferry>
{ 25020,0x000080,   0, 0},  // Galley Chief Alunwea <Feathermoon Ferry>
{ 25032,0x000080,   0, 0},  // Eldara Dawnrunner <Shattered Sun Quartermaster>
{ 25051,0x001880,   0, 0},  // Merchant Frostwalker <Elune's Blessing>
{ 25052,0x000080,   0, 0},  // Galley Chief Halumvorea <Elune's Blessing>
{ 25082,0x001080,   0, 0},  // Engineer Torquespindle <The Maiden's Fancy>
{ 25089,0x000080,   0, 0},  // Galley Chief Steelbelly <The Maiden's Fancy>
{ 25099,0x000050, 202, 0},  // Jonathan Garrett <Master Engineering Trainer>
{ 25176,0x000080,   0, 0},  // Grikkin Copperspring <Arena Vendor>
{ 25195,0x001180,   0, 0},  // Marksman Bova <Specialty Ammunition Vendor>
{ 25196,0x001180,   0, 0},  // Archer Delvinar <Specialty Ammunition Vendor>
{ 25206,0x001080,   0, 0},  // Ahlurglgr <Clam Vendor>
{ 25245,0x010280,   0, 0},  // James Deacon <Innkeeper>
{ 25248,0x000280,   0, 0},  // "Salty" John Thorpe
{ 25274,0x001080,   0, 0},  // Armorer Orkuruk <Blacksmith>
{ 25277,0x000050, 202, 0},  // Chief Engineer Leveny <Grand Master Engineer>
{ 25278,0x010280,   0, 0},  // Williamson <Innkeeper>
{ 25288,0x002000,   0, 0},  // Turida Coldwind <Wind Rider Master>
{ 25314,0x001080,   0, 0},  // Archmage Berinand <Maces & Staves>
{ 25736,0x000c80,   0, 0},  // Supply Master Taz'ishi <Poison & Reagents>
{ 25976,0x000080,   0, 0},  // Theremis <Keeper of Lost Artifacts>
{ 25977,0x000080,   0, 0},  // Yrma <Transmuter of Sin'dorei Relics>
{ 26081,0x001080,   0, 0},  // High Admiral "Shelly" Jorrik <Retiree>
{ 26110,0x000080,   0, 0},  // Librarian Serrah <Food & Drink>
{ 26229,0x001080,   0, 0},  // Tiernan Anvilheart <Weapon Merchant>
{ 26374,0x000080,   0, 0},  // Maevin Farmoon <Trade Goods>
{ 26375,0x010280,   0, 0},  // Quartermaster McCarty <Innkeeper>
{ 26382,0x000c80,   0, 0},  // Balfour Blackblade <Reagents and Poisons>
{ 26388,0x000180,   0, 0},  // Veira Langdon <General Goods>
{ 26474,0x000880,   0, 0},  // Ameenah <Reagents>
{ 26484,0x000280,   0, 0},  // Hugh Glass <Merchant>
{ 26560,0x002000,   0, 0},  // Ohura <Dragonhawk Master>
{ 26564,0x000050, 164, 0},  // Borus Ironbender <Grand Master Blacksmithing Trainer>
{ 26566,0x002000,   0, 0},  // Narzun Skybreaker <Wind Rider Master>
{ 26567,0x000280,   0, 0},  // Aegalas <Food & Drink>
{ 26568,0x000480,   0, 0},  // Zebu'tan <Herbalism & Poison Supplies>
{ 26569,0x000880,   0, 0},  // Alys Vol'tyr <Reagents & Enchanting Supplies>
{ 26596,0x010280,   0, 0},  // "Charlie" Northtop <Innkeeper>
{ 26598,0x000c80,   0, 0},  // Mistie Flitterdawn <Reagents and Poisons>
{ 26599,0x001080,   0, 0},  // Willis Wobblewheel <Blacksmith>
{ 26600,0x001080,   0, 0},  // Chief Engineer Galpen Rolltie <Engineering Supplies>
{ 26602,0x002000,   0, 0},  // Kara Thricestar <Air Traffic Controller>
{ 26680,0x010280,   0, 0},  // Aiyan Coldwind <Innkeeper>
{ 26697,0x001080,   0, 0},  // Tewah Chillmane <Leather Armor Merchant>
{ 26707,0x001080,   0, 0},  // Litoko Icetotem <Armor Merchant>
{ 26709,0x010280,   0, 0},  // Pahu Frosthoof <Innkeeper>
{ 26718,0x000180,   0, 0},  // Trader Alorn <General Goods>
{ 26720,0x000080,   0, 0},  // Danook Stormwhisper <Trade Goods>
{ 26844,0x002000,   0, 0},  // Lilleth Radescu <Bat Handler>
{ 26845,0x002000,   0, 0},  // Junter Weiss <Bat Handler>
{ 26847,0x002000,   0, 0},  // Omu Spiritbreeze <Wind Rider Master>
{ 26848,0x002000,   0, 0},  // Kimbiza <Wind Rider Master>
{ 26850,0x002000,   0, 0},  // Numo Spiritbreeze <Wind Rider Master>
{ 26851,0x002000,   0, 0},  // Nethestrasz <Flight Master>
{ 26852,0x002000,   0, 0},  // Kragh <Wind Rider Master>
{ 26853,0x002000,   0, 0},  // Makki Wintergale <Wind Rider Master>
{ 26868,0x000280,   0, 0},  // Provisioner Lorkran <General Goods>
{ 26876,0x002000,   0, 0},  // Samuel Clearbook <Gryphon Master>
{ 26877,0x002000,   0, 0},  // Derek Rammel <Gryphon Master>
{ 26878,0x002000,   0, 0},  // Rodney Wells <Gryphon Master>
{ 26879,0x002000,   0, 0},  // Tomas Riverwell <Gryphon Master>
{ 26880,0x002000,   0, 0},  // Vana Grey <Gryphon Master>
{ 26881,0x002000,   0, 0},  // Palena Silvercloud <Hippogryph Master>
{ 26898,0x001080,   0, 0},  // Troz
{ 26900,0x000c80,   0, 0},  // Tinky Stabberson <Poison & Reagent Supplies>
{ 26901,0x001180,   0, 0},  // Torik <Ammunition>
{ 26903,0x000050, 171, 0},  // Lanolis Dewdrop <Grand Master Alchemy Trainer>
{ 26904,0x000050, 164, 0},  // Rosina Rivet <Grand Master Blacksmithing Trainer>
{ 26905,0x000050, 185, 0},  // Brom Brewbaster <Grand Master Cooking Trainer>
{ 26906,0x000050, 333, 0},  // Elizabeth Jackson <Grand Master Enchanting Trainer>
{ 26907,0x000050, 202, 0},  // Tisha Longbridge <Grand Master Engineering Trainer>
{ 26908,0x000880,   0, 0},  // Helen Fairchild <Reagents>
{ 26909,0x000050, 356, 0},  // Byron Welwick <Grand Master Fishing Trainer>
{ 26910,0x000050, 182, 0},  // Fayin Whisperleaf <Grand Master Herbalism Trainer>
{ 26911,0x000050, 165, 0},  // Bernadette Dexter <Grand Master Leatherworking Trainer>
{ 26912,0x000050, 186, 0},  // Grumbol Stoutpick <Grand Master Mining Trainer>
{ 26913,0x000050, 393, 0},  // Frederic Burrhus <Grand Master Skinning Trainer>
{ 26914,0x000050, 197, 0},  // Benjamin Clegg <Grand Master Tailoring Trainer>
{ 26915,0x000050, 755, 0},  // Ounhulo <Grand Master Jewelcrafting Trainer>
{ 26916,0x0008d0, 773, 0},  // Mindri Dinkles <Grand Master Inscription Trainer>
{ 26934,0x001080,   0, 0},  // Everett McGill <Explorers' League Provisioner>
{ 26936,0x000180,   0, 0},  // Chaska Frosthoof <General Goods>
{ 26938,0x000080,   0, 0},  // Groll <General Goods>
{ 26939,0x000080,   0, 0},  // Koro the Wanderer <Trade Goods>
{ 26941,0x000080,   0, 0},  // Brokkan Bear-Arms <Trade Goods>
{ 26945,0x000080,   0, 0},  // Zend'li Venomtusk <Poison Supplies>
{ 26950,0x000c80,   0, 0},  // Sanut Swiftspear <Reagents and Poisons>
{ 26951,0x000050, 171, 0},  // Wilhelmina Renel <Grand Master Alchemy Trainer>
{ 26952,0x000050, 164, 0},  // Kristen Smythe <Grand Master Blacksmithing Trainer>
{ 26953,0x000050, 185, 0},  // Thomas Kolichio <Grand Master Cooking Trainer>
{ 26954,0x000050, 333, 0},  // Emil Autumn <Grand Master Enchanting Trainer>
{ 26955,0x000050, 202, 0},  // Jamesina Watterly <Grand Master Engineering Trainer>
{ 26956,0x000050, 129, 0},  // Sally Tompkins <Grand Master First Aid Trainer>
{ 26957,0x000050, 356, 0},  // Angelina Soren <Grand Master Fishing Trainer>
{ 26958,0x000050, 182, 0},  // Marjory Kains <Grand Master Herbalism Trainer>
{ 26959,0x0008d0, 773, 0},  // Booker Kells <Grand Master Inscription Trainer>
{ 26960,0x000050, 755, 0},  // Carter Tiffens <Grand Master Jewelcrafting Trainer>
{ 26961,0x000050, 165, 0},  // Gunter Hansen <Grand Master Leatherworking Trainer>
{ 26962,0x000050, 186, 0},  // Jonathan Lewis <Grand Master Mining Trainer>
{ 26963,0x000050, 393, 0},  // Roberta Jacks <Grand Master Skinning Trainer>
{ 26964,0x000050, 197, 0},  // Alexandra McQueen <Grand Master Tailoring Trainer>
{ 26968,0x000080,   0, 0},  // Drikka <Reagents>
{ 26969,0x000050, 197, 0},  // Raenah <Grand Master Tailoring Trainer>
{ 26972,0x000050, 185, 0},  // Orn Tenderhoof <Grand Master Cooking Trainer>
{ 26974,0x000050, 182, 0},  // Tansy Wildmane <Grand Master Herbalism Trainer>
{ 26975,0x000050, 171, 0},  // Arthur Henslowe <Grand Master Alchemy Trainer>
{ 26976,0x000050, 186, 0},  // Brunna Ironaxe <Grand Master Mining Trainer>
{ 26977,0x0008d0, 773, 0},  // Adelene Sunlance <Grand Master Inscription Trainer>
{ 26980,0x000050, 333, 0},  // Eorain Dawnstrike <Grand Master Enchanting Trainer>
{ 26981,0x000050, 164, 0},  // Crog Steelspine <Grand Master Blacksmithing Trainer>
{ 26982,0x000050, 755, 0},  // Geba'li <Grand Master Jewelcrafting Trainer>
{ 26984,0x000880,   0, 0},  // Stephan Franks <Reagent Supplies>
{ 26985,0x010000,   0, 0},  // Barracks Master Harga <Innkeeper>
{ 26986,0x000050, 393, 0},  // Tiponi Stormwhisper <Grand Master Skinning Trainer>
{ 26987,0x000050, 171, 0},  // Falorn Nightwhisper <Grand Master Alchemy Trainer>
{ 26988,0x000050, 164, 0},  // Argo Strongstout <Grand Master Blacksmithing Trainer>
{ 26989,0x000050, 185, 0},  // Rollick MacKreel <Grand Master Cooking Trainer>
{ 26990,0x000050, 333, 0},  // Alexis Marlowe <Grand Master Enchanting Trainer>
{ 26991,0x000050, 202, 0},  // Sock Brightbolt <Grand Master Engineering Trainer>
{ 26992,0x000050, 129, 0},  // Brynna Wilson <Grand Master First Aid Trainer>
{ 26993,0x000050, 356, 0},  // Old Man Robert <Grand Master Fishing Trainer>
{ 26994,0x000050, 182, 0},  // Kirea Moondancer <Grand Master Herbalism Trainer>
{ 26995,0x0008d0, 773, 0},  // Tink Brightbolt <Grand Master Inscription Trainer>
{ 26996,0x000050, 165, 0},  // Awan Iceborn <Grand Master Leatherworker>
{ 26997,0x000050, 755, 0},  // Alestos <Grand Master Jewelcrafting Trainer>
{ 26998,0x000050, 165, 0},  // Rosemary Bovard <Grand Master Leatherworking Trainer>
{ 26999,0x000050, 186, 0},  // Fendrig Redbeard <Grand Master Mining Trainer>
{ 27000,0x000050, 393, 0},  // Trapper Jack <Grand Master Skinning Trainer>
{ 27001,0x000050, 197, 0},  // Darin Goodstitch <Grand Master Tailoring Trainer>
{ 27011,0x001180,   0, 0},  // Broff Bombast <General Goods>
{ 27012,0x000080,   0, 0},  // Beem Goldsprocket <Trade Goods>
{ 27019,0x001080,   0, 0},  // Siegesmith Gulda <Blacksmithing Supplies>
{ 27021,0x000080,   0, 0},  // Tradesman Kontor <Trade Supplies>
{ 27022,0x000180,   0, 0},  // Afha <General Goods>
{ 27023,0x000050, 171, 0},  // Apothecary Bressa <Master Alchemy Trainer>
{ 27025,0x000180,   0, 0},  // Harold Haggler <General Goods>
{ 27026,0x000080,   0, 0},  // Rohesia Werner <Trade Supplies>
{ 27027,0x010280,   0, 0},  // Mrs. Winterby <Innkeeper>
{ 27029,0x000050, 171, 0},  // Apothecary Wormwick <Master Alchemy Trainer>
{ 27030,0x000880,   0, 0},  // Bradley Towns <Reagents & Enchanting Supplies>
{ 27031,0x000480,   0, 0},  // Apothecary Rose <Alchemy & Poison Supplies>
{ 27032,0x000280,   0, 0},  // Lovely Liddia <Mushroom Vendor>
{ 27034,0x000050, 164, 0},  // Josric Fame <Grand Master Blacksmithing Trainer>
{ 27037,0x001080,   0, 0},  // Hidetrader Jun'ik <Leather Goods>
{ 27038,0x000480,   0, 0},  // Drolfy <Alchemy & Poison Supplies>
{ 27039,0x000880,   0, 0},  // Lexey Brevig <Reagents & Enchanting Supplies>
{ 27041,0x000180,   0, 0},  // Fanny McLumpkins <General Goods>
{ 27042,0x010280,   0, 0},  // Illusia Lune <Innkeeper>
{ 27043,0x000080,   0, 0},  // Trixy Trixerton <Trade Supplies>
{ 27044,0x000280,   0, 0},  // Ordal McLumpkins <Food & Drink>
{ 27045,0x001080,   0, 0},  // Master Smith Devin Brevig <Blacksmithing Supplies>
{ 27046,0x002000,   0, 0},  // Warmage Adami <Flight Master>
{ 27051,0x000180,   0, 0},  // Doldaen <General Goods>
{ 27052,0x010280,   0, 0},  // Naohain <Innkeeper>
{ 27053,0x000480,   0, 0},  // Lanus Longleaf <Herbalism & Poison Supplies>
{ 27054,0x000880,   0, 0},  // Modoru <Reagents & Enchanting Supplies>
{ 27055,0x001180,   0, 0},  // Sentinel Amberline <Bowyer>
{ 27057,0x000080,   0, 0},  // Saramer Whitewillow <Trade Supplies>
{ 27058,0x000180,   0, 0},  // Korag Keeneye <General Goods>
{ 27062,0x001080,   0, 0},  // Brom Armstrong <Blacksmithing Supplies>
{ 27063,0x000080,   0, 0},  // Vrok <Trade Goods>
{ 27066,0x010280,   0, 0},  // Jennifer Bell <Innkeeper>
{ 27067,0x001080,   0, 0},  // Drigoth <Armorer>
{ 27069,0x010280,   0, 0},  // Matron Magah <Innkeeper>
{ 27070,0x000080,   0, 0},  // Lisa Philbrook <Trade Goods>
{ 27071,0x000180,   0, 0},  // Benjamin Jacobs <General Goods>
{ 27088,0x000880,   0, 0},  // Yolanda Haymer <Reagent Supplies>
{ 27089,0x000480,   0, 0},  // Saffron Reynolds <Poison Supplies>
{ 27125,0x010280,   0, 0},  // Barracks Master Rhekku <Innkeeper>
{ 27132,0x000080,   0, 0},  // Sani'i <Trade Goods>
{ 27133,0x000c80,   0, 0},  // Seer Yagnar <Poison & Reagents>
{ 27134,0x001080,   0, 0},  // Smith Prigka <Armorer>
{ 27137,0x000280,   0, 0},  // Apprentice Fraser <Food & Drink>
{ 27138,0x000880,   0, 0},  // Apprentice Rosen <Reagent Supplies>
{ 27139,0x001080,   0, 0},  // Librarian Whitley <Wands>
{ 27140,0x000080,   0, 0},  // Librarian Andersen <Alchemy Supplies>
{ 27141,0x000080,   0, 0},  // Librarian Hamilton <Herbalism Supplies>
{ 27142,0x000080,   0, 0},  // Librarian Jeffers <Jewelcrafting Supplies>
{ 27143,0x000880,   0, 0},  // Librarian Ingram <Inscription Supplies>
{ 27144,0x000280,   0, 0},  // Noatak <Fishmonger>
{ 27145,0x000180,   0, 0},  // Tipvigut <General Goods>
{ 27146,0x000080,   0, 0},  // Uukkarnit <Trade Goods>
{ 27147,0x000080,   0, 0},  // Librarian Erickson <Enchanting Supplies>
{ 27148,0x010280,   0, 0},  // Caregiver Iqniq <Innkeeper>
{ 27149,0x000c80,   0, 0},  // Arrluk <Poison & Reagents>
{ 27151,0x001080,   0, 0},  // Deniigi <Spearcrafter>
{ 27174,0x010280,   0, 0},  // Caregiver Mumik <Innkeeper>
{ 27176,0x000c80,   0, 0},  // Mystic Makittuq <Poison & Reagents>
{ 27181,0x000280,   0, 0},  // Qannik <Fishmonger>
{ 27182,0x000180,   0, 0},  // Takubvik <General Goods>
{ 27184,0x000080,   0, 0},  // Imnek <Trade Goods>
{ 27185,0x001080,   0, 0},  // Kuk'uq <Spearcrafter>
{ 27186,0x000c80,   0, 0},  // Oogrooq <Poison & Reagents>
{ 27187,0x010280,   0, 0},  // Caregiver Poallu <Innkeeper>
{ 27188,0x001080,   0, 0},  // Tonraq <Spearcrafter>
{ 27190,0x000280,   0, 0},  // Tupit <Fishmonger>
{ 27193,0x000180,   0, 0},  // Alornerk <General Goods>
{ 27195,0x000080,   0, 0},  // Tarralikitak <Trade Goods>
{ 27267,0x001080,   0, 0},  // Quartermaster Bartlett <Blacksmithing Supplies>
{ 27291,0x000080,   0, 0},  // Initiate Mehrtens <Test Goods>
{ 27344,0x002000,   0, 0},  // Bat Handler Adeline <Bat Handler>
{ 27703,0x000030,   0, 0},  // Ysuria <Portal Trainer>
{ 27704,0x000030,   0, 8},  // Horace Alder <Mage Trainer>
{ 27705,0x000030,   0, 0},  // Lorrin Foxfire <Portal Trainer>
{ 27711,0x001080,   0, 0},  // Technician Halmaha <Engineering Supplies>
{ 27935,0x000880,   0, 0},  // Ferithos <Reagent Supplies>
{ 27938,0x000080,   0, 0},  // Trizormu <Trade Goods>
{ 27940,0x000180,   0, 0},  // Lethecus <General Goods>
{ 27943,0x001080,   0, 0},  // Dalormi <Wands>
{ 27950,0x010280,   0, 0},  // Demestrasz <Innkeeper>
{ 28037,0x002000,   0, 0},  // The Spirit of Gnomeregan <Flight Master>
{ 28038,0x010280,   0, 0},  // Purser Boulian <Innkeeper>
{ 28040,0x001180,   0, 0},  // Mardan Thunderhoof <Gunsmith>
{ 28046,0x000280,   0, 0},  // Korg the Cleaver <Ship's Cook>
{ 28195,0x002000,   0, 0},  // Bilko Driftspark <Flight Master>
{ 28196,0x002000,   0, 0},  // Cid Flounderfix <Flight Master>
{ 28197,0x002000,   0, 0},  // Kip Trawlskip <Flight Master>
{ 28344,0x001080,   0, 0},  // Blazzle <Blacksmithing Supplies>
{ 28347,0x000480,   0, 0},  // Miles Sidney <Poison Supplies>
{ 28471,0x000030,   0, 0},  // Lady Alistra
{ 28472,0x000030,   0, 0},  // Lord Thorval
{ 28474,0x000030,   0, 0},  // Amal'thazad
{ 28500,0x001080,   0, 0},  // Master Siegesmith Corvus
{ 28574,0x002000,   0, 0},  // Marvin Wobblesprocket <Flight Master>
{ 28589,0x000080,   0, 0},  // Gristlegut
{ 28615,0x002000,   0, 0},  // Baneflight <Flight Master>
{ 28618,0x002000,   0, 0},  // Danica Saint <Flight Master>
{ 28623,0x002000,   0, 0},  // Gurric <Flight Master>
{ 28624,0x002000,   0, 0},  // Maaka <Flight Master>
{ 28674,0x002000,   0, 0},  // Aludane Whitecloud <Flight Master>
{ 28675,0x020000,   0, 0},  // Teller Rames <Banker>
{ 28676,0x020000,   0, 0},  // Teller Althiellis <Banker>
{ 28677,0x020000,   0, 0},  // Teller Hanners <Banker>
{ 28682,0x000280,   0, 0},  // Inzi Charmlight <Barmaid>
{ 28685,0x000280,   0, 0},  // Narisa Redgold <Barmaid>
{ 28686,0x010080,   0, 0},  // Caliel Brightwillow <Assistant Innkeeper>
{ 28687,0x010280,   0, 0},  // Amisi Azuregaze <Innkeeper>
{ 28691,0x000180,   0, 0},  // Susana Averoy <General Goods>
{ 28692,0x000080,   0, 0},  // "Red" Jack Findle <Trade Supplies>
{ 28693,0x000050, 333, 0},  // Enchanter Nalthanis <Grand Master Enchanting Trainer>
{ 28694,0x000050, 164, 0},  // Alard Schmied <Grand Master Blacksmithing Trainer>
{ 28696,0x000050, 393, 0},  // Derik Marks <Grand Master Skinning Trainer>
{ 28697,0x000050, 202, 0},  // Timofey Oshenko <Grand Master Engineering Trainer>
{ 28698,0x000050, 186, 0},  // Jedidiah Handers <Grand Master Mining Trainer>
{ 28699,0x000050, 197, 0},  // Charles Worth <Grand Master Tailoring Trainer>
{ 28700,0x000050, 165, 0},  // Diane Cannings <Grand Master Leatherworking Trainer>
{ 28701,0x0000d0, 755, 0},  // Timothy Jones <Grand Master Jewelcrafting Trainer>
{ 28702,0x000050, 773, 0},  // Professor Pallin <Grand Master Inscription Trainer>
{ 28703,0x000050, 171, 0},  // Linzy Blackbolt <Grand Master Alchemy Trainer>
{ 28704,0x000050, 182, 0},  // Dorothy Egan <Grand Master Herbalism Trainer>
{ 28705,0x000050, 185, 0},  // Katherine Lee <Grand Master Cooking Trainer>
{ 28706,0x000050, 129, 0},  // Olisarra the Kind <Grand Master First Aid Trainer>
{ 28707,0x000080,   0, 0},  // Angelo Pescatore <Fishing Supplies>
{ 28714,0x000880,   0, 0},  // Ildine Sorrowspear <Enchanting Supplies>
{ 28715,0x000080,   0, 0},  // Endora Moorehead <Magical Goods>
{ 28716,0x001080,   0, 0},  // Palja Amboss <Blacksmithing Supplies>
{ 28718,0x000080,   0, 0},  // Ranid Glowergold <Leatherworking & Skinning Supplies>
{ 28721,0x000080,   0, 0},  // Tiffany Cartier <Jewelcrafting Supplies>
{ 28722,0x001080,   0, 0},  // Bryan Landers <Engineering Supplies>
{ 28723,0x000880,   0, 0},  // Larana Drome <Inscription Supplies>
{ 28725,0x000080,   0, 0},  // Patricia Egan <Alchemy Supplies>
{ 28726,0x000080,   0, 0},  // Dominique Stefano <Tailoring Supplies>
{ 28727,0x000080,   0, 0},  // Edward Egan <Herbalism Supplies>
{ 28728,0x000080,   0, 0},  // Dorian Fines <Mining Supplies>
{ 28742,0x0002d0, 356, 0},  // Marcia Chase <Grand Master Fishing Trainer & Supplies>
{ 28746,0x000050,   0, 0},  // Pilot Vic <Cold Weather Flying Trainer>
{ 28760,0x001080,   0, 0},  // Hargus the Gimp <Supplies>
{ 28776,0x000080,   0, 0},  // Elizabeth Ross <Tabard Vendor>
{ 28791,0x010280,   0, 0},  // Marissa Everwatch <Innkeeper>
{ 28792,0x000180,   0, 0},  // Noggra <General Goods>
{ 28794,0x000080,   0, 0},  // Kevin Weaver <Trade Goods>
{ 28796,0x001080,   0, 0},  // Arlen Brighthammer <Blacksmith>
{ 28797,0x001080,   0, 0},  // Haley Copperturn <Engineering Supplies>
{ 28798,0x000080,   0, 0},  // Claudia Bloodraven <Leatherworking & Skinning Supplies>
{ 28799,0x000080,   0, 0},  // Alanna <Jewelcafting Supplies>
{ 28800,0x001180,   0, 0},  // Aidan Steeleye <Ammunition>
{ 28806,0x000080,   0, 0},  // Chad Carter <Trade Goods>
{ 28807,0x000180,   0, 0},  // Amarante <General Goods>
{ 28809,0x000880,   0, 0},  // Vincent Huber <Reagents>
{ 28810,0x000080,   0, 0},  // Lessien <Tailoring Supplies>
{ 28811,0x000280,   0, 0},  // Brady Ironcrock <Cooking Supplies>
{ 28812,0x000280,   0, 0},  // Lapu Stormhorn <Fish & Fishing Supplies>
{ 28813,0x001180,   0, 0},  // Elizabeth Hollingsworth <Ammunition>
{ 28827,0x000880,   0, 0},  // Co'man <Reagents>
{ 28828,0x000080,   0, 0},  // Ansari <Herbalism Supplies>
{ 28829,0x000080,   0, 0},  // Saree <Alchemy Supplies>
{ 28830,0x000180,   0, 0},  // Ra'wiri <General Goods>
{ 28831,0x000080,   0, 0},  // Yamuna <Trade Supplies>
{ 28832,0x000480,   0, 0},  // Chin'ika <Poison Supplier>
{ 28855,0x001080,   0, 0},  // Ba'kari <Maces & Staves>
{ 28866,0x000080,   0, 0},  // Corrosion <Alchemy Supplies>
{ 28867,0x000180,   0, 0},  // Spiked <General Goods>
{ 28868,0x000080,   0, 0},  // Mulch <Herbalism Supplies>
{ 28869,0x000480,   0, 0},  // Deathdrip <Poison Supplier>
{ 28870,0x000880,   0, 0},  // Corpsedust <Reagents>
{ 28871,0x001080,   0, 0},  // Scabbard <Blades>
{ 28872,0x000080,   0, 0},  // Squirmworm <Trade Supplies>
{ 28951,0x000080,   0, 0},  // Breanni <Pet Supplies>
{ 28956,0x000030,   0, 8},  // Warcaster Fanoraithe <Mage Trainer>
{ 28958,0x000030,   0, 8},  // Jessa Weaver <Mage Trainer>
{ 28989,0x001080,   0, 0},  // Aemara <Bowyer>
{ 28990,0x001080,   0, 0},  // Anthony Durain <Shield Merchant>
{ 28991,0x001080,   0, 0},  // Valaden Silverblade <Sword Merchant>
{ 28992,0x001080,   0, 0},  // Valerie Langrom <Leather Armor Merchant>
{ 28993,0x000080,   0, 0},  // Aerith Primrose <Flower Vendor>
{ 28994,0x001080,   0, 0},  // Abra Cadabra <Wands>
{ 28995,0x001080,   0, 0},  // Paldesse <Cloth Armor Merchant>
{ 28997,0x001080,   0, 0},  // Griselda Hunderland <Plate Armor Merchant>
{ 29014,0x001180,   0, 0},  // Grakjek <Fletcher>
{ 29015,0x000c80,   0, 0},  // Shaman Partak <Reagents & Poisons>
{ 29035,0x001180,   0, 0},  // Loomee <Shinies Collector>
{ 29037,0x000c80,   0, 0},  // Soo-jam <Reagents & Poisons>
{ 29049,0x000280,   0, 0},  // Arille Azuregaze <Bartender>
{ 29121,0x000280,   0, 0},  // Soo-yum <Food & Drink>
{ 29122,0x000280,   0, 0},  // Rarkag <Food & Drink>
{ 29156,0x000030,   0, 0},  // Archmage Celindra <Portal Trainer>
{ 29194,0x000030,   0, 0},  // Amal'thazad
{ 29195,0x000030,   0, 0},  // Lady Alistra
{ 29196,0x000030,   0, 0},  // Lord Thorval
{ 29203,0x000880,   0, 0},  // Alchemist Karloff <Corpse Dust Vendor>
{ 29205,0x000280,   0, 0},  // Corpulous <Food & Drink>
{ 29207,0x000180,   0, 0},  // Gangrenus <General Goods>
{ 29208,0x000080,   0, 0},  // Fester <Trade Goods>
{ 29233,0x000050, 129, 0},  // Nurse Applewood <Grand Master First Aid Trainer>
{ 29244,0x000080,   0, 0},  // Jesse Masters <Butcher>
{ 29261,0x000080,   0, 0},  // Windle Sparkshine
{ 29270,0x000080,   0, 0},  // Aspen Grove Trader <Trade Goods>
{ 29275,0x000080,   0, 0},  // Aspen Grove Supplier <Food & Drink>
{ 29277,0x000080,   0, 0},  // Datalore Smallsphere <Trade Goods>
{ 29282,0x020000,   0, 0},  // Paymaster Alstein <Banker>
{ 29288,0x001880,   0, 0},  // Engineer Kurtis Paddock <The Assurance>
{ 29291,0x000080,   0, 0},  // Galley Chief Paul Kubit <The Assurance>
{ 29476,0x001080,   0, 0},  // Dagna Flintlock <Gunsmith>
{ 29478,0x000080,   0, 0},  // Jepetto Joybuzz <Toymaker>
{ 29480,0x002000,   0, 0},  // Grimwing <Flight Master>
{ 29491,0x001080,   0, 0},  // Karandonna <Clothier>
{ 29493,0x001180,   0, 0},  // Jarold Puller <Specialty Ammunition>
{ 29494,0x001080,   0, 0},  // Shen Kang Cheng <Staves>
{ 29495,0x000080,   0, 0},  // Norvin Alderman <Cloaks>
{ 29496,0x001080,   0, 0},  // Kerta the Bold <Axe & Polearm Merchant>
{ 29497,0x001080,   0, 0},  // Walther Whiteford <Mace Merchant>
{ 29499,0x001080,   0, 0},  // Bartram Haller <Dagger & Fist Weapon Merchant>
{ 29505,0x000050, 164, 0},  // Imindril Spearsong <Weaponsmithing Trainer>
{ 29506,0x000050, 164, 0},  // Orland Schaeffer <Armorsmithing Trainer>
{ 29507,0x000050, 165, 0},  // Manfred Staller <Elemental Leatherworking Trainer>
{ 29508,0x000050, 165, 0},  // Andellion <Dragonscale Leatherworking Trainer>
{ 29509,0x000050, 165, 0},  // Namha Moonwater <Tribal Leatherworking Trainer>
{ 29510,0x000080,   0, 0},  // Linna Bruder <Shadoweave Tailoring Specialist>
{ 29511,0x000080,   0, 0},  // Lalla Brightweave <Spellfire Tailoring Specialist>
{ 29512,0x000080,   0, 0},  // Ainderu Summerleaf <Mooncloth Tailoring Specialist>
{ 29513,0x000050, 202, 0},  // Didi the Wrench <Goblin Engineering Trainer>
{ 29514,0x000050, 202, 0},  // Findle Whistlesteam <Gnome Engineering Trainer>
{ 29523,0x001080,   0, 0},  // Bragund Brightlink <Mail Armor Merchant>
{ 29527,0x000080,   0, 0},  // Orton Bennet <Offhand Offerings>
{ 29528,0x000080,   0, 0},  // Debbi Moore <Trinkets & Charms>
{ 29529,0x000080,   0, 0},  // Ninsianna <Relics>
{ 29530,0x020000,   0, 0},  // Binzik Goldbook <Banker>
{ 29532,0x010280,   0, 0},  // Ajay Green <Innkeeper>
{ 29535,0x000480,   0, 0},  // Alchemist Cinesra <Poison Vendor>
{ 29537,0x000880,   0, 0},  // Darahir <Reagents & Magical Goods>
{ 29538,0x001080,   0, 0},  // Hexil Garrot <Tools of the Trade>
{ 29547,0x000280,   0, 0},  // Applebough <Fruit Vendor>
{ 29548,0x000080,   0, 0},  // Aimee <Pie, Pastry & Cakes>
{ 29583,0x010280,   0, 0},  // Pan'ya <Innkeeper>
{ 29587,0x000080,   0, 0},  // Dread Commander Thalanor
{ 29628,0x000080,   0, 0},  // Angelique Butler <First Aid Supplies>
{ 29631,0x000050, 185, 0},  // Awilo Lon'gomba <Grand Master Cooking Trainer>
{ 29636,0x000880,   0, 0},  // Hagatha Moorehead <Reagent Merchant>
{ 29688,0x001080,   0, 0},  // Engineer Reed
{ 29702,0x001080,   0, 0},  // Chameli Banaphash <Haberdasher>
{ 29703,0x001080,   0, 0},  // Sheddle Glossgleam <Cobbler>
{ 29714,0x000280,   0, 0},  // Lucian Trias <Bread & Cheese Vendor>
{ 29715,0x000280,   0, 0},  // Fialla Sweetberry <Food & Drink>
{ 29716,0x000080,   0, 0},  // Clockwork Assistant <Jepetto's Companion>
{ 29721,0x002000,   0, 0},  // Skizzle Slickslide <Flight Master>
{ 29744,0x000080,   0, 0},  // Rork Sharpchin <Explorers' League>
{ 29750,0x002000,   0, 0},  // Faldorf Bitterchill <Flight Master>
{ 29757,0x002000,   0, 0},  // Kabarg Windtamer <Flight Master>
{ 29762,0x002000,   0, 0},  // Hyeyoung Parka <Wind Rider Master>
{ 29904,0x010280,   0, 0},  // Smilin' Slirk Brassknob <Innkeeper>
{ 29905,0x000280,   0, 0},  // Grillix Bonesaw <Butcher>
{ 29907,0x001080,   0, 0},  // Xark Bolthammer <Blacksmithing & Engineering Supplies>
{ 29908,0x000180,   0, 0},  // Plip Fatpurse <General Goods>
{ 29909,0x000c80,   0, 0},  // Nilika Blastbeaker <Poisons, Reagents & Alchemical Supplies>
{ 29922,0x000c80,   0, 0},  // Corig the Cunning <Poisons & Reagents>
{ 29923,0x001080,   0, 0},  // Dagni Oregleam <Blacksmithing Supplies>
{ 29924,0x000050, 164, 0},  // Brandig <Grand Master Blacksmithing Trainer>
{ 29925,0x000180,   0, 0},  // Rutner Steelpick <General Goods>
{ 29926,0x010280,   0, 0},  // Gunda Boldhammer <Innkeeper>
{ 29944,0x010080,   0, 0},  // Peon Gakra <Innkeeper & Supplies>
{ 29945,0x001080,   0, 0},  // Marksman Udabu <Gunsmith>
{ 29947,0x000080,   0, 0},  // Apothecary Maple <Poisons & Reagents>
{ 29950,0x002000,   0, 0},  // Breck Rockbrow <Flight Master>
{ 29951,0x002000,   0, 0},  // Shavalius the Fancy <Flight Master>
{ 29961,0x000c80,   0, 0},  // Brangrimm <Poisons & Reagents>
{ 29962,0x000180,   0, 0},  // Horgoru the Collector <General Goods>
{ 29963,0x010280,   0, 0},  // Magorn <Innkeeper>
{ 29964,0x001080,   0, 0},  // Dargum Hammerdeep <Blacksmithing Supplies>
{ 29968,0x000c80,   0, 0},  // Hapanu Coldwind <Poisons & Reagents>
{ 29969,0x001080,   0, 0},  // Ontak <Blacksmithing Supplies>
{ 29970,0x000180,   0, 0},  // Danho Farcloud <General Goods>
{ 29971,0x010280,   0, 0},  // Wabada Whiteflower <Innkeeper>
{ 30005,0x010080,   0, 0},  // Lodge-Matron Embla <Innkeeper>
{ 30006,0x001080,   0, 0},  // Warsmith Sigfinna <Arms & Armor>
{ 30010,0x000c80,   0, 0},  // Fylla Ingadottir <Poisons & Reagents>
{ 30011,0x000180,   0, 0},  // Sigdis the Trader <General Goods>
{ 30067,0x001080,   0, 0},  // Initiate Claget <Blades>
{ 30069,0x000080,   0, 0},  // Initiate Roderick <Poisons & Reagents>
{ 30070,0x000080,   0, 0},  // Initiate Gahark <General Goods>
{ 30098,0x000280,   0, 0},  // Amphitheater Vendor <Snacks>
{ 30239,0x000c80,   0, 0},  // Alanura Firecloud <Poisons & Reagents>
{ 30241,0x001080,   0, 0},  // Lanudal Silverhart <Blacksmithing Supplies>
{ 30244,0x000c80,   0, 0},  // Miura Brightweaver <Poisons & Reagents>
{ 30253,0x001080,   0, 0},  // Felindel Sunhammer <Blacksmithing Supplies>
{ 30254,0x000280,   0, 0},  // Marisalira <Food & Drink>
{ 30255,0x000280,   0, 0},  // Aniduria <Food & Drink>
{ 30256,0x000180,   0, 0},  // Scout Ordimbral <General Goods>
{ 30257,0x000180,   0, 0},  // Scout Yribria <General Goods>
{ 30269,0x002000,   0, 0},  // Skymaster Baeric <Dragonhawk Master>
{ 30271,0x002000,   0, 0},  // Galendror Whitewing <Hippogryph Master>
{ 30306,0x000480,   0, 0},  // Bileblow <Poisons>
{ 30307,0x000880,   0, 0},  // Sarhule the Risen <Reagents>
{ 30308,0x010000,   0, 0},  // Initiate Brenners <Innkeeper>
{ 30309,0x000280,   0, 0},  // Shambles <Food & Drink>
{ 30310,0x000180,   0, 0},  // Boltskull <General Goods>
{ 30311,0x000080,   0, 0},  // Mangled <Trade Supplies>
{ 30314,0x002000,   0, 0},  // Morlia Doomwing <Flight Master>
{ 30336,0x001080,   0, 0},  // Runesmith Balehammer <Blacksmithing Supplies>
{ 30345,0x001880,   0, 0},  // Chief Engineer Boltwrench
{ 30346,0x000280,   0, 0},  // Galley Chief Dolinger
{ 30431,0x000080,   0, 0},  // Veteran Crusader Aliocha Segard <Argent Crusade Quartermaster>
{ 30433,0x002000,   0, 0},  // Aedan Moran <Flight Master>
{ 30434,0x001080,   0, 0},  // Durik Bronzebomb <Engineering Supplies>
{ 30436,0x001080,   0, 0},  // Halig Fireforge <Blacksmithing Supplies>
{ 30437,0x000180,   0, 0},  // Crusader Marisse <Ammunition>
{ 30438,0x000c80,   0, 0},  // Supply Officer Thalmers <Poisons, Reagents & Trade Supplies>
{ 30439,0x000080,   0, 0},  // Sister Colleen Tulley <Food & Drink>
{ 30472,0x000080,   0, 0},  // Olut Alegut
{ 30569,0x002000,   0, 0},  // Rafae <Flight Master>
{ 30572,0x001180,   0, 0},  // Nofal <Ammunition>
{ 30604,0x020000,   0, 0},  // Teller Almeida <Banker>
{ 30605,0x020000,   0, 0},  // Teller Gee <Banker>
{ 30606,0x020000,   0, 0},  // Paymaster Chang <Banker>
{ 30607,0x020000,   0, 0},  // Teller Plushner <Banker>
{ 30608,0x020000,   0, 0},  // Paymaster Amadi <Banker>
{ 30706,0x000050, 773, 0},  // Jo'mah <Inscription Trainer>
{ 30709,0x000050, 773, 0},  // Poshken Hardbinder <Inscription Trainer>
{ 30710,0x000050, 773, 0},  // Zantasia <Inscription Trainer>
{ 30711,0x000050, 773, 0},  // Margaux Parchley <Inscription Trainer>
{ 30713,0x000050, 773, 0},  // Catarina Stanford <Inscription Trainer>
{ 30715,0x000050, 773, 0},  // Feyden Darkin <Inscription Trainer>
{ 30716,0x000050, 773, 0},  // Thoth <Inscription Trainer>
{ 30717,0x000050, 773, 0},  // Elise Brightletter <Inscription Trainer>
{ 30721,0x000050, 773, 0},  // Michael Schwan <Master Inscription Trainer>
{ 30722,0x000050, 773, 0},  // Neferatti <Master Inscription Trainer>
{ 30723,0x000880,   0, 0},  // Xantili <Inscription Supplies>
{ 30724,0x000880,   0, 0},  // Mertle Murkpen <Inscription Supplies>
{ 30727,0x000080,   0, 0},  // Lelorian <Inscription Supplies>
{ 30729,0x000080,   0, 0},  // Ickabod Pimlen <Inscription Supplies>
{ 30730,0x000880,   0, 0},  // Stanly McCormick <Inscription Supplies>
{ 30731,0x000080,   0, 0},  // Illianna Moonscribe <Inscription Supplies>
{ 30732,0x000880,   0, 0},  // Sessoh <Inscription Supplies>
{ 30733,0x000880,   0, 0},  // Thargen Heavyquill <Inscription Supplies>
{ 30734,0x000880,   0, 0},  // Jezebel Bican <Inscription Supplies>
{ 30735,0x000080,   0, 0},  // Kul Inkspiller <Inscription Supplies>
{ 30825,0x001080,   0, 0},  // Chief Engineer Copperclaw
{ 30827,0x000080,   0, 0},  // Utamu
{ 30869,0x002000,   0, 0},  // Arzo Safeflight <Flight Master>
{ 30870,0x002000,   0, 0},  // Herzo Safeflight <Flight Master>
{ 30885,0x000080,   0, 0},  // Blazik Fireclaw <Water Vendor>
{ 31017,0x000280,   0, 0},  // Mal Corricks <Barkeep>
{ 31019,0x000280,   0, 0},  // Stephanie Sindree <Fruit and Fish Vendor>
{ 31021,0x000080,   0, 0},  // Sophie Aaren <Florist>
{ 31022,0x000180,   0, 0},  // George Goodman <General Goods>
{ 31024,0x001080,   0, 0},  // Brock Thriss <Blacksmith>
{ 31025,0x000180,   0, 0},  // Robert Pierce <Ammunition>
{ 31027,0x001080,   0, 0},  // Leeka Turner <Shields and Maces>
{ 31031,0x000080,   0, 0},  // Misensi <Cooking Supplies>
{ 31032,0x000080,   0, 0},  // Derek Odds <Cooking Supplies>
{ 31069,0x002000,   0, 0},  // Penumbrius <Flight Master>
{ 31078,0x002000,   0, 0},  // Dreadwind <Flight Master>
{ 31115,0x000080,   0, 0},  // Quartermaster Vaskess <Supplies>
{ 31238,0x0000d0,   0, 0},  // Hira Snowdawn <Cold Weather Flying Trainer>
{ 31247,0x0000d0,   0, 0},  // Roxi Ramrocket <Cold Weather Flying Trainer>
{ 31557,0x010280,   0, 0},  // Uda the Beast <Innkeeper>
{ 31579,0x000080,   0, 0},  // Arcanist Adurin <Emblem of Valor Quartermaster>
{ 31580,0x000080,   0, 0},  // Arcanist Ivrenne <Emblem of Heroism Quartermaster>
{ 31581,0x000080,   0, 0},  // Magister Brasael <Emblem of Valor Quartermaster>
{ 31582,0x000080,   0, 0},  // Magistrix Lambriesse <Emblem of Heroism Quartermaster>
{ 31776,0x001080,   0, 0},  // Frazzle Geargrinder <Engineering Supplies>
{ 31781,0x001080,   0, 0},  // Blast Thunderbomb <Engineering Supplies>
{ 31804,0x001280,   0, 0},  // Qatiichii <Green Island>
{ 31805,0x001280,   0, 0},  // Nagojut <Walker of Waves>
{ 31910,0x000080,   0, 0},  // Geen <Oracles Quartermaster>
{ 31911,0x000080,   0, 0},  // Tanak <Frenzyheart Quartermaster>
{ 31916,0x000080,   0, 0},  // Tanaika <Kalu'ak Quartermaster>
{ 32172,0x000080,   0, 0},  // Harold Winston <Jewelry Vendor>
{ 32216,0x000080,   0, 0},  // Mei Francis <Exotic Mounts>
{ 32253,0x001080,   0, 0},  // Kyunghee <Throwing Axes>
{ 32287,0x000080,   0, 0},  // Archmage Alvareaux <Kirin Tor Quartermaster>
{ 32334,0x000080,   0, 0},  // Nixi Fireclaw <Water Vendor>
{ 32337,0x000080,   0, 0},  // Christi Stockton <Wine Vendor>
{ 32403,0x000280,   0, 0},  // Sandra Bartan <Barmaid>
{ 32411,0x010000,   0, 0},  // Afsaneh Asrar <Assistant Innkeeper>
{ 32412,0x000280,   0, 0},  // Mato <Food & Drink>
{ 32413,0x010280,   0, 0},  // Isirami Fairwind <Innkeeper>
{ 32415,0x000280,   0, 0},  // Hamaka <Bartender>
{ 32416,0x000280,   0, 0},  // Stefen Cotter <Bartender>
{ 32418,0x010000,   0, 0},  // Abohba <Assistant Innkeeper>
{ 32419,0x000280,   0, 0},  // Umbiwa <Barmaid>
{ 32420,0x000280,   0, 0},  // Mimbihi <Barmaid>
{ 32421,0x000280,   0, 0},  // Marcella Bloom <Barmaid>
{ 32424,0x000280,   0, 0},  // Laire Brewgold <Brewmaiden>
{ 32426,0x000280,   0, 0},  // Coira Longrifle <Brewmaiden>
{ 32474,0x000050, 356, 0},  // Fishy Ser'ji <Grand Master Fishing Trainer>
{ 32477,0x001180,   0, 0},  // Chester Copperpot <General & Trade Supplies>
{ 32478,0x000280,   0, 0},  // Slosh <Food & Drink>
{ 32509,0x000080,   0, 0},  // Brammold Deepmine <Antiques & Heirlooms>
{ 32514,0x000080,   0, 0},  // Vanessa Sellers <Shard Trader>
{ 32515,0x000080,   0, 0},  // Braeg Stoutbeard <Fur & Leather Trader>
{ 32533,0x000080,   0, 0},  // Cielstrasza <Wyrmrest Accord Quartermaster>
{ 32538,0x000080,   0, 0},  // Duchess Mynx <Ebon Blade Quartermaster>
{ 32564,0x000080,   0, 0},  // Logistics Officer Silverstone <Alliance Vanguard Quartermaster>
{ 32565,0x000080,   0, 0},  // Gara Skullcrush <Horde Expedition Quartermaster>
{ 32631,0x000280,   0, 0},  // Alfred Copperworth <Butler>
{ 32763,0x000080,   0, 0},  // Sairuk <Kalu'ak Quartermaster>
{ 32773,0x000080,   0, 0},  // Logistics Officer Brighton <Alliance Vanguard Quartermaster>
{ 32774,0x000080,   0, 0},  // Sebastian Crane <Horde Expedition Quartermaster>
{ 33018,0x000080,   0, 0},  // Jennifer Owings <Food & Drink>
{ 33019,0x000080,   0, 0},  // Megan Owings <Bartender>
{ 33026,0x000280,   0, 0},  // Sarah Brady <Alcohol>
{ 33027,0x000080,   0, 0},  // Jessica Sellers <Ink Trader>
{ 33307,0x000080,   0, 0},  // Corporal Arthur Flew <Stormwind Quartermaster>
{ 33310,0x000080,   0, 0},  // Derrick Brindlebeard <Ironforge Quartermaster>
{ 33553,0x000080,   0, 0},  // Freka Bloodaxe <Orgrimmar Quartermaster>
{ 33554,0x000080,   0, 0},  // Samamba <Sen'jin Quartermaster>
{ 33555,0x000080,   0, 0},  // Eliza Killian <Undercity Quartermaster>
{ 33556,0x000080,   0, 0},  // Doru Thunderhorn <Thunder Bluff Quartermaster>
{ 33557,0x000080,   0, 0},  // Trellis Morningsun <Silvermoon Quartermaster>
{ 33580,0x000050, 197, 0},  // Dustin Vail <Grand Master Tailoring Trainer>
{ 33581,0x000010,   0, 0},  // Kul'de <Grand Master Leatherworking Trainer>
{ 33583,0x000010,   0, 0},  // Fael Morningsong <Grand Master Enchanting Trainer>
{ 33586,0x000010,   0, 0},  // Binkie Brightgear <Grand Master Engineering Trainer>
{ 33587,0x000050, 185, 0},  // Bethany Cromwell <Grand Master Cooking Trainer>
{ 33588,0x000050, 171, 0},  // Crystal Brightspark <Grand Master Alchemy Trainer>
{ 33589,0x000050, 129, 0},  // Joseph Wilson <Grand Master First Aid Trainer>
{ 33590,0x000050, 755, 0},  // Oluros <Grand Master Jewelcrafting Trainer>
{ 33591,0x000010,   0, 0},  // Rekka the Hammer <Grand Master Blacksmithing Trainer>
{ 33594,0x001080,   0, 0},  // Fizzix Blastbolt <Engineering Supplies>
{ 33595,0x000080,   0, 0},  // Mera Mistrunner <Cooking Supplies>
{ 33596,0x000080,   0, 0},  // Meline Duskpath <Tailoring Supplies>
{ 33597,0x000080,   0, 0},  // Liere Morningsong <Enchanting Supplies>
{ 33598,0x000880,   0, 0},  // Fanii <Inscription Supplies>
{ 33602,0x000080,   0, 0},  // Anuur <Jewelcrafting Supplies>
{ 33630,0x0000d0, 171, 0},  // Aelthin <Master Alchemy Trainer>
{ 33631,0x0000d0, 164, 0},  // Barien <Master Blacksmithing Trainer>
{ 33633,0x0000d0, 333, 0},  // Enchantress Andiala <Master Enchanting Trainer>
{ 33634,0x0000d0, 202, 0},  // Engineer Sinbei <Master Engineering Trainer>
{ 33635,0x0000d0, 165, 0},  // Daenril <Master Leatherworking Trainer>
{ 33636,0x0000d0, 197, 0},  // Miralisse <Master Tailoring Trainer>
{ 33637,0x0000d0, 755, 0},  // Kirembri Silvermane <Master Jewelcrafting Trainer>
{ 33638,0x0000d0, 773, 0},  // Scribe Lanloer <Master Inscription Trainer>
{ 33639,0x0000d0, 182, 0},  // Botanist Alaenra <Master Herbalism Trainer>
{ 33640,0x0000d0, 186, 0},  // Hanlir <Master Mining Trainer>
{ 33641,0x0000d0, 393, 0},  // Irduil <Master Skinning Trainer>
{ 33644,0x000280,   0, 0},  // Ellia Moondancer <Fresh Food>
{ 33645,0x000280,   0, 0},  // Jenna Thunderbrew <Refreshments>
{ 33650,0x000080,   0, 0},  // Rillie Spindlenut <Gnomeregan Quartermaster>
{ 33653,0x000080,   0, 0},  // Rook Hawkfist <Darnassus Quartermaster>
{ 33657,0x000080,   0, 0},  // Irisee <Exodar Quartermaster>
{ 33674,0x0000d0, 171, 0},  // Alchemist Kanhu <Master Alchemy Trainer>
{ 33675,0x0000d0, 164, 0},  // Onodo <Master Blacksmithing Trainer>
{ 33676,0x0000d0, 333, 0},  // Zurii <Master Enchanting Trainer>
{ 33677,0x0000d0, 202, 0},  // Technician Mihila <Master Engineering Trainer>
{ 33678,0x0000d0, 182, 0},  // Jijia <Master Herbalism Trainer>
{ 33679,0x0000d0, 773, 0},  // Recorder Lidio <Master Inscription Trainer>
{ 33680,0x0000d0, 755, 0},  // Nemiha <Master Jewelcrafting Trainer>
{ 33681,0x0000d0, 165, 0},  // Korim <Master Leatherworking Trainer>
{ 33682,0x0000d0, 186, 0},  // Fono <Master Mining Trainer>
{ 33683,0x0000d0, 393, 0},  // Dremm <Master Skinning Trainer>
{ 33684,0x0000d0, 197, 0},  // Weaver Aoa <Master Tailoring Trainer>
{ 33849,0x002000,   0, 0},  // Helidan Lightwing <Flight Master>
{ 33853,0x000080,   0, 0},  // Broxel Goldgrasp <Souvenirs>
{ 33865,0x000280,   0, 0},  // Makuli <Fresh Food>
{ 33866,0x000280,   0, 0},  // Akrittok <Fresh Food>
{ 33868,0x000280,   0, 0},  // Tingiyok <Refreshments>
{ 33869,0x000280,   0, 0},  // Torngasak <Refreshments>
{ 33871,0x000880,   0, 0},  // Julie Osworth <Reagents>
{ 33872,0x000180,   0, 0},  // Torgah <General Goods>
{ 33963,0x000080,   0, 0},  // Magister Sarien <Emblem of Conquest Quartermaster>
{ 33964,0x000080,   0, 0},  // Arcanist Firael <Emblem of Conquest Quartermaster>
{ 33970,0x010000,   0, 0},  // Caris Sunlance <Innkeeper>
{ 33971,0x010000,   0, 0},  // Jarin Dawnglow <Innkeeper>
{ 33996,0x0000d0,   0, 0},  // William Saldean <Grain & Feed Vendor>
{ 34038,0x001080,   0, 0},  // Sergeant Thunderhorn <Apprentice Armor Quartermaster>
{ 34043,0x000080,   0, 0},  // Lady Palanseer <Jewelcrafting Quartermaster>
{ 34060,0x001080,   0, 0},  // Doris Volanthius <Veteran Armor Quartermaster>
{ 34063,0x001080,   0, 0},  // Blood Guard Zar'shi <Northrend Armor Quartermaster>
{ 34075,0x001080,   0, 0},  // Captain Dirgehammer <Apprentice Armor Quartermaster>
{ 34078,0x001080,   0, 0},  // Lieutenant Tristia <Veteran Armor Quartermaster>
{ 34081,0x000080,   0, 0},  // Captain O'Neal <Jewelcrafting Quartermaster>
{ 34252,0x001080,   0, 0},  // Dubin Clay <Plate Armor Merchant>
{ 34772,0x000080,   0, 0},  // Vasarin Redmorn <Sunreavers Quartermaster>
{ 34881,0x000080,   0, 0},  // Hiren Loresong <Silver Covenant Quartermaster>
{ 34885,0x000080,   0, 0},  // Dame Evniki Kapsalis <Crusader's Quartermaster>
{ 35093,0x000050, 762, 0},  // Wind Rider Jahubo <Riding Trainer>
{ 35099,0x000080,   0, 0},  // Bana Wildmane <Wind Rider Keeper>
{ 35100,0x000050, 762, 0},  // Hargen Bronzewing <Riding Trainer>
{ 35101,0x000080,   0, 0},  // Grunda Bronzewing <Gryphon Keeper>
{ 35131,0x000080,   0, 0},  // Durgan Thunderbeak <Gryphon Keeper>
{ 35132,0x000080,   0, 0},  // Tohfo Skyhoof <Wind Rider Keeper>
{ 35133,0x000050, 762, 0},  // Maigra Keenfeather <Riding Trainer>
{ 35135,0x000050, 762, 0},  // Wind Rider Sabamba <Riding Trainer>
{ 35281,0x000030,   0, 2},  // Rukua <Paladin Trainer>
{ 35494,0x000080,   0, 0},  // Arcanist Miluria <Emblem of Triumph Quartermaster>
{ 35495,0x000080,   0, 0},  // Magistrix Vesara <Emblem of Triumph Quartermaster>
{ 35496,0x001080,   0, 0},  // Rueben Lauren <Cloth Armor Merchant>
{ 35497,0x001080,   0, 0},  // Rafael Langrom <Leather Armor Merchant>
{ 35498,0x001080,   0, 0},  // Horace Hunderland <Plate Armor Merchant>
{ 35500,0x001080,   0, 0},  // Matilda Brightlink <Mail Armor Merchant>
{ 35507,0x000080,   0, 0},  // Enchanter Isian <Heirloom Vendor>
{ 35508,0x000080,   0, 0},  // Enchanter Erodin <Heirloom Vendor>
{ 35573,0x000080,   0, 0},  // Arcanist Asarina <Emblem of Triumph Quartermaster>
{ 35574,0x000080,   0, 0},  // Magistrix Iruvia <Emblem of Triumph Quartermaster>
{ 35575,0x000080,   0, 0},  // Champion Isimode <Triumphant Armor Vendor>
{ 35576,0x000080,   0, 0},  // Champion Faesrol <Triumphant Armor Vendor>
{ 35577,0x000080,   0, 0},  // Valiant Laradia <Triumphant Armor Vendor>
{ 35578,0x000080,   0, 0},  // Valiant Bressia <Triumphant Armor Vendor>
{ 35579,0x000080,   0, 0},  // Aspirant Forudir <Triumphant Armor Vendor>
{ 35580,0x000080,   0, 0},  // Aspirant Naradiel <Triumphant Armor Vendor>
{ 35594,0x200000,   0, 0},  // Brassbolt Mechawrench <Steam-Powered Auctioneer>
{ 35607,0x200000,   0, 0},  // Reginald Arcfire <Steam-Powered Auctioneer>
{ 35790,0x000080,   0, 0},  // Usuri Brightcoin <Money Changer>
{ 35826,0x000080,   0, 0},  // Kaye Toogie
{ 37687,0x001080,   0, 0},  // Alchemist Finklestein <Ashen Verdict Quartermaster>
{ 37688,0x001080,   0, 0},  // Crusader Grimtong <Warrior Armor>
{ 37696,0x001080,   0, 0},  // Crusader Halford <Paladin Armor>
{ 37888,0x002000,   0, 0},  // Frax Bucketdrop <Flight Master>
{ 37915,0x002000,   0, 0},  // Timothy Cunningham <Bat Handler>
{ 37941,0x000080,   0, 0},  // Magister Arlan <Emblem of Frost Quartermaster>
{ 37942,0x000080,   0, 0},  // Arcanist Uovril <Emblem of Frost Quartermaster>
{ 37991,0x001080,   0, 0},  // Ikfirus the Vile <Rogue Armor>
{ 37992,0x001080,   0, 0},  // Tortunok <Druid Armor>
{ 37993,0x001080,   0, 0},  // Gerardo the Suave <Hunter Armor>
{ 38054,0x001080,   0, 0},  // Scott the Merciful <Priest Armor>
{ 38181,0x001080,   0, 0},  // Haragg the Unseen <Warlock Armor>
{ 38284,0x001080,   0, 0},  // Uvlus Banefire <Mage Armor>
{ 38316,0x001080,   0, 0},  // Ormus the Penitent <Death Knight Armor>
{ 38841,0x001080,   0, 0},  // Vol'guk <Shaman Armor>
{ 38858,0x001080,   0, 0},  // Goodman the "Closer" <Emblem of Frost Quartermaster>
{ 40160,0x000080,   0, 0},  // Frozo the Renowned <Frozen Orb Trader>
{ 40405,0x000010,   0, 0},  // Kieupid <Pet Trainer>
{ 40607,0x001080,   0, 0},  // Knight-Lieutenant T'Maire Sydes <Northrend Armor Quartermaster>
