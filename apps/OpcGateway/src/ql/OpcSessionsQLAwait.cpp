#include "OpcSessionsQLAwait.h"
#include <jde/ql/QLAwait.h>
#include "../auth/OpcServerSession.h"
#include "../GatewayAppClient.h"
#include "../server/GatewayServer.h"
#include "GatewayQL.h"
#define let const auto

namespace Jde::Opc::Gateway{
	//Anything beyond user{id} needs the users table, which lives on AppServer: the gateway has no access schema locally, and the remote sees identities created after startup (the local Authorize cache names those ""). Empty when nothing needs fetching.
	Ω usersQuery( const QL::TableQL* userQL, const flat_set<Jde::UserPK::Type>& pks )ι->string{
		string columns;
		if( userQL ){
			for( let& c : userQL->Columns ){
				if( c.JsonName!="id" )
					columns += c.JsonName + " ";
			}
		}
		if( columns.empty() || pks.empty() )
			return {};
		string ids;
		for( let pk : pks )
			ids += std::to_string( pk ) + ",";
		ids.pop_back();
		return Ƒ( "users(id:[{}]){{ id {}}}", ids, columns );
	}
	//The users the query named, by pk, as usersQuery asked for them.
	Ω fetchedUsers( jvalue&& jusers )ι->flat_map<Jde::UserPK,jobject>{
		flat_map<Jde::UserPK,jobject> y;
		for( auto& v : Json::AsArray(jusers) ){
			auto& o = Json::AsObject( v );
			let pk = Jde::UserPK{ Json::AsNumber<Jde::UserPK::Type>(o, "id") };
			y.emplace( pk, move(o) );
		}
		return y;
	}
	//user{…} for a row:  null for no identity - anonymous and certificate credentials carry none, nor a username login the hub has no
	//user for yet - else its id and what the fetch found of the rest.
	Ω userJson( const QL::TableQL& userQL, Jde::UserPK pk, const flat_map<Jde::UserPK,jobject>& users )ι->jvalue{
		if( !pk )
			return jvalue{};
		jobject user;
		let fetched = users.find( pk );
		for( let& column : userQL.Columns ){
			if( column.JsonName=="id" )
				user["id"] = pk.Value;
			else if( fetched!=users.end() ){
				if( auto p = fetched->second.find(column.JsonName); p!=fetched->second.end() )
					user[column.JsonName] = p->value();
			}
		}
		return user;
	}

	α OpcSessionsQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			QL().Authorizer().Test( "gateway", "sessions", Access::ERights::Read, UserPK(), _sl ); //gateway/sessions - declared in opcGateway-meta.jsonnet's `resources`, shipped unenforced;  enforced once an admin says so.
			let counts = SessionCounts();
			let userQL = _query.FindTable( "user" );
			let connectionQL = _query.FindTable( "connection" );
			flat_set<Jde::UserPK::Type> pks;
			for( let& c : counts ){
				if( c.UserPK )
					pks.insert( c.UserPK.Value );
			}
			flat_map<Jde::UserPK,jobject> users;
			if( auto q = usersQuery(userQL, pks); q.size() )
				users = fetchedUsers( co_await *AppClient()->Query<jvalue>(move(q), {}, true, _sl) );
			jarray rows;
			for( let& c : counts ){
				jobject row;
				if( connectionQL ){
					jobject connection;
					if( connectionQL->FindColumn("slug") )
						connection["slug"] = c.Connection;
					row["connection"] = move( connection );
				}
				if( _query.FindColumn("type") )
					row["type"] = TokenTypeName( c.Type );
				if( userQL )
					row["user"] = userJson( *userQL, c.UserPK, users );
				if( _query.FindColumn("count") )
					row["count"] = c.Count;
				rows.push_back( move(row) );
			}
			_query.ReturnRaw = true; //TablesAwait re-wraps under ReturnName().
			Resume( _query.TransformResult(move(rows)) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α ServerCnnctnSessionsQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			QL().Authorizer().Test( "gateway", "sessions", Access::ERights::Read, UserPK(), _sl ); //one gate covers both grafts - the same live-usage telemetry.
			auto sessionsQL = _query.ExtractTable( "opcSessions" ); //before the select: SelectAwait derefs every child's DBTable(), and these have none.
			auto connectionsQL = _query.ExtractTable( "opcConnections" );
			auto statusQL = _query.ExtractTable( "connectionStatus" );
			let addedSlug = _query.AddColumn( "slug" ); //the join key - both totals are per slug.
			_query.ReturnRaw = true;
			auto rows = co_await QL::QLAwait{ move(_query), UserPK(), _sl }; //UserPK ctor: no IQL, so CustomQuery is not re-entered; SelectAwait still authorizes the read.
			flat_map<ServerCnnctnNK,uint32> sessionTotals;
			if( sessionsQL ){
				for( let& c : SessionCounts() )
					sessionTotals[c.Connection] += c.Count;
			}
			let connectionTotals = connectionsQL || statusQL ? UAClient::ConnectionCounts() : flat_map<ServerCnnctnNK,uint32>{};
			let connectErrors = statusQL ? UAClient::ConnectErrors() : flat_map<ServerCnnctnNK,string>{};
			auto graft = []( const flat_map<ServerCnnctnNK,uint32>& totals, const QL::TableQL& child, str slug )ι->jobject {
				jobject y;
				if( child.FindColumn("count") ){
					let p = totals.find( slug );
					y["count"] = p==totals.end() ? 0 : p->second;
				}
				return y;
			};
			//An open client wins over a remembered failure: the error is only the *last* attempt, and a later one succeeding clears it anyway.
			auto graftStatus = [&connectionTotals, &connectErrors]( const QL::TableQL& child, str slug )ι->jobject {
				jobject y;
				let client = connectionTotals.find( slug );
				let error = connectErrors.find( slug );
				if( child.FindColumn("name") )
					y["name"] = client!=connectionTotals.end() && client->second ? "Connected" : error!=connectErrors.end() ? "Error" : "Idle";
				if( child.FindColumn("error") ){
					if( error==connectErrors.end() )
						y["error"] = nullptr;
					else
						y["error"] = error->second;
				}
				return y;
			};
			Json::Visit( rows, [&]( jobject& o ){
				let slug = Json::AsString( o, "slug" );
				if( addedSlug )
					o.erase( "slug" );
				if( sessionsQL )
					o["opcSessions"] = graft( sessionTotals, *sessionsQL, slug );
				if( connectionsQL )
					o["opcConnections"] = graft( connectionTotals, *connectionsQL, slug );
				if( statusQL )
					o["connectionStatus"] = graftStatus( *statusQL, slug );
			} );
			Resume( move(rows) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α EndpointQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			QL().Authorizer().Test( "gateway", "endpoint", Access::ERights::Read, UserPK(), _sl );//gateway/endpoint - declared in opcGateway-meta.jsonnet's `resources`, shipped unenforced.
			let server = GatewayServer::Instance();
			if( !server ){//no block:  null, which the page reads as no endpoint.
				Resume( jvalue{} );
				co_return;
			}
			let sessionsQL = _query.FindTable( "sessions" );
			if( sessionsQL )
				QL().Authorizer().Test( "gateway", "sessions", Access::ERights::Read, UserPK(), _sl );
			let description = server->Describe( sessionsQL!=nullptr );
			jobject y;
			if( _query.FindColumn("url") )
				y["url"] = description.Url;
			if( let policiesQL = _query.FindTable("policies"); policiesQL ){
				jarray policies;
				for( let& [uri, mode] : description.Policies ){
					jobject policy;
					if( policiesQL->FindColumn("uri") )
						policy["uri"] = uri;
					if( policiesQL->FindColumn("mode") )
						policy["mode"] = SecurityModeName( mode );
					policies.push_back( move(policy) );
				}
				y["policies"] = move( policies );
			}
			if( _query.FindColumn("trustedCertDirs") ){
				jarray dirs;
				for( let& dir : description.TrustedCertDirs )
					dirs.emplace_back( dir.string() );
				y["trustedCertDirs"] = move( dirs );
			}
			if( _query.FindColumn("rejectedCertDir") )
				y["rejectedCertDir"] = description.RejectedCertDir.empty() ? jvalue{} : jvalue{ description.RejectedCertDir.string() };
			if( let servedQL = _query.FindTable("served"); servedQL ){
				jvalue served;
				if( let group = server->ServedNodes(servedQL->AsNumber<HistGroupPK>("group", _sl)); group ){
					AppClient()->Acl()->Test( "gateway", "histGroups", group->Guid, Access::ERights::Read, UserPK(), _sl );//as the group's own rows are.
					jarray rows;
					for( let& node : group->Nodes ){
						jobject row;
						if( servedQL->FindColumn("id") )
							row["id"] = node.Id;
						if( servedQL->FindColumn("nodeId") )
							row["nodeId"] = node.NodeId;
						if( servedQL->FindColumn("path") )
							row["path"] = node.Path;
						rows.push_back( move(row) );
					}
					served = move( rows );
				}
				y["served"] = move( served );
			}
			if( sessionsQL ){
				let userQL = sessionsQL->FindTable( "user" );
				flat_set<Jde::UserPK::Type> pks;
				for( let& session : description.Sessions ){
					if( session.User )
						pks.insert( session.User.Value );
				}
				flat_map<Jde::UserPK,jobject> users;
				if( auto q = usersQuery(userQL, pks); q.size() )
					users = fetchedUsers( co_await *AppClient()->Query<jvalue>(move(q), {}, true, _sl) );
				jarray rows;
				for( let& session : description.Sessions ){
					jobject row;
					if( sessionsQL->FindColumn("id") )
						row["id"] = session.Id;
					if( sessionsQL->FindColumn("type") )
						row["type"] = session.Type;
					if( sessionsQL->FindColumn("policy") )
						row["policy"] = session.PolicyUri;
					if( sessionsQL->FindColumn("mode") )
						row["mode"] = SecurityModeName( session.Mode );
					if( sessionsQL->FindColumn("since") )
						row["since"] = ToIsoString( session.Since );
					if( userQL )
						row["user"] = userJson( *userQL, session.User, users );
					rows.push_back( move(row) );
				}
				y["sessions"] = move( rows );
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}