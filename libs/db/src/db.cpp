#include <jde/db/db.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/process/dll.h>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/Cluster.h>
#include <jde/db/meta/Catalog.h>
#include <jde/db/meta/AppSchema.h>
#include "jde/fwk/log/logTags.h"
#include "jde/fwk/usings.h"
#include "meta/ddl/CatalogDdl.h"
#include "meta/ddl/SchemaDdl.h"
#include "meta/IServerMeta.h"
#include "c_api.h"

#define let const auto

namespace Jde::DB{
	vector<sp<Cluster>> _clusters;
	Ω buildClusters( const jobject& dbServers, sp<Access::IAcl> authorize )ε->vector<sp<Cluster>>{
		vector<sp<Cluster>> y;
		for( auto&& [name, value] : dbServers ){
			if( value.is_object() ){ //vs sync script dir
				y.emplace_back( ms<Cluster>(name, value.get_object(), authorize) );
				Cluster::Initialize( y.back() );
			}
		}
		THROW_IF( y.empty(), "No db servers found." );
		return y;
	}
	Ω getClusters( sp<Access::IAcl> authorize )ε->const vector<sp<Cluster>>&{ //global-settings cache; supplied dbSettings go through buildClusters instead (see GetAppSchema).
		if( _clusters.empty() )
			_clusters = buildClusters( Settings::AsObject("/dbServers"), authorize );
		return _clusters;
	}
}
namespace Jde{
	class DataSourceApi{
		DllHelper _dll;
	public:
		DataSourceApi( fs::path path ):
			_dll{ move(path) },
			GetDataSourceFunction{ _dll["GetDataSource"] }
		{}
		decltype(GetDataSource) *GetDataSourceFunction;
	};

	DllApiCache<DataSourceApi> _dataSources;
	α DB::DataSource( const jobject& config, SL sl )ε->sp<IDataSource>{
		fs::path driver{ Json::AsString(config, "driver") };
		THROW_IF( !fs::is_regular_file(driver), Exception(sl, {ELogLevel::Critical, ELogTags::App}, "Dynamic Library '{}' not found.", driver.string()) );
		auto api = _dataSources.Get( driver );
		sp<IDataSource> ds{ api->GetDataSourceFunction(), [api](IDataSource* p){ delete p; } }; //deleter keeps the api (and dll) mapped until after the last data source built from it is destroyed.
		try{
			ds->SetConfig( config );
		} catch( const std::exception& e ){
			THROW( "Failed to configure data source: {}", e.what() ); //dll gets closed here, taking the format literal and source_location with it.
		}
		return ds;
	}

	α DB::GetCluster( sv configName, sp<Access::IAcl> authorize, SL sl )ε->sp<Cluster>{
		for( auto& cluster : getClusters(authorize) )
			if( cluster->ConfigName==configName )
				return cluster;
		THROW( Exception(sl, {ELogLevel::Critical, ELogTags::App}, "Cluster '{}' not found.", configName) );
	}

	α DB::GetAppSchema( str metaName, sp<Access::IAcl> authorize, optional<jobject> dbSettings )ε->sp<AppSchema>{
		//Supplied dbSettings bypass the cache both ways - clusters built from exactly these settings, no cache pollution.
		let adHoc = dbSettings ? buildClusters( *dbSettings, authorize ) : vector<sp<Cluster>>{};
		for( auto& cluster : dbSettings ? adHoc : getClusters(authorize) ){
			if( auto schema = cluster->FindAppSchema(metaName); schema )
				return schema;
		}
		THROW( "Schema '{}' not found.", metaName );
	}


	α DB::SyncSchema( const AppSchema& schema, sp<QL::IQL> ql )ε->void{
		SchemaDdl::Sync( schema, ql );
	}
	α DB::SyncData( const AppSchema& schema, sp<QL::IQL> ql, sv extension, bool skipUnchanged )ε->void{
		SchemaDdl::SeedData( schema, extension, ql, skipUnchanged );
	}
	α DB::SeedFile( const AppSchema& schema, string name, str text, sp<QL::IQL> ql, bool skipUnchanged )ε->bool{
		return SchemaDdl::SeedFile( schema, move(name), text, ql, skipUnchanged );
	}
}

#ifndef PROD
namespace Jde::DB{
	bool recreated{}; //TODO: make this per dbSchema.
	α NonProd::Recreate( const AppSchema& schema, sp<QL::IQL> ql )ε->void{
		if( !recreated )
			CatalogDdl::NonProd::Drop( schema );
		recreated = true;
		return SchemaDdl::Sync( schema, ql );
	}
}
#endif
