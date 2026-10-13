#include "globals.h"
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/ql/ql.h>

#define let const auto

namespace Jde::QL{
	sp<DataSourceQL> _ql;
	α Tests::SetQL( sp<DataSourceQL> ql )ι->void{ _ql = move(ql); }
	α Tests::QL()ι->LocalQL&{ return *_ql; }
	α Tests::QLPtr()ι->sp<LocalQL>{ return _ql; }
	α Tests::Schemas()ι->const vector<sp<DB::AppSchema>>&{ return _ql->Schemas(); }
	α Tests::DS()ι->DB::IDataSource&{ return _ql->DS(); }

	α Tests::Query( string ql, jobject vars, UserPK executer, SL sl )ε->jvalue{ return QL().QuerySync<jvalue>( move(ql), move(vars), executer, true, sl ); }
	α Tests::QueryObject( string ql, jobject vars, UserPK executer, SL sl )ε->jobject{ return QL().QuerySync<jobject>( move(ql), move(vars), executer, true, sl ); }
	α Tests::QueryArray( string ql, jobject vars, UserPK executer, SL sl )ε->jarray{ return QL().QuerySync<jarray>( move(ql), move(vars), executer, true, sl ); }
	α Tests::Create( string ql, jobject vars, UserPK executer, SL sl )ε->uint{ return AsId<uint>( Query(move(ql), move(vars), executer, sl), sl ); }
	α Tests::Count( sv table, string where, vector<DB::Value> params )ε->uint{
		return DS().ScalerSync<uint>( {Ƒ("select count(*) from {}{} {}", Schemas().front()->Prefix, table, where), move(params)} );
	}

	α Tests::EnsureItemTypes()ε->void{
		let existing = QueryArray( "itemTypes{ id name }" );
		for( let& [id,name] : vector<std::pair<uint,sv>>{ {1,Gadget}, {2,Gizmo} } ){
			if( find_if(existing, [&](let& v){ return Json::AsNumber<uint>(v.get_object(), "id")==id; })==existing.end() )
				Query( Ƒ("mutation createItemType( id:{}, name:\"{}\" )", id, name) );
		}
	}
}