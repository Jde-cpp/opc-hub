#include "UserAwait.h"
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Statement.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/QLAwait.h>
#include "../serverInternal.h"
#define let const auto


namespace Jde::Access::Server{
	α UserAwait::GroupStatement()->DB::Statement{
		let& identityTable = GetTable( "identities" );
		auto pk = identityTable.GetPK();
		let& groupDBTable = GetTable( "groups" );
		DB::Statement statement;
		auto& groupTable = _query.GetTable( "groups" );
		let memberIdColumn = groupDBTable.GetColumnPtr( "member_id" );
		groupTable.Columns.emplace_back( QL::ColumnQL{"memberId", memberIdColumn} );

		statement.From+={ pk, memberIdColumn, true };
		statement.From+={ groupDBTable.SurrogateKeys[0], {}, pk, "groups_", true };
		if( let key = _query.FindKey(); key )
			statement.Where.Add( key->IsPK() ? pk : identityTable.GetColumnPtr("slug"), DB::Value::FromKey(*key) );
		statement.Select.TryAdd( memberIdColumn );
		for( auto& c : groupTable.Columns ){
			if( c.JsonName=="memberId" ){
				c.DBColumn = memberIdColumn;
				statement.Select.TryAdd( c.DBColumn );
			}
			else{
				let dbColumn = c.JsonName=="id" ? pk : identityTable.GetColumnPtr( DB::Names::FromJson(c.JsonName) );
				statement.Select.TryAdd( {"groups_", dbColumn} );
			}
		}
		return statement;
	}
	α UserAwait::QueryGroups()ι->QL::QLAwait<jarray>::Task{
		try{
			auto groupStatement = GroupStatement();
			auto groupInfo = co_await QL::QLAwait<jarray>{ move(_query.GetTable("groups")), move(groupStatement), _executer, _sl };
			QueryTables( move(groupInfo) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α UserAwait::QueryTables( jarray groups )ι->QL::QLAwait<jvalue>::Task{
		try{
			_query.Tables.clear();
			_query.ReturnRaw = true;
			auto userInfo = _query.Columns.size() ? co_await QL::QLAwait<jvalue>( move(_query), {}, _executer, _sl ) : jobject{};
			if( !userInfo.is_null() ){
				Json::Visit( userInfo, [&](jobject& user){
					let userPK = user.contains("id") ? QL::AsId<UserPK::Type>( user ) : optional<UserPK::Type>{};
					jarray userGroups;
					for( auto v=groups.begin(); v!=groups.end(); ){
						auto& group = v->as_object();
						if( let memberId = Json::AsNumber<UserPK::Type>( group, "memberId" ); !userPK || memberId==*userPK ){
							group.erase( "memberId" );
							userGroups.push_back( move(group) );
							v = groups.erase( v );
						}else
							v = std::next(v);
					}
					user["groups"] = userGroups;
				} );
			}
			Resume( move(userInfo) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}