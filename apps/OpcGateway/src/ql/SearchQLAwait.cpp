#include "SearchQLAwait.h"
#include <jde/access/Authorize.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include "../async/ConnectAwait.h"
#include "../UAClient.h"
#include "GatewayQL.h"
#define let const auto

namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α SearchQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			QL().Authorizer().Test( "gateway", "search", Access::ERights::Read, UserPK(), _sl ); //gateway/search - declared in opcGateway-meta.jsonnet's `resources`, shipped unenforced;  enforced once an admin says so.
			auto session = Session(); THROW_IF( !session, "No Session for query" );
			let textPtr = _query.FindPtr<jstring>( "text" );
			let text = textPtr ? Str::ToLower( Str::Trim(sv{*textPtr}) ) : string{};
			let limit = _query.TryNumber<uint>( "limit" ).value_or( Settings::FindNumber<uint>("/gateway/search/limit").value_or(20) );//value_or mirrors config/Opc.Gateway.jsonnet.
			let refresh = _query.Find<bool>( "refresh" ).value_or( false );
			let opcPtr = _query.FindPtr<jstring>( "opc" );
			let opc = opcPtr ? string{*opcPtr} : string{};
			vector<sp<UAClient>> clients;
			if( text.size() ){
				if( opcPtr ){
					if( auto client = UAClient::Find( opc, SessionCredential(session->SessionId, session->UserPK, opc).value_or(Credential{}) ); client && client->Connected )
						clients.push_back( move(client) );
				}
				else{
					for( auto& client : UAClient::LiveClients() ){
						if( client->Credential==SessionCredential(session->SessionId, session->UserPK, client->Slug()).value_or(Credential{}) )
							clients.push_back( move(client) );
					}
				}
			}
			for( auto& client : clients )
				client->Index().Start( client, refresh );//every crawl in flight before the first wait - one browse per client strand, concurrent across clients.
			struct Hit{ sp<UAClient> Client; NodeIndex::Entry E; };
			vector<Hit> hits;
			for( auto& client : clients ){
				try{
					co_await client->Index().Ready( client, refresh, _sl );
					for( auto& e : client->Index().Search(text, limit) )
						hits.push_back( {client, move(e)} );
				}
				catch( runtime_error& e ){
					if( opcPtr )
						throw;
					WARN( "[{}]search skipped '{}': {}", hex(client->Handle()), client->Slug(), e.what() );//one dead connection must not sink the fan-out.
				}
			}
			if( clients.size()>1 ){
				std::ranges::sort( hits, []( const Hit& a, const Hit& b ){
					return a.E.Rank!=b.E.Rank ? a.E.Rank<b.E.Rank : a.E.Depth!=b.E.Depth ? a.E.Depth<b.E.Depth : a.E.NameLower<b.E.NameLower;
				} );
				if( limit && hits.size()>limit )
					hits.resize( limit );
			}
			jarray rows; rows.reserve( hits.size() );
			for( let& hit : hits )
				rows.push_back( Row(*hit.Client, hit.E) );
			_query.ReturnRaw = true; //TablesAwait re-wraps under ReturnName().
			Resume( jvalue{move(rows)} ); //not TransformResult:  'search' is a singular name, and that would collapse the rows to the first one.
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α SearchQLAwait::Row( const UAClient& client, const NodeIndex::Entry& e )ι->jobject{
		jobject row;
		if( auto connectionQL = _query.FindTable("connection"); connectionQL ){
			jobject connection;
			if( connectionQL->FindColumn("slug") )
				connection["slug"] = client.Slug();
			if( connectionQL->FindColumn("name") )
				connection["name"] = client.Name();
			row["connection"] = move( connection );
		}
		if( _query.FindColumn("id") )
			e.Id.Add( row );
		if( _query.FindColumn("path") )
			row["path"] = e.Path;
		if( _query.FindColumn("name") )
			row["name"] = e.Name;
		if( auto browseQL = _query.FindTable("browse"); browseQL ){
			jobject browse;
			if( browseQL->FindColumn("ns") )
				browse["ns"] = e.BrowseNs;
			if( browseQL->FindColumn("name") )
				browse["name"] = e.Browse;
			row["browse"] = move( browse );
		}
		if( _query.FindColumn("nodeClass") )
			row["nodeClass"] = (uint)e.Class;
		if( _query.FindColumn("depth") )
			row["depth"] = e.Depth;
		return row;
	}
}
