#include "OpcSessionsQLAwait.h"
#include <jde/access/Authorize.h>
#include <jde/ql/QLAwait.h>
#include "../auth/OpcServerSession.h"
#include "../GatewayAppClient.h"
#include "../UAClient.h"
#include "GatewayQL.h"
#define let const auto

namespace Jde::Opc::Gateway{
	//Anything beyond user{id} needs the users table, which lives on AppServer: the gateway has no access schema locally, and the remote sees identities created after startup (the local Authorize cache names those ""). Empty when nothing needs fetching.
	Ω usersQuery( const QL::TableQL* userQL, const vector<SessionCount>& counts )ι->string{
		string columns;
		if( userQL ){
			for( let& c : userQL->Columns ){
				if( c.JsonName!="id" )
					columns += c.JsonName + " ";
			}
		}
		flat_set<Jde::UserPK::Type> pks;
		for( let& c : counts ){
			if( c.UserPK )
				pks.insert( c.UserPK.Value );
		}
		if( columns.empty() || pks.empty() )
			return {};
		string ids;
		for( let pk : pks )
			ids += std::to_string( pk ) + ",";
		ids.pop_back();
		return Ƒ( "users(id:[{}]){{ id {}}}", ids, columns );
	}

	α OpcSessionsQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			QL().Authorizer().Test( "gateway", "sessions", Access::ERights::Read, UserPK(), _sl ); //gateway/sessions - declared in opcGateway-meta.jsonnet's `resources`, shipped unenforced;  enforced once an admin says so.
			let counts = SessionCounts();
			let userQL = _query.FindTable( "user" );
			let connectionQL = _query.FindTable( "connection" );
			flat_map<Jde::UserPK,jobject> users;
			if( auto q = usersQuery(userQL, counts); q.size() ){
				auto jusers = co_await *AppClient()->Query<jvalue>( move(q), {}, true, _sl );
				for( auto& v : Json::AsArray(jusers) ){
					auto& o = Json::AsObject( v );
					let pk = Jde::UserPK{ Json::AsNumber<Jde::UserPK::Type>(o, "id") };
					users.emplace( pk, move(o) );
				}
			}
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
				if( userQL ){
					if( !c.UserPK )
						row["user"] = nullptr; //anonymous/certificate credentials carry no identity.
					else{
						jobject user;
						let fetched = users.find( c.UserPK );
						for( let& column : userQL->Columns ){
							if( column.JsonName=="id" )
								user["id"] = c.UserPK.Value;
							else if( fetched!=users.end() ){
								if( auto p = fetched->second.find(column.JsonName); p!=fetched->second.end() )
									user[column.JsonName] = p->value();
							}
						}
						row["user"] = move( user );
					}
				}
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
}
