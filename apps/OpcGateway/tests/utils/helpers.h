#pragma once
#include <open62541/server.h>
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/co/Await.h>
#include <jde/access/usings.h>
#include <jde/db/db.h>
#include <jde/db/Key.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/QLAwait.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include "../../src/usings.h"
#include "../../src/types/ServerCnnctn.h"

namespace Jde::Web{ struct Jwt; }
namespace Jde::Access{ struct Authorize; }
namespace Jde::App::Client{ struct IAppClient; }
namespace Jde::Opc::Gateway{ enum class ETokenType : uint8; struct UAClient; struct Credential; }
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
	α PurgeServerCnnctn( str slug )ι->void;//the row for `slug`, when there is one.

	α GetConnection( str slug )ε->ServerCnnctn;
	α GetConnection( str slug, str url, str certificateUri )ε->ServerCnnctn;//the row of that shape, created (with its provider) when absent, as the one-argument form does for OpcServerSlug.
	α SelectServerCnnctn( DB::Key id )ι->optional<ServerCnnctn>;

	α AvailableUserTokens( sv url )ε->ETokenType;
	α ReadValue( const sp<UAClient>& client, const NodeId& node )ε->Value;//as the client reads it, its status unchecked.
	α ReadValue( UA_Server* ua, const NodeId& node )ι->Value;//as the server holds it.
	α Negotiated( const sp<UAClient>& client )ι->string;//"ok (<policy>/<mode>)" - what the client's session was opened with, read on its strand; "ok" when it cannot be read.
	//`pred` polled every `interval` until it holds;  false once `timeout` passes, though it is always asked once.
	α WaitUntil( function<bool()> pred, Duration timeout=std::chrono::seconds{10}, Duration interval=std::chrono::milliseconds{10} )ι->bool;
	//`schema`'s `slug` resource - the row for `criteria`, or the root - enforced, or soft-deleted again as it ships:  `deleted` is
	//the switch, so restore enforces.  Returns once `authorizer` has heard, which it does over its subscription after the mutation
	//returns;  the first form is the gateway's own client and authorizer.
	α EnforceResource( sv schema, sv slug, bool on, sv criteria={} )ε->void;
	α EnforceResource( App::Client::IAppClient& app, Access::Authorize& authorizer, sv schema, sv slug, bool on, sv criteria={} )ε->void;
	//`user` given `allowed` on `schema`'s `slug` resource (the row for `criteria`, or the root) through a role of its own, `role`,
	//made with the grant the first time and kept after.  A role rather than a direct grant:  a direct grant's acl event reaches
	//every app's authorizer, the embedded OpcServer's among them, which never saw the gateway's row and asserts (the acl
	//subscriptions' schema filter, reviews/soak-findings.md).
	α GrantRole( App::Client::IAppClient& app, sv role, sv schema, sv slug, sv criteria, Access::ERights allowed, UserPK user )ε->void;
	//The hist_groups resource enforced, with the gateway's user granted everything on it through a role of its own, or
	//soft-deleted again, as it ships:  a group's history edits need a grant on an enforced resource (spec *Authorization*).
	α EnforceHistGroups( bool on )ε->void;
	//histGroups enforced until the cleanup returned runs, which opens it again:  armed first, so an enforce that throws part
	//way, which can leave it restored, is opened again too.
	Ξ EnforcedHistGroups()ε{
		absl::Cleanup open = []{ try{ EnforceHistGroups( false ); }catch( const std::exception& e ){ ADD_FAILURE() << "histGroups wasn't opened again:  " << e.what(); } };
		EnforceHistGroups( true );
		return open;
	}
	//A group's own access resource, its guid the criteria (#357), enforced with `allowed` granted the suite's user:  it stands in for
	//histGroups' root for that group alone (Authorize::Test), and counts only while the root is enforced (EnforceHistGroups).  The
	//grant takes Administer too, which enforcing - restoring - the row needs (GrantRole).  Returns once the gateway's authorizer has
	//the resource;  the grant can reach it after.
	α ScopeHistGroup( uint32 group, Access::ERights allowed )ε->void;
	α UnscopeHistGroup( uint32 group )ε->void;//its resource deleted, so the group inherits the root again;  returns once the gateway's authorizer has heard.
	Ξ GatewayPort()ι{ return Settings::FindNumber<PortType>("/http/gateway/port").value_or(2068); }
	Ξ OpcUrl()ι->string{ return string{ Settings::FindSV("/opc/url").value_or("opc.tcp://127.0.0.1:4940") }; }//the embedded OpcServer's, or the server /opc names.
	Ξ OpcUrn()ι->string{ return string{ Settings::FindSV("/opc/urn").value_or("urn:open62541.server.application") }; }//its certificateUri.
	//The IA examples' lamp Intensity, a writable Float:  the node the subscription suites watch.
	inline const NodeId Intensity{ 4, 6017 };
	struct GatewayClientSocket;
	α Socket()ι->GatewayClientSocket&;//the suites' shared socket to the gateway, connected on first use as the AppClient's session.
	α Jwt()ε->const Web::Jwt&;//the AppClient's, fetched on first use:  its payload is the token the suites present, its user the session's.
	α SessionCredential( optional<UserPK> user={} )ε->Credential;//Jwt()'s payload, with `user` set when given - Jwt().UserPK for the session's own.
	//A node of the embedded OpcServer given a DataType of namespace 0 for a test, holding no value, and given its own DataType
	//and value back when this goes.
	struct RetypedNode final{
		RetypedNode( UA_Server* ua, NodeId node, UA_UInt32 dataType )ε;
		~RetypedNode();
		RetypedNode( const RetypedNode& )=delete;
		RetypedNode& operator=( const RetypedNode& )=delete;
	private:
		UA_Server* _ua;
		NodeId _node;
		Value _value;
		NodeId _dataType;
	};
}