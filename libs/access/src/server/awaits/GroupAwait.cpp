#include "GroupAwait.h"
//#include <jde/db/awaits/RowAwait.h>
#include <jde/db/IDataSource.h>
#include <jde/db/names.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/db/generators/Statement.h>
#include <jde/ql/ql.h>
#include <jde/ql/IQL.h>
#include <jde/ql/types/TableQL.h>
#include <jde/ql/QLAwait.h>
#include <jde/access/Authorize.h>
#include "../serverInternal.h"
#include "../../accessInternal.h"

#define let const auto

namespace Jde::Access::Server{
	//Ω removeFromGroup( GroupPK groupPK, flat_set<IdentityPK> members )ι->void;


	α GroupAwait::Select()ι->QL::QLAwait<>::Task{
		try{
			//group_id, member_id & member columns.
			optional<QL::TableQL> membersQL = [&]()->optional<QL::TableQL>{
				auto p = find_if( _query.Tables, [](let& t){ return t.JsonName.starts_with("groupMember"); } );
				if( p==_query.Tables.end() )
					return {};
				auto ql = *p;
				_query.Tables.erase( p );
				return ql;
			}();
			GetTable( "groups" ).Authorize( Access::ERights::Read, _executer, _sl );
			optional<jarray> members;
			bool haveId{};
			_query.JsonName = _query.IsPlural() ? "identities" : "identity"; //from members, want distinct + nothing in members table except for members.
			_query.SetDBTable( GetTablePtr("identities", _sl) );//the rename alone left _dbTable on access_groups - the member map, one row per member - so SelectStatement joined it to its identities base and fanned a group out into one row per member.
			_query.AddFilter( "is_group", true );
			_query.ReturnRaw = true;
			let onlyHaveId = _query.Columns.size()==1 && _query.Columns[0].JsonName=="id";
			//the shortcut serves one group's members - `group(id:5){ groupMembers{…} }` needs nothing from identities.  A list always
			//queries:  `groups{ id }` used to come back {} whatever the table held, so a client counting groups read zero.
			auto groups = _query.Columns.size() && (_query.IsPlural() || !onlyHaveId) ? co_await QL::QLAwait( move(_query), _executer, _sl ) : _query.DefaultResult();
			if( membersQL ){
				let& groupTable = GetTable( "group_members" );
				haveId = membersQL->FindColumn( "id" );
				if( haveId )
					membersQL->EraseColumn( "id" );
				membersQL->Columns.push_back( QL::ColumnQL{"groupId", groupTable.GetColumnPtr("group_id")} );
				membersQL->Columns.push_back( QL::ColumnQL{"memberId", groupTable.GetColumnPtr("member_id")} );
				if( let idArg = membersQL->Args.if_contains("id"); idArg ){
					auto id = *idArg;
					membersQL->Args["memberId"] = id;
					membersQL->Args.erase( "id" );
				}
				membersQL->JsonName = "groupMembers";
				auto statement = QL::SelectStatement( *membersQL, {}, false );
				for( let& [name,value] : _query.Args ){
					if( name=="is_group" )
						continue;
					string groupName = name=="id"
						? "groupId"
						: name=="slug" ? "group_slug" : name;
					membersQL->Args[groupName] = value;
				}
				statement.Where = QL::ToWhereClause( *membersQL, groupTable, membersQL->FindColumn("deleted")!=nullptr );
				//statement.Where.Remove( "is_group" );
				//statement.Where.Replace( "identities.", "members." );
				auto membersResult = co_await QL::QLAwait( move(*membersQL), move(statement), _executer, _sl );
				if( membersResult.is_array() )
					members = move( membersResult.get_array() );
				else if( membersResult.is_object() )
					members = jarray{ move(membersResult.get_object()) };
			}
			if( !members ){
				Resume( jvalue{move(groups)} );
				co_return;
			}
			auto addMembers = [&](jobject& group){
				optional<GroupPK> groupPK = Json::FindKey<GroupPK>(group);//did not ask for group_id.
				jarray groupMembers;
				for( auto&& memberValue : *members ){
					auto& member = memberValue.as_object();
					const GroupPK memberGroupPK{ Json::FindNumber<GroupPK::Type>(member, "groupId").value_or(0) };//group already assigned
					if( !groupPK || memberGroupPK==*groupPK ){
						member.erase( "groupId" );
						if( haveId )
							member["id"] = Json::AsNumber<IdentityPK::Type>( member, "memberId" );
						member.erase( "memberId" );
						groupMembers.emplace_back( move(member) );
					}
				}
				group["groupMembers"] = groupMembers;
			};
			if( !groups.is_null() )
				Json::Visit( groups, addMembers );
			Resume( move(groups) );
		}
		catch( boost::system::system_error& e ){
			ResumeExp( CodeException{e.code(), ELogTags::Access, ELogLevel::Debug} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	//{ mutation addGroup( "id":14, "memberId":[15,13] ) } - or "identityId":14, the map parent column's json name, which
	//QL::getChildParentParams accepts just as readily.  Reading only "id" with AsNumber made the other spelling a terminate:
	//this is ι, and QL::MutationAwaits::await_ready is a second noexcept frame above it (#13).
	α GroupHook::AddRemoveArgs( const QL::MutationQL& m )ι->std::pair<optional<GroupPK>, flat_set<IdentityPK::Type>>{
		let args = m.ExtrapolateVariables();
		optional<GroupPK> groupPK;
		if( let p = Json::FindNumber<GroupPK::Type>(args, "id"); p )
			groupPK = GroupPK{ *p };
		else if( let map = m.DBTable ? m.DBTable->Map : nullopt; map ){
			if( let p = Json::FindNumber<GroupPK::Type>(args, DB::Names::ToJson(map->Parent->Name)); p )
				groupPK = GroupPK{ *p };
		}
		flat_set<IdentityPK::Type> memberPKs;
		auto members = Json::FindValue( args, "memberId" );
		if( members ){
			if( members->is_array() ){
				for( auto& member : members->get_array() ){
					memberPKs.emplace( Json::FindNumber<IdentityPK::Type>(member,{}).value_or(0) );
				}
			}else if( members->is_number() )
				memberPKs.emplace( Json::FindNumber<IdentityPK::Type>(*members,{}).value_or(0) );
		}
		return {groupPK, memberPKs};
	}

	α GroupHook::AddBefore( const QL::MutationQL& m, UserPK /*executer*/, SL sl )ι->HookResult{
		if( m.TableName()=="groups" ){
			auto [groupPK, memberPKs] = AddRemoveArgs( m );
			try{
				THROW_IF( !groupPK, "Could not find the group id in '{}' - expected 'id' or the group's parent column.", serialize(m.Args) );
				Authorizer().TestAddGroupMember( *groupPK, move(memberPKs), sl );
			}catch( Exception& e ){
				return mu<ExceptionAwait<jvalue>>( e.Move() );
			}
		}
		return {};
	}
}