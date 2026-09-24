// The vocabulary every *-meta.jsonnet builds its tables from: the column `types`, the `$now` sql function, the
// sequenced primary-key shapes (small/pk/long), the `name` column and the slug set (slug, attributes, created, updated,
// deleted, description) with their natural keys, and filter() for dropping fields.  Column keys: `i` the column order,
// `sk` the natural-key position, `sequence` a generated key, `insertable`/`updateable` whether writes carry it (libs/db
// meta/Column.cpp, Table.cpp).  Not imported by path: the build links it beside each meta (linkConfigFile in the
// access, AppServer and OpcGateway CMakeLists) and the installers copy it into the product dir.
{
	local types = self.types,
	types: {
		binary: {type: "Binary"},
		bit: {type:"Bit", length: 1},
		blob: {type: "Blob"},
		char: {type: "Char"},
		cursor: {type: "Cursor"},
		dateTime: {type: "DateTime", length:: 64},
		decimal: {type: "Decimal"},
		float: {type:"Float", length:: 64},
		guid: {type: "Guid" },
		image: {type: "Image"},
		int: {type:"Int", length:: 32},
		int8: {type: "Int8", length:: 8},
		int16: {type:"Int16", length:: 16},
		long: {type: "Long", length:: 64},
		money: {type: "Money", length:: 64},
		ntext: {type: "NText"},
		numeric: {type: "Numeric"},
		refCursor: {type: "RefCursor", length:: 64},
		smallDateTime: {type: "SmallDateTime", length:: 32},
		smallFloat: {type:"SmallFloat", length:: 32},
		tchar: {type: "TChar"},
		text: {type: "Text"},
		timeSpan: {type: "TimeSpan", length:: 64},
		uint: {type:"UInt", length:: 32},
		uint8: {type: "UInt8", length:: 8},
		uint16: {type: "UInt16", length:: 16},
		ulong: {type: "ULong", length:: 64},
		varbinary: {type: "VarBinary"},
		varchar: {type: "Varchar"},
	},
	local sqlFunctions = self.sqlFunctions,
	sqlFunctions:{
		now: { name: "$now" }
	},

	smallSequenced:: types.uint16+{ sequence: true, sk:0, i:0 },
	pkSequenced: types.uint+{ sequence: true, sk:0, i:0 },
	longSequenced: types.ulong+{ sequence: true, sk:0, i:0 },
	local valuesColumns = self.valuesColumns,
	valuesColumns: { name: types.varchar+{ length: 256, i:10 } },
	valuesNK: ["name"],

	slugColumns: valuesColumns+{
		slug: types.varchar+{ length: valuesColumns.name.length, i:20 },
		attributes: types.uint16+{ nullable: true, i:30 },
		created: types.dateTime+{ insertable: false, updateable: false, default: sqlFunctions.now.name, i:40 },
		updated: types.dateTime+{ nullable: true, insertable: false, updateable: false, i:50 },
		deleted:types.dateTime+{ nullable: true, insertable: false, updateable: false, i:60 },
		description: types.varchar+{ length: 2048, nullable: true, i:70 }
	},
	slugNKs: [self.valuesNK, ["slug"]],

	filter(obj, ignore)::
    std.foldl(
			function(filtered, field)(
				if std.member(ignore, field) then
					filtered
				else
					filtered + { [field]: obj[field] }
			),
     	std.objectFields(obj), {}
		)
}