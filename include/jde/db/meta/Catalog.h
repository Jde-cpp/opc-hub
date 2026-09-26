#pragma once

namespace Jde::Access{ struct IAcl; }
namespace Jde::DB{
	struct AppSchema; struct Cluster; struct DBSchema; struct IDataSource; struct Syntax;
	//Cluster > Catalog > Schema > Table > Columns & Rows
	//sql server=Database.
	//mysql=n/a ie single catalog.
	struct Catalog final{
		Catalog( sp<IDataSource> ds )ε:_dataSource{ds}{}//for ddl
		Catalog( sv name, const jobject& config, sp<Access::IAcl> authorizer )ε;

		Ω Initialize( sp<DB::Cluster> cluster, sp<Catalog> self )ε->void;
		α DS()Ε->sp<IDataSource>;
		α Syntax()Ι->const DB::Syntax&;//the dialect - never a connection, which DS() may need to name the catalog
		α SetDS( sp<IDataSource> ds )ι->void{ _dataSource = ds; }
		α FindAppSchema( str name )ι->sp<AppSchema>;
		string Name;
		sp<DB::Cluster> Cluster;
		vector<sp<DBSchema>> Schemas;
	private:
		mutable sp<IDataSource> _dataSource;
	};
}