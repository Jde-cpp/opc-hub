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
}
