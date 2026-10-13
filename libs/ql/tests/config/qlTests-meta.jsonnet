// The `qlTests` schema Jde.QL.Tests runs the stock ops against (#238):  four tables with nothing but what the ops read - a
// sequenced pk, a natural key, the soft-delete column, an fk to an enum table, and a map table for add/remove.  Mounted by
// config/args/sqlite/args.libsonnet with the `qlt_` prefix and the Jde.DB.Sqlite.QLTests proc module (config/sql/sqlite),
// which carries the insert twins the two sequenced tables need on sqlite - the other dialects generate theirs.
// common-meta is imported by path rather than linked beside this file:  nothing installs a test schema.
local common = import '../../../db/config/common-meta.libsonnet';
local types = common.types;
{
	local tables = self.tables,
	tables:{
		itemTypes:{ //an enum:  two columns, the second `name` - what items.item_type_id renders and filters through.
			columns:{
				itemTypeId: types.uint8+{ sk:0, i:0 },
			}+common.valuesColumns
		},
		items:{
			columns:{
				itemId: common.pkSequenced,
				itemTypeId: tables.itemTypes.columns.itemTypeId+{ sk:null, pkTable: "item_types", i:1 },
			}+common.slugColumns,
			naturalKeys: common.slugNKs
		},
		labels:{ //three columns, so not an enum - the child side of the map is a plain row.
			columns:{
				labelId: common.pkSequenced,
				color: types.varchar+{ length: 16, nullable: true, i:11 }
			}+common.valuesColumns
		},
		itemLabels:{
			columns:{
				itemId: tables.items.columns.itemId+{ pkTable: "items", sk:0, i:0 },
				labelId: tables.labels.columns.labelId+{ pkTable: "labels", sk:1, i:1 }
			},
			map: { parentId: "item_id", childId: "label_id" }
		}
	}
}