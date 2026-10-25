#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	//opcSessions{ connection{slug} type user{id slug name} count } - the live OPC credential cache (auth/OpcServerSession), one row per (connection, credential type, user).
	//No UAClient: reads _sessions directly, so it bypasses GatewayQLAwait's ConnectAwait. user{...} columns beyond id come from AppServer's users table.
	struct OpcSessionsQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		OpcSessionsQLAwait( QL::TableQL&& q, QL::Creds&& creds, SRCE )ι:base{ move(q), move(creds), sl }{}
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
	//serverConnections{ … opcSessions{count} opcConnections{count} connectionStatus{name error} } - grafts live state onto the DB rows: opcSessions = web
	//sessions holding a credential on the slug (auth cache), opcConnections = open UAClients (idle drain/disconnect shrink it), connectionStatus = those
	//counts read as Connected|Idle|Error against UAClient::ConnectErrors(), so a broken slug is distinguishable from a merely unused one.
	struct ServerCnnctnSessionsQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		ServerCnnctnSessionsQLAwait( QL::TableQL&& q, QL::Creds&& creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName.starts_with("serverConnection") && std::ranges::any_of( Tables, [&q]( sv t ){ return q.FindTable(t)!=nullptr; } ); }
		static constexpr array<sv,3> Tables{ "opcSessions", "opcConnections", "connectionStatus" };//opcSessions is OpcSessionsQLAwait's at the top level too.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
	//endpoint{ url policies{ uri mode } trustedCertDirs rejectedCertDir sessions{ id type policy mode since user{ id slug name } } } - the
	//gateway's own OPC UA endpoint (servers spec *Web*), the web's Endpoint tab;  null without a /gateway/server block.  Its url, the
	//security policy and mode of each endpoint it offers, the directories a client certificate is trusted from and a refused one is
	//written to - trust stays a file operation, so the page says where - and the sessions it holds now (UAAccess::SessionList):  each
	//with its UA session id, the token type, the channel's policy and mode, when it first activated, and the hub user its token
	//resolved to - null for an anonymous session, or a username the hub has no user for yet.  The sessions are gated as opcSessions
	//is, gateway/sessions Read, the same telemetry;  the rest any session reads, as the connections are.  user{…} beyond id comes
	//from the AppServer's users table, as opcSessions fetches it.  No UAClient, no lock of the endpoint's:  GatewayServer::Describe.
	struct EndpointQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		EndpointQLAwait( QL::TableQL&& q, QL::Creds&& creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		static constexpr sv Command{ "endpoint" };
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}