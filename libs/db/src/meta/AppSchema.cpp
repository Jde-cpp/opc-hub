#include <jde/db/meta/AppSchema.h>
#include <jde/fwk/io/json.h>
#include <jde/db/IDataSource.h>
#include <jde/db/names.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/DBSchema.h>
#include <jde/db/meta/Catalog.h>
#include <jde/db/meta/Cluster.h>
#include <jde/db/meta/Table.h>
#define let const auto

namespace Jde::DB{
	α GetTables( const jobject& jtables )ε->flat_map<string,sp<Table>>{
		flat_map<string,sp<Table>> tables;
		for( let& [jname,table] : jtables ){
			let name = Names::FromJson(jname);
			tables.emplace( name, ms<Table>(name, Json::AsObject(table)) );
		}
		return tables;
	}

	//`resources:{ <jsonName>:{ ops:[…] } }` - a resource the schema declares without a table behind it.  Same key
	//convention as tables, so the row the sync writes is the one a table of that name would have produced.
	α GetResources( const jobject& jresources )ε->flat_map<string,Access::ERights>{
		flat_map<string,Access::ERights> y;
		for( let& [jname,resource] : jresources )
			y.emplace( Names::FromJson(jname), Access::ToRights(Json::AsArray(Json::AsObject(resource), "ops")) );
		return y;
	}

	α GetViews( const jobject& jviews )ε->flat_map<string,sp<Table>>{
		flat_map<string,sp<Table>> views;
		for( let& [jname,view] : jviews ){
			let name = Names::FromJson( jname );
			views.emplace( name, ms<Table>(name, Json::AsObject(view)) );
		}
		return views;
	}

	AppSchema::AppSchema( sv name, sv prefix, const jobject& meta, sp<Access::IAcl> authorizer )ε:
		Name{ name },
		Authorizer{ authorizer },
		Prefix{ prefix },
		Tables{ GetTables(Json::AsObject(meta,"tables")) },
		Views{ meta.contains("views") ? GetViews(Json::AsObject(meta.at("views"))) : flat_map<string,sp<Table>>{} },
		Resources{ meta.contains("resources") ? GetResources(Json::AsObject(meta.at("resources"))) : flat_map<string,Access::ERights>{} }
	{}

	AppSchema::AppSchema( sv name, const jobject& appSchema, sp<Access::IAcl> authorizer )ε:
		AppSchema{ name, Json::FindDefaultSV(appSchema,"prefix"), Json::ReadJsonNet(Json::AsString(appSchema,"meta"), {}), authorizer }
	{}

	α AppSchema::Initialize( sp<DB::DBSchema> db, sp<AppSchema> self )ε->void{
		self->DBSchema = db;
		if( !db->IsQLOnly() ){
			let& syntax = self->Syntax();
			if( syntax.HasSchemas() && !syntax.CanSetDefaultSchema() && db->DS()->SchemaName() != db->Name )
				self->Prefix = Ƒ( "{}.{}", db->Name, self->Prefix );
		}
		for_each( self->Tables, [self](auto&& kv){kv.second->Initialize(self,kv.second);} );
		for_each( self->Views, [self](auto&& kv){kv.second->Initialize(self,kv.second);} );
	}
	α AppSchema::FindTable( const vector<sp<AppSchema>>& schemas, str tableName )ι->sp<Table>{
		for( let& schema : schemas ){
			if( let table = schema->FindTable(tableName) )
				return table;
		}
		return nullptr;
	}
	α AppSchema::GetTablePtr( const vector<sp<AppSchema>>& schemas, str tableName, SL sl )ε->sp<Table>{
		auto y = FindTable( schemas, tableName );
		THROW_IFSL( !y, "Could not find table '{}'", tableName );
		return y;
	}

	α AppSchema::FindView( const vector<sp<AppSchema>>& schemas, str viewName )ι->sp<Table>{
		for( let& schema : schemas ){
			if( let view = schema->FindView(viewName) )
				return view;
		}
		return nullptr;
	}
	α AppSchema::GetViewPtr( const vector<sp<AppSchema>>& schemas, str viewName, SL sl )ε->sp<Table>{
		auto y = FindView( schemas, viewName );
		THROW_IFSL( !y, "Could not find view '{}'", viewName );
		return y;
	}
	α AppSchema::ConfigPath()Ι->string{
		let catalog = DBSchema->Catalog;
		let cluster = catalog->Cluster;
		return Ƒ( "/dbServers/{}/catalogs/{}/schemas/{}/{}", cluster->ConfigName, catalog->Name, DBSchema->Name, Name );
	}

	α AppSchema::DBName( str objectName )Ι->string{
		return Prefix+objectName;
	}

	α AppSchema::DS()Ε->sp<IDataSource>{
		return DBSchema->DS();
	}
	α AppSchema::ResetDS()Ι->void{ DBSchema->ResetDS(); }
	α AppSchema::Syntax()Ι->const DB::Syntax&{ return DBSchema->Catalog->Syntax(); }//not DS():  that may connect, and this is noexcept (install-issues #66)

	α AppSchema::FindTable( str name )Ι->sp<Table>{
		let y = Tables.find( name );
		return y==Tables.end() ? nullptr : y->second;
	}
	α AppSchema::GetTable( str name, SL sl )Ε->const Table&{
		return *GetTablePtr( name, sl );
	}
	α AppSchema::GetTablePtr( str name, SL sl )Ε->sp<Table>{
		let y = FindTable( name ); THROW_IFSL( !y, "[{}.{}]Could not find table.", Name, name );
		return y;
	}

	α AppSchema::FindView( str name )Ι->sp<Table>{
		auto kv = Views.find( name );
		return kv==Views.end() ? FindTable(name) : kv->second;
	}
	α AppSchema::GetView( str name, SL sl )ε->const Table&{
		return *GetViewPtr( name, sl );
	}
	α AppSchema::GetViewPtr( str name, SL sl )ε->sp<Table>{
		let y = FindView( name ); THROW_IFSL( !y, "Could not find view '{}'", name );
		return y;
	}

	α AppSchema::ObjectPrefix()Ι->string{
		string y{ Prefix };
		if( let index = Prefix.find( '.' ); index!=string::npos )
			y = index < Prefix.size() - 2 ? Prefix.substr( index + 1 ) : string{};
		return y;
	}
}