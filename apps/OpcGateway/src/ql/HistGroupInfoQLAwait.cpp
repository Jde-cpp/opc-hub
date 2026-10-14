#include "HistGroupInfoQLAwait.h"
#include <jde/access/Authorize.h>
#include <jde/db/meta/Table.h>
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "HistQL.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α HistGroupInfoQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			jvalue y;
			if( _query.JsonName==GroupsCommand )
				y = Groups();
			else if( _query.JsonName==CapabilitiesCommand )
				y = Capabilities();
			else{
				let groupId = _query.AsNumber<HistGroupPK>( "group", _sl );
				auto host = Host( groupId );
				auto groups = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)groupId}}) );
				if( groups.empty() )
					throw Exception{ _sl, ExceptionArgs{EHttpStatus::NotFound}, "Group {} not found.", groupId };
				auto rows = co_await Any( SelectHistGroupNodes({.Group=groupId}) );
				let members = groups.front().Template ? HistTemplateNodesById( co_await Any(SelectHistTemplateNodes({.Template=groups.front().Template})) ) : flat_map<HistTemplateNodePK,HistTemplateNodeRow>{};
				y = Thresholds( groups.front(), rows, members, *host );
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α HistGroupInfoQLAwait::Host( HistGroupPK group )ε->sp<HistGroupHost>{
		AppClient()->Acl()->Test( "gateway", "histGroups", std::to_string(group), Access::ERights::Read, UserPK(), _sl );
		auto& hist = HistHost::Instance();
		hist.TestEnabled( _sl );
		auto host = hist.Find( group );
		if( !host )
			throw Exception{ _sl, ExceptionArgs{EHttpStatus::NotFound}, "Group {} isn't one this gateway historizes.", group };
		return host;
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
			catch( Exception& e ){
				if( e.HttpStatus()==EHttpStatus::Unauthorized )//who the caller is, not what they may read:  the client's 401 is for this.
					throw;
				e.SetLevel( ELogLevel::NoLog );//a group the caller may not read is none of theirs:  an answer, not an error to log.
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
		Host( group );
		let acl = AppClient()->Acl();
		let may = [&]( Access::ERights rights )->bool {
			auto allowed = acl->CheckEnforced( "gateway", "histGroups", std::to_string(group), rights, UserPK(), _sl );
			if( !allowed )
				allowed.error().SetLevel( ELogLevel::NoLog );//a capability the caller lacks is an answer, as in Groups:  not a denial to log.
			return allowed.has_value();
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

	//Whether updateHistGroupNode would pass the caller:  the table's Update right, as Table::Authorize tests it for the stock op,
	//which an unenforced resource passes.  A lack is an answer, not a denial to log, as in Groups;  a caller Authorize doesn't
	//know is refused, as there.
	Ω mayUpdate( UserPK user, SL sl )ε->bool{
		try{
			GetViewPtr( "hist_group_nodes" )->Authorize( Access::ERights::Update, user, sl );
			return true;
		}
		catch( Exception& e ){
			if( e.HttpStatus()==EHttpStatus::Unauthorized )
				throw;
			e.SetLevel( ELogLevel::NoLog );
			return false;
		}
	}
	α HistGroupInfoQLAwait::Thresholds( const HistGroupRow& group, const vector<HistGroupNodeRow>& rows, const flat_map<HistTemplateNodePK,HistTemplateNodeRow>& members, const HistGroupHost& host )ε->jvalue{
		jobject y;
		if( _query.FindColumn("update") )
			y["update"] = mayUpdate( UserPK(), _sl );
		let nodesQL = _query.FindTable( "nodes" );
		if( !nodesQL )
			return y;
		let wantId = nodesQL->FindColumn( "id" ), wantMember = nodesQL->FindColumn( "templateNode" ), wantPath = nodesQL->FindColumn( "path" ), wantEnumeration = nodesQL->FindColumn( "enumeration" ), wantBand = nodesQL->FindColumn( "band" );
		let wantNode = nodesQL->FindColumn( "node" ) || nodesQL->FindTable( "node" );
		let thresholdsQL = nodesQL->FindTable( "thresholds" );
		let wantName = thresholdsQL && thresholdsQL->FindColumn( "name" ), wantValue = thresholdsQL && thresholdsQL->FindColumn( "value" ), wantSource = thresholdsQL && thresholdsQL->FindColumn( "source" );
		jarray nodes; nodes.reserve( rows.size() );
		for( let& row : rows ){
			let member = FindHistTemplateNode( members, row.TemplateNode );
			jobject node;
			if( wantId )
				node["id"] = row.Id;
			if( wantNode ){
				ExNodeId ex{ row.Node };
				if( let id = host.ServerId(row.Id); id )
					ex.nodeId.namespaceIndex = id->namespaceIndex;
				node["node"] = ex.ToJson();
			}
			if( wantMember )
				node["templateNode"] = row.TemplateNode ? jvalue{*row.TemplateNode} : jvalue{};
			if( wantPath )
				node["path"] = member ? jvalue{member->Path} : jvalue{};
			if( wantEnumeration ){
				let described = host.Description( row.Id );
				node["enumeration"] = described && described->Enumeration ? jvalue{*described->Enumeration} : jvalue{};
			}
			if( wantBand ){
				constexpr std::array<sv,5> names{ "none", "applied", "enumeration", "waiting", "noRange" };//EHistBand's order.
				let band = host.Band( row.Id );
				node["band"] = band ? jvalue{ names[underlying(*band)] } : jvalue{};
			}
			if( thresholdsQL ){
				jarray thresholds;
				for( auto& t : ResolveThresholdSources(row.Thresholds, member ? &member->Thresholds : nullptr, group.Thresholds) ){
					jobject j;
					if( wantName )
						j["name"] = t.Name;
					if( wantValue )
						j["value"] = move( t.Value );
					if( wantSource )
						j["source"] = t.Source;
					thresholds.push_back( move(j) );
				}
				node["thresholds"] = move( thresholds );
			}
			nodes.push_back( move(node) );
		}
		y["nodes"] = move( nodes );
		return y;
	}
}