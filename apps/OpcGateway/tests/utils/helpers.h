#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/access/usings.h>
#include <jde/db/db.h>
#include <jde/db/Key.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/QLAwait.h>
#include "../../src/usings.h"
#include "../../src/types/ServerCnnctn.h"

namespace Jde::Opc::Gateway{ enum class ETokenType : uint8; struct UAClient; }
namespace Jde::Opc::Gateway::Tests{
	const static string OpcServerSlug{ "opcTestsConnectionSlug" };
	//The default row is OpcServerSlug against the embedded server with /opc/urn as its certificateUri; the three-argument form
	//is a row of the caller's shape - NoSecurityTests needs one without a certificateUri, ExternalServerTests a third-party url.
	struct CreateServerCnnctnAwait : TAwaitEx<ServerCnnctnPK,QL::QLAwait<jobject>::Task>{
		using base = TAwaitEx<ServerCnnctnPK,QL::QLAwait<jobject>::Task>;
		CreateServerCnnctnAwait( SRCE )ι:base{ sl }{}
		CreateServerCnnctnAwait( string slug, string url, string certificateUri, SRCE )ι:base{ sl },_slug{move(slug)},_url{move(url)},_certificateUri{move(certificateUri)}{}
		α Execute()ι->QL::QLAwait<jobject>::Task override;
	private:
		optional<string> _slug, _url, _certificateUri;
	};
	α CreateServerCnnctn()ε->ServerCnnctnPK;

	struct PurgeServerCnnctnAwait : TAwaitEx<uint,QL::QLAwait<>::Task>{
		PurgeServerCnnctnAwait( optional<ServerCnnctnPK> pk )ι:_pk{pk}{}
		α Execute()ι->QL::QLAwait<>::Task override;
	private:
		optional<ServerCnnctnPK> _pk;
	};
	α PurgeServerCnnctn( optional<ServerCnnctnPK> id=nullopt )ι->uint;

	α GetConnection( str slug )ε->ServerCnnctn;
	α GetConnection( str slug, str url, str certificateUri )ε->ServerCnnctn;//the row of that shape, created (with its provider) when absent, as the one-argument form does for OpcServerSlug.
	α SelectServerCnnctn( DB::Key id )ι->optional<ServerCnnctn>;

	α AvailableUserTokens( sv url )ε->ETokenType;
	α Negotiated( const sp<UAClient>& client )ι->string;//"ok (<policy>/<mode>)" - what the client's session was opened with, read on its strand; "ok" when it cannot be read.
	α Query( sv ql, jobject vars={}, bool raw=true )ε->jobject;
	//The hist_groups resource enforced, with the gateway's user granted everything on it through a role of its own, or
	//soft-deleted again, as it ships:  a group's history edits need a grant on an enforced resource (spec *Authorization*).
	α EnforceHistGroups( bool on )ε->void;
	//A group's own access resource, its id the criteria, enforced with `allowed` granted the suite's user:  it stands in for histGroups'
	//root for that group alone (Authorize::Test), and counts only while the root is enforced (EnforceHistGroups).  The grant takes Administer too, which enforcing - restoring - the row needs.  Through
	//a role:  a direct grant's acl event reaches every app's authorizer, the embedded OpcServer's among them, which never saw the
	//gateway's row and asserts (the acl subscriptions' schema filter, reviews/soak-findings.md).  Returns once the gateway's authorizer
	//has the resource;  the grant can reach it after.
	α ScopeHistGroup( uint32 group, Access::ERights allowed )ε->void;
	α UnscopeHistGroup( uint32 group )ε->void;//its resource deleted, so the group inherits the root again;  returns once the gateway's authorizer has heard.
	Ξ GatewayPort()ι{ return Settings::FindNumber<PortType>("/http/gateway/port").value_or(2068); }
}