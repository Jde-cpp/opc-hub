#include <jde/ql/types/Introspection.h>
#include <jde/fwk/io/json.h>
#include <jde/db/names.h>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/DBSchema.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ql.h>
#include "../qlInternal.h"

#define let const auto

namespace Jde{
	using namespace Json;
	QL::Introspection _introspection;
	α QL::AddIntrospection( Introspection&& x )ι->void{ _introspection += move(x); }
	α QL::FindIntrospection( sv typeName )ι->const QL::Object*{ return _introspection.Find( typeName ); }

namespace QL{
	constexpr array<sv,8> FieldKindStrings = { "SCALAR", "OBJECT", "INTERFACE", "UNION", "ENUM", "INPUT_OBJECT", "LIST", "NON_NULL" };
	α ToFieldKind( sv x ){ return ToEnum<EFieldKind>( FieldKindStrings, x ); }
	α ToString( EFieldKind x ){ return FromEnum<EFieldKind>( FieldKindStrings, x ); }

	Type::Type( const jobject& j )ε:
		Name{ FindDefaultSV(j, "name") },
		Kind{ FindEnum<EFieldKind>( j,"kind", ToFieldKind ).value_or( EFieldKind::Scalar ) },
		IsNullable{ Kind!=EFieldKind::NonNull }{
		if( !IsNullable ){
			const jobject& type = AsObject( j, "ofType" );
			Name = AsString( type, "name" );
			Kind = FindEnum<EFieldKind>( type, "kind", ToFieldKind ).value_or( EFieldKind::Scalar );
			if( type.contains("ofType") )
				OfTypePtr = mu<Type>( AsObject(type, "ofType") );
		}
		else if( j.contains("ofType") )
			OfTypePtr = mu<Type>( AsObject(j, "ofType") );
	}
	α Type::ToJson( const TableQL& query, bool ignoreNull )Ε->jobject{
		jobject jType;
		bool isNullType = ignoreNull || IsNullable;
		if( auto pColumn = query.FindColumn("name"); pColumn ){
			if( isNullType && Name.size() )
				jType["name"] = Name;
			else
				jType["name"] = nullptr;
		}
		if( auto pColumn = query.FindColumn("kind"); pColumn )
			jType["kind"] = ToString( isNullType ? Kind : EFieldKind::NonNull );
		auto pOfType = !IsNullable && !ignoreNull ? this : OfTypePtr.get();
		if( auto pTable = pOfType ? query.FindTable("ofType") : nullptr; pTable )
			jType["ofType"] = pOfType->ToJson( *pTable, pOfType==this );
		return jType;
	}

	Field::Field( const jobject& j )ε:
		Name{ AsSV(j, "name") },
		FieldType{ AsObject(j, "type") }
	{}

	α Field::ToJson( const TableQL& query )Ε->jobject{
		jobject jField;
		if( auto pColumn = query.FindColumn("name"); pColumn )
			jField["name"] = Name;
		if( auto pTable = query.FindTable("type"); pTable )
			jField["type"] = FieldType.ToJson( *pTable, false );
		return jField;
	}

	EnumValue::EnumValue( const jobject& j )ε:
		Id{ FindNumber<_int>(j, "id") },
		Name{ AsSV(j, "name") },
		Description{ FindDefaultSV(j, "description") },
		IsDeprecated{ FindBool(j, "isDeprecated").value_or(false) },
		DeprecationReason{ FindDefaultSV(j, "deprecationReason") }
	{}

	α EnumValue::ToJson( const TableQL& query )Ε->jobject{
		jobject y;
		if( query.FindColumn("id") )
			y["id"] = Id ? jvalue{*Id} : jvalue{};
		if( query.FindColumn("name") )
			y["name"] = Name;
		if( query.FindColumn("description") )
			y["description"] = Description.size() ? jvalue{Description} : jvalue{};
		if( query.FindColumn("isDeprecated") )
			y["isDeprecated"] = IsDeprecated;
		if( query.FindColumn("deprecationReason") )
			y["deprecationReason"] = DeprecationReason.size() ? jvalue{DeprecationReason} : jvalue{};
		return y;
	}


	Object::Object( sv name, const jobject& j )ε:
		Name{ name },
		Extend{ FindBool(j, "extend").value_or(false) }{
		for( let& field : FindDefaultArray(j, "fields") )
			Fields.emplace_back( AsObject(field) );
		for( let& enumValue : FindDefaultArray(j, "enumValues") )
			EnumValues.emplace_back( AsObject(enumValue) );
	}

	α Object::ToJson( const TableQL& query )Ε->jobject{
		jobject jTable;
		jTable["name"] = Name;
		auto add = [&]( sv key, let& vec ){
			jarray array;
			for( let& item : vec )
				array.push_back( item.ToJson(query) );
			jTable[key] = array;
		};
		if( query.JsonName=="fields" )
			add( query.JsonName, Fields );
		else if( query.JsonName=="enumValues" )
			add( query.JsonName, EnumValues );
		return jTable;
	}

	Introspection::Introspection( const jobject&& j )ε{
		for( let& [name, value]  : j )
			Objects.emplace_back( name, AsObject(value) );
	}

	α Introspection::Find( sv name )Ι->const Object*{
		auto y = find_if( Objects, [name](let& Value){ return Value.Name==name; } );
		return y==Objects.end() ? nullptr : &*y;
	}
	α Introspection::operator+=( Introspection&& x )ι->void{
		for( auto&& o : x.Objects )
			Objects.emplace_back( move(o) );
	}


	using namespace DB::Names;
	Ω introspectFields( const DB::Table& mainTable, const TableQL& fieldTable )ε->jobject{
		jarray fields;
		let haveName = fieldTable.FindColumn( "name" )!=nullptr;
		let typeTable = fieldTable.FindTable( "type" );
		let ofTypeTable = typeTable ? typeTable->FindTable( "ofType" ) : nullptr;
		jobject jTable;
		jTable["name"] = mainTable.JsonName();

		auto addField = [&]( sv name, sv typeName, EFieldKind typeKind, sv ofTypeName, optional<EFieldKind> ofTypeKind ){
			jobject field;
			if( haveName )
				field["name"] = name;
			if( typeTable ){
				jobject type;
				auto setField = []( const TableQL& t, jobject& j, str key, sv x ){ if( t.FindColumn(key) ){ if(x.size()) j[key]=x; else j[key]=nullptr; } };
				auto setKind = []( const TableQL& t, jobject& j, optional<EFieldKind> pKind ){
					if( t.FindColumn("kind") ){
						if( pKind )
							j["kind"] = ToString( *pKind );
						else
							j["kind"] = nullptr;
					}
				};
				setField( *typeTable, type, "name", typeName );
				setKind( *typeTable, type, typeKind );
				if( ofTypeTable && (ofTypeName.size() || ofTypeKind) ){
					jobject ofType;
					setField( *ofTypeTable, ofType, "name", ofTypeName );
					setKind( *ofTypeTable, ofType, ofTypeKind );
					type["ofType"] = ofType;
				}
				field["type"] = type;
			}
			fields.push_back( field );
		};
		function<void(const DB::Table&, bool, string)> addColumns = [&addColumns,&addField,&mainTable]( const DB::Table& dbTable, bool isMap, string prefix={} ){
			for( let& c : dbTable.Columns ){
				let& column = *c;
				string fieldName;
				string qlTypeName;
				auto rootType{ EFieldKind::Scalar };
				if( !column.PKTable ){
					if( prefix.size() && column.SKIndex )//use NID see RolePermission's permissions
						continue;
					fieldName = column.IsPK() ? "id" : ToJson( column.Name );
					//#40: QLType throws for every type graphql has no spelling for, and that took the whole __type document with it -
					//opcServer's nodeIds.guid is Guid, and every node table extends node_ids, so `__type(name:"NodeId"){fields{}}` and
					//each of theirs answered "Query failed.".  A column that can not be named in the schema is left out of it, which is
					//what the VarBinary case here (users.password) already did by hand and what QuerySchema does since #31.  Not mapped to
					//String: DB::Value{Guid,json} throws too, so the field would advertise a filter the rest of the stack refuses.
					try{ qlTypeName = ColumnQL::QLType( column ); }
					catch( const Exception& ){ continue; }
				}
				else{
					auto childColumn = dbTable.Map ? dbTable.Map->Child : nullptr;
					if( !isMap || column.PKTable->IsFlags || (childColumn && childColumn->Name==column.Name)  ){ //
						if( find_if(dbTable.Columns, [&column](let& c){return c->QLAppend==column.Name;})!=dbTable.Columns.end() )
							continue;
						if( mainTable.Extends && mainTable.Extends->GetPK()->Name==column.Name ){//extension table
							addColumns( *column.PKTable, false, prefix );
							continue;
						}
						qlTypeName = column.PKTable->JsonName();
						if( column.IsPK() ){ //roles
							fieldName = "id";
							qlTypeName = "ID";
						}else if( column.PKTable->IsFlags ){
							fieldName = ToPlural( ToJson(column.Name) );
							rootType = EFieldKind::List;
						}
						else if( childColumn ){
							fieldName = ToPlural( ToJson(Str::Replace(column.Name, "_id", "")) );
							rootType = EFieldKind::List;
						}
						else{
							fieldName = ToJson( qlTypeName );
							rootType = column.PKTable->IsEnum() ? EFieldKind::Enum : EFieldKind::Object;
						}
					}
					else{ //isMap
						//if( !typeName.starts_with(pPKTable->JsonName()) )//typeName==RolePermission, don't want role columns, just permissions.
						addColumns( *column.PKTable, false, column.PKTable->JsonName() );
						continue;
					}
				}

				auto pChildColumn = dbTable.Map ? dbTable.Map->Child : nullptr;
				let isNullable = pChildColumn || column.IsNullable;
				let typeName2 = isNullable ? qlTypeName : "";
				let typeKind = isNullable ? rootType : EFieldKind::NonNull;
				let ofTypeName = isNullable ? "" : qlTypeName;
				let ofTypeKind = isNullable ? optional<EFieldKind>{} : rootType;

				addField( fieldName, typeName2, typeKind, ofTypeName, ofTypeKind );
			}
		};
		addColumns( mainTable, mainTable.Map.has_value(), {} );
		for( let& [name,pTable] : mainTable.Schema->Tables ){
			auto addMapField = [addField,pTable,&mainTable]( let& c1Name, let& c2Name ){//a map table whose c1 points here contributes a list field of the other side.
				if( let pColumn1=pTable->FindColumn(c1Name), pColumn2=pTable->FindColumn(c2Name) ; pColumn1 && pColumn2 /*&& pColumn->PKTable==n*/ ){
					if( pColumn1->PKTable->Name==mainTable.Name ){
						let pTable2 = pColumn2->PKTable;
						let jsonType = pTable->Columns.size()==2 ? pTable2->JsonName() : pTable->JsonName();
						addField( ToPlural( ToJson(jsonType) ), {}, EFieldKind::List, jsonType, EFieldKind::Object );
					}
				}
			};
			let child = pTable->Map ? pTable->Map->Child : nullptr;
			let parent = pTable->Map ? pTable->Map->Parent : nullptr;
			if( child && parent ){
				addMapField( child->Name, parent->Name );
				addMapField( parent->Name, child->Name );
			}
		}
		jTable["fields"] = fields;
		return jTable;
	}

	α introspectEnum( const sp<DB::Table> baseTable, const TableQL& fieldTable)ε->jobject{
		THROW_IF( !baseTable, "Base table is null" );
		auto dbTable = baseTable->QLView ? baseTable->QLView : baseTable;
		DB::SelectClause select;
		for_each( fieldTable.Columns, [&select, &dbTable](let& x){
			if( let c = x.JsonName=="id" ? dbTable->GetPK() : x.JsonName=="name" ? dbTable->FindColumn( x.JsonName ) : nullptr; c )
				select.TryAdd( {c} );
		});
		DB::Statement statement{
			select,
			{ {dbTable} },
			{},
			dbTable->GetPK()->Name
		};
		jarray fields;
		dbTable->Schema->DS()->Select( statement.Move(), [&]( DB::Row&& row ){
			jobject j;
			for( uint i=0; i<select.Columns.size(); ++i ){
				auto& c = *get<DB::AliasCol>(select.Columns[i]).Column;
				if( c.IsPK() )
					j["id"] = row.Get<uint>( i );
				else
					j[c.Name] = row.TakeString(i);
			}
			fields.push_back( j );
		} );
		jobject jTable;
		jTable["enumValues"] = fields;
		return jTable;
	}

	α QueryType( const TableQL& typeTable, UserPK executer, SL sl )ε->jobject{
		let& typeName = typeTable.As<jstring>( "name" ); //variable-aware: raw Args holds the '\b$var' marker when the caller binds name via $variables, which made Find miss the config types.
		auto dbTable = typeTable.DBTable(); //null for a config-only type (Parser uses FindView when the name is pre-defined) - then preDefined answers everything.
		let preDefined = _introspection.Find( typeName );
		let extend = preDefined && preDefined->Extend && dbTable; //the DB fields first, then the config's extra ones; without a table there is nothing to extend, so the config replaces.
		jobject y;
		for( let& qlTable : typeTable.Tables ){
			if( preDefined && !extend )
				y = preDefined->ToJson( qlTable );
			else if( qlTable.JsonName=="fields" ){
				THROW_IF( !dbTable, "__type '{}' has no table and no introspection entry.", typeName );
				y = introspectFields( *dbTable, qlTable );
				if( extend ){
					auto& fields = y["fields"].as_array();
					for( let& field : preDefined->Fields )
						fields.push_back( field.ToJson(qlTable) );
				}
			}
			else if( qlTable.JsonName=="enumValues" ){
				if( typeName=="logTags" ){
					jarray enumValues;
					bool haveId=qlTable.FindColumn("id"), haveName=qlTable.FindColumn("name"), haveDescription=qlTable.FindColumn("description");
					for( let& [name, id] : Logging::Tags() ){
						jobject j;
						if( haveId )
							j["id"] = std::to_string(id);
						if( haveName )
							j["name"] = name;
						if( haveDescription )
							j["description"] = nullptr;
						enumValues.push_back( j );
					}
					y["enumValues"] = enumValues;
				}
				else{
					THROW_IF( !dbTable, "__type '{}' has no table and no introspection entry.", typeName );
					if( !dbTable->IsEnum() )//not a lookup table:  its rows are a read, authorized as one - through the view a select would use.
						(dbTable->QLView ? dbTable->QLView : dbTable)->Authorize( Access::ERights::Read, executer, sl );
					y = introspectEnum( dbTable, qlTable );
				}
			}
			else
				THROW( "__type data for '{}' not supported", qlTable.JsonName );
		}
		return y;
	}
	α QuerySchema( const TableQL& schemaTable )ε->jobject{
		THROW_IF( schemaTable.Tables.size()!=1, "Only Expected 1 table type for __schema {}", schemaTable.Tables.size() );
		let& mutationTable = schemaTable.Tables[0]; THROW_IF( mutationTable.JsonName!="mutationType", "Only mutationType implemented for __schema - {}", mutationTable.JsonName );
		jarray fields;
		for( let& schema : schemaTable.DBTable()->Schema->DBSchema->AppSchemas ){
			for( let& nameTablePtr : schema.second->Tables ){
				let pDBTable = nameTablePtr.second;
				let childColumn = pDBTable->Map ? pDBTable->Map->Child : nullptr;
				let jsonType = pDBTable->JsonName();

				let addField = [&jsonType, pDBTable, &fields]( sv name, bool allColumns=false, bool idColumn=true ){
					jobject field;
					jarray args;
					for( let& column : pDBTable->Columns ){
						if( (column->IsPK() && !idColumn) || (!column->IsPK() && !allColumns) )
							continue;
						//#31: QLType throws for the types graphql has no spelling for (users.password is VarBinary), which killed the
						//whole document.  A column that cannot be named in the schema is left out of it instead.
						jobject type;
						try{
							type["name"] = ColumnQL::QLType( *column );
						}
						catch( const Exception& ){
							continue;
						}
						jobject arg;
						arg["name"] = ToJson( column->Name );
						arg["defaultValue"] = nullptr;
						arg["type"]=type;
						args.push_back( arg );
					}
					field["args"] = args;
					field["name"] = Ƒ( "{}{}", name, jsonType );
					fields.push_back( field );
				};
				if( !childColumn ){
					addField( "create", true, false );//#41: `create` is the verb MutationQLNames has;  `insert` was rejected by the parser it advertised itself to.
					addField( "update", true );

					addField( "delete" );
					addField( "restore" );
					addField( "purge" );
				}
				else{
					addField( "add", true, false );
					addField( "remove", true, false );
				}
			}
		}
		jobject jmutationType;
		jmutationType["fields"] = fields;
		jmutationType["name"] = "Mutation";
		jobject jSchema; jSchema["mutationType"] = jmutationType;
		return jSchema;
	}
}}