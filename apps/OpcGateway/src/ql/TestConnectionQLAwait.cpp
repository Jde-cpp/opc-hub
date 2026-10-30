#include "TestConnectionQLAwait.h"
#include <jde/access/Authorize.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/str.h>
#include "../async/NamespacesAwait.h"
#include "../async/TestConnectAwait.h"
#include "../async/UAStrandAwait.h"
#include "../UAClient.h"
#include "GatewayQL.h"
#include "GatewayQLAwait.h"
#include "ServerCnnctnMAwait.h"
#define let const auto

namespace Jde::Opc::Gateway{
	α TestConnectionQLAwait::Query()ι->TAwait<jvalue>::Task{
		sp<UAClient> client;
		try{
			//Update, where ConnectAwait asks Read:  this connects the gateway to a url of the caller's choosing, which is editing a connection whether or not its row is saved yet.
			QL().Authorizer().Test( "gateway", "serverConnections", Access::ERights::Update, UserPK(), _sl );
			auto session = RequireSession();
			let slug = string{ Str::Trim(sv{_query.As<jstring>("opc")}) };
			if( auto e = ServerCnnctn::SlugError(slug, _sl); e )//the slug names the certificate file - never a path.
				e->Throw();
			ServerCnnctn cnnctn{ string{Str::Trim(sv{_query.As<jstring>("url")})} };
			THROW_IFX( cnnctn.Url.empty(), Exception(_sl, {EHttpStatus::BadRequest}, "A url is required.") );
			cnnctn.Slug = slug;
			cnnctn.Name = slug;
			//The certificateUri and the spec's connection settings, as the save merges them, under the save's rules:  the test connects
			//as the row Save would save, and a row Save would refuse is refused here (spec *Connection settings*).
			cnnctn = MergeSettings( _query, move(cnnctn), _sl );
			if( auto e = ServerCnnctn::SettingsError(cnnctn, _sl); e )
				e->Throw();
			let cred = SessionCredential( session->SessionId, session->UserPK, slug ).value_or( Credential{} );//what a connect to the saved row would present (ConnectAwait).
			client = co_await TestConnectAwait{ move(cnnctn), move(cred), _sl };
			jvalue y = jobject{};//MutationsAwait keys it under the command name.
			if( _query.ResultRequest ){
				optional<Namespaces> namespaces;
				if( _query.ResultRequest->FindTable("namespaces") )//a round trip - only when selected.
					namespaces = co_await Any( NamespacesAwait{client, _sl} );
				y = co_await UAStrandAwait<jvalue>{ client, [this, client, &namespaces]()->jvalue { return Describe( *_query.ResultRequest, client, namespaces ? &*namespaces : nullptr ); }, _sl };
			}
			client->StopProcessing();
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			if( client )
				client->StopProcessing();
			ResumeExp( move(e) );
		}
	}

	α TestConnectionQLAwait::Describe( const QL::TableQL& q, const sp<UAClient>& client, const Namespaces* namespaces )ε->jobject{
		jobject y = GatewayQLAwait::ServerDescription( q, client );
		if( q.FindColumn("securityPolicyUri") )
			y["securityPolicyUri"] = GatewayQLAwait::SecurityPolicyUri( client );
		if( q.FindColumn("securityMode") )
			y["securityMode"] = GatewayQLAwait::SecurityMode( client );
		if( let nsQL = q.FindTable("namespaces"); nsQL && namespaces )
			y["namespaces"] = GatewayQLAwait::NamespaceRows( *nsQL, *namespaces );
		return y;
	}
}