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
	//serverConnections{ … opcSessions{count} opcConnections{count} connectionStatus{name error} rejectedCertificate{…} clientCertificate{…}
	//clientDiagnostics{…} } - grafts live state onto the DB rows.  opcSessions = web sessions holding a credential on the slug (auth cache),
	//opcConnections = open UAClients (idle drain/disconnect shrink it), connectionStatus = those counts read as Connected|Idle|Error against
	//UAClient::ConnectErrors(), so a broken slug is distinguishable from a merely unused one - the live-usage telemetry, which needs
	//gateway/sessions Read as well.  clientDiagnostics{ connectedSince reconnects lastBreak sessionAge monitoredItems parkedItems
	//reconnectWaiting clients } is the same telemetry for the caller's own client of the slug, the one its session would use
	//(SessionCredential, UAClient::Find), else the lost one its reconnect chain holds (UAClient::LostDiagnostics), null with neither - this
	//never connects (docs/clients/spec.md *Diagnostics*, #427):  the client's start, breaks and session age (UAClient::ClientDiagnostics),
	//the items it monitors, and the slug's - its parked items, whether a reconnect chain of it is waiting, and its client count,
	//opcConnections' number.
	//rejectedCertificate reads the <thumbprint>.der our verifier wrote for a slug whose server certificate we rejected (ServerTrust,
	//UAClient::RejectedThumbprint), null for a slug with no standing rejection;  clientCertificate reads the gateway's own issued certificate
	//for the slug (UAClient::CryptoSettings), the one it presents, so the page downloads its PEM, null when the file does not exist.  Both
	//are the connection's, not telemetry:  under the table's Read alone, as the rest of the row is.
	struct ServerCnnctnSessionsQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		ServerCnnctnSessionsQLAwait( QL::TableQL&& q, QL::Creds&& creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName.starts_with("serverConnection") && std::ranges::any_of( Tables, [&q]( sv t ){ return q.FindTable(t)!=nullptr; } ); }
		static constexpr array<sv,6> Tables{ "opcSessions", "opcConnections", "connectionStatus", "rejectedCertificate", "clientCertificate", "clientDiagnostics" };//opcSessions is OpcSessionsQLAwait's at the top level too.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
	//endpoint{ url policies{ uri mode } trustedCertDirs rejectedCertDir served( group ){ id nodeId path } sessions{ id type policy mode
	//since user{ id slug name } } } - the gateway's own OPC UA endpoint (servers spec *Web*), the web's Endpoint tab and a group's page;
	//null without a /gateway/server block.  Its url, the security policy and mode of each endpoint it offers, the directories a client
	//certificate is trusted from and a refused one is written to - trust stays a file operation, so the page says where - a group's
	//members as it serves them, null for a group it doesn't serve (GatewayServer::ServedNodes), and the sessions it holds now
	//(UAAccess::SessionList):  each with its UA session id, the token type, the channel's policy and mode, when it first activated, and
	//the hub user its token resolved to - null for an anonymous session, or a username the hub has no user for yet.  All of it needs
	//gateway/endpoint Read;  the sessions gateway/sessions Read as well, as opcSessions, the same telemetry;  served the group's Read,
	//as its rows.  user{…} beyond id comes from the AppServer's users table, as opcSessions fetches it.  No UAClient, no lock of the
	//endpoint's:  GatewayServer::Describe and ServedNodes.
	struct EndpointQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		EndpointQLAwait( QL::TableQL&& q, QL::Creds&& creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		static constexpr sv Command{ "endpoint" };
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}