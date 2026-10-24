#include "globals.h"
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/LocalSubscriptions.h>
#include <jde/ql/ql.h>

#define let const auto

namespace Jde::QL{
	sp<Tests::DataSourceQL> _ql;
	α Tests::SetQL( sp<DataSourceQL> ql )ι->void{ _ql = move(ql); }
	α Tests::QL()ι->LocalQL&{ return *_ql; }
	α Tests::QLPtr()ι->sp<LocalQL>{ return _ql; }
	α Tests::Schemas()ι->const vector<sp<DB::AppSchema>>&{ return _ql->Schemas(); }
	α Tests::DS()ι->DB::IDataSource&{ return _ql->DS(); }

	α Tests::Query( string ql, jobject vars, UserPK executer, SL sl )ε->jvalue{ return QL().QuerySync<jvalue>( move(ql), move(vars), executer, true, sl ); }
	α Tests::QueryObject( string ql, jobject vars, UserPK executer, SL sl )ε->jobject{ return QL().QuerySync<jobject>( move(ql), move(vars), executer, true, sl ); }
	α Tests::QueryArray( string ql, jobject vars, UserPK executer, SL sl )ε->jarray{ return QL().QuerySync<jarray>( move(ql), move(vars), executer, true, sl ); }
	α Tests::Create( string ql, jobject vars, UserPK executer, SL sl )ε->uint{ return AsId<uint>( Query(move(ql), move(vars), executer, sl), sl ); }
	Ω prefixed( sv table )ι->string{ return Ƒ( "{}{}", Tests::Schemas().front()->Prefix, table ); }
	α Tests::Count( sv table, string where, vector<DB::Value> params )ε->uint{
		return DS().ScalerSync<uint>( {Ƒ("select count(*) from {} {}", prefixed(table), where), move(params)} );
	}
	α Tests::Purge( sv table, sv keyColumn, uint id )ε->void{
		DS().ExecuteSync( {Ƒ("delete from {} where {}=?", prefixed(table), keyColumn), {DB::Value{id}}} );
	}

	α Tests::EnsureItemTypes()ε->void{
		let existing = QueryArray( "itemTypes{ id name }" );
		for( let& [id,name] : vector<std::pair<uint,sv>>{ {1,Gadget}, {2,Gizmo} } ){
			if( find_if(existing, [&](let& v){ return Json::AsNumber<uint>(v.get_object(), "id")==id; })==existing.end() )
				Query( Ƒ("mutation createItemType( id:{}, name:\"{}\" )", id, name) );
		}
	}
	α Tests::WhatOf( string ql )ι->string{ return WhatOf( [&]{ Query(move(ql)); } ); }
namespace Tests{
	α OpsFixture::TearDown()->void{
		for( auto& listener : _listeners )
			Subscriptions::StopListen( listener );
		for( let id : _items )
			Purge( "items", "item_id", id );
	}
	α OpsFixture::Listen( sv text )ε->sp<RecordingListener>{
		auto listener = _listeners.emplace_back( ms<RecordingListener>(::testing::UnitTest::GetInstance()->current_test_info()->test_suite_name()) );
		BlockAwait( move(*QLPtr()->Subscribe(string{text}, jobject{{"id",7717}}, listener, System)) );
		return listener;
	}
	α OpsFixture::Item( sv slug, sv itemType, sv extra )ε->uint{
		return Track( Create(Ƒ(R"(mutation createItem( name:"{0}", slug:"{0}", itemType:"{1}"{2} ){{ id }})", slug, itemType, extra)) );
	}
}}