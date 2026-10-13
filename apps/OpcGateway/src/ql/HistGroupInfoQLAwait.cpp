#include "HistGroupInfoQLAwait.h"
#include <jde/access/Authorize.h>
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "HistQL.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α HistGroupInfoQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			Resume( _query.JsonName==GroupsCommand ? Groups() : Capabilities() );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
		co_return;
	}

	α HistGroupInfoQLAwait::Groups()ε->jvalue{
		let opc = _query.FindPtr<jstring>( "opc" );
		if( !opc || opc->empty() )
			throw Exception{ _sl, ExceptionArgs{EHttpStatus::BadRequest}, "{} names no connection.", GroupsCommand };
		vector<ExNodeId> nodes;
		for( let& j : HistQL::Nodes(_query, GroupsCommand, _sl) )
			nodes.push_back( ParseHistNode(j, _sl) );
		jobject y;
		let groupsQL = _query.FindTable( "groups" );
		if( !groupsQL )
			return y;
		let wantId = groupsQL->FindColumn( "id" ), wantName = groupsQL->FindColumn( "name" );
		let wantNodes = groupsQL->FindColumn( "nodes" ) || groupsQL->FindTable( "nodes" );
		let acl = AppClient()->Acl();
		jarray groups;
		for( let& host : HistHost::Instance().Groups() ){
			if( host->Slug()!=*opc )
				continue;
			jarray members;
			for( let& node : nodes ){
				if( host->Index(node) )
					members.push_back( node.ToJson() );
			}
			if( members.empty() )
				continue;
			try{
				acl->Test( "gateway", "histGroups", std::to_string(host->Id()), Access::ERights::Read, UserPK(), _sl );
			}
			catch( Exception& e ){//a group the caller may not read is none of theirs:  an answer, not an error to log.
				e.SetLevel( ELogLevel::NoLog );
				continue;
			}
			jobject group;
			if( wantId )
				group["id"] = host->Id();
			if( wantName )
				group["name"] = host->Name();
			if( wantNodes )
				group["nodes"] = move( members );
			groups.push_back( move(group) );
		}
		y["groups"] = move( groups );
		return y;
	}

	α HistGroupInfoQLAwait::Capabilities()ε->jvalue{
		let group = _query.AsNumber<HistGroupPK>( "group", _sl );
		let acl = AppClient()->Acl();
		acl->Test( "gateway", "histGroups", std::to_string(group), Access::ERights::Read, UserPK(), _sl );
		auto& hist = HistHost::Instance();
		hist.TestEnabled( _sl );
		if( !hist.Find(group) )
			throw Exception{ _sl, ExceptionArgs{EHttpStatus::NotFound}, "Group {} isn't one this gateway historizes.", group };
		let may = [&]( Access::ERights rights )->bool {
			return acl->IsEnforced( "gateway", "histGroups", std::to_string(group), rights, UserPK(), _sl ).has_value();
		};
		jobject y;
		if( _query.FindColumn("aggregates") ){
			jarray aggregates;
			for( let& aggregate : Hist::Aggregates )
				aggregates.emplace_back( aggregate.Name );
			y["aggregates"] = move( aggregates );
		}
		if( _query.FindColumn("update") )
			y["update"] = may( Access::ERights::Update );
		if( _query.FindColumn("delete") )
			y["delete"] = may( Access::ERights::Delete );
		return y;
	}
}