#include "ServerCnnctn.h"
#include <jde/db/IDataSource.h>
#include <jde/db/Row.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/Statement.h>
#include <jde/db/generators/WhereClause.h>
#include "../opcInternal.h"

#define let const auto

namespace Jde::Opc::Gateway{
	ServerCnnctn::ServerCnnctn( DB::Row&& r )ε:
		Id{ r.Get<uint32>(0) },
		Url{ r.TakeString(1) },
		CertificateUri{ r.TakeString(2) },
		DefaultBrowseNs{ r.GetOpt<uint16_t>(4).value_or(1) },//unset means 1 - what the SPA reads a NULL as, and so spells a namespace-1 segment bare (reviews/m3-closing.md #3, ruled 09-21; gateway-review #26 had unified both ctors on 0).
		IsDefault{ r.GetBit(3) },
		Name{ r.TakeString(5) },
		Deleted{ r.GetOpt<TimePoint>(7) },
		Slug{ r.TakeString(6) }
	{}
	ServerCnnctn::ServerCnnctn( jobject&& o )ε:
		Id{ Json::FindNumber<uint32>(o, "id").value_or(0) },
		Url{ Json::FindDefaultSV(o, "url") },
		CertificateUri{ Json::FindDefaultSV(o, "certificateUri") },//the QL's names, not the columns':  a serverConnection{…} result is what arrives here (security-matrix #7).
		DefaultBrowseNs{ Json::FindNumber<NsIndex>(o, "defaultBrowseNs").value_or(1) },//as the row ctor.
		Description{ Json::FindDefaultSV(o, "description") },
		IsDefault{ Json::FindBool(o, "isDefault").value_or(false) },//the value, not the optional:  braces take an optional<bool> for "has one", which the old, never-matching key hid.
		Name{ Json::FindDefaultSV(o, "name") },
		Deleted{ Json::FindTimePoint(o, "deleted") },
		Slug{ Json::FindDefaultSV(o, "slug") }
	{}
	α ServerCnnctn::ToJson()Ι->jobject{
		jobject o;
		o.emplace( "id", Id );
		//o.emplace("client_id", Id);
		o.emplace("url", Url);
		o.emplace("certificateUri", CertificateUri);
		o.emplace("isDefault", IsDefault);
		o.emplace("defaultBrowseNs", DefaultBrowseNs);
		o.emplace("name", Name);
		o.emplace("slug", Slug);
		o.emplace( "description", Description );
		o.emplace( "deleted", Deleted ? jvalue{ToIsoString(*Deleted)} : jvalue{} );
		return o;
	}

	α ServerCnnctnAwait::Select()ι->DB::SelectAwait::Task{
		let view = GetViewPtr( "server_connections" );
		DB::WhereClause where;
		if( !_includeDeleted )
			where.Add( view->GetColumnPtr("deleted"), nullptr );
		if( _key ){
			if( _key->IsPK() )
				where.Add( view->GetColumnPtr("server_connection_id"), _key->PK() );
			else{
				if( _key->NK().size() )
					where.Add( view->GetColumnPtr("slug"), _key->NK() );
				else
					where.Add( view->GetColumnPtr("is_default"), true );
			}
		}
		auto statement = DB::Statement{ {view->GetColumns({"server_connection_id", "url", "certificate_uri", "is_default", "default_browse_ns", "name", "slug", "deleted"})}, {view}, move(where) };
		try{
			vector<ServerCnnctn> y;
			auto rows = co_await DS()->SelectAsync( statement.Move() );
			for( auto&& row : rows )
				y.push_back( ServerCnnctn{move(row)} );
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}