#pragma once
#include <jde/ql/IQLAwaitExe.h>
#include "../Discovery.h"

namespace Jde::Opc::Gateway{
	//discover( url, servers, requestTimeout ){ … } - the connection page's Discover (docs/clients/spec.md *Discovery*):  what a url
	//offers, read on a throwaway None-policy client of open62541's own, as the connect diagnostics read theirs (Discovery.h), on the
	//discovery pool (DiscoveryPool), never the connects'.  `servers: true` is FindServers at `url` - a Local Discovery Server's, or any
	//server's, which answers with itself - and answers each server:  { applicationUri productUri applicationName applicationType
	//gatewayServerUri discoveryProfileUri discoveryUrls }.  Otherwise GetEndpoints at a server url answers each endpoint:  { url
	//applicationUri policy mode securityLevel tokenPolicies{ type policy policyId } certificate{ subject issuer notBefore notAfter uri
	//thumbprint fingerprint } }, the certificate decoded by Crypto::Certificate and null on an endpoint that carries none.  Either is
	//an array, keyed under `discover`.  `requestTimeout`, milliseconds, bounds the connect and the call, as a row's does, up to
	//MaxTimeout;  null is the gateway's requestTimeout, held to MaxTimeout.  A failure - a url that isn't reached, a service refused -
	//is the mutation's error.  A mutation, not a query, as testConnection is and for its reason:  it connects the gateway to a url of
	//the caller's choosing, which a GET - a cross-site <img> on an unenforced install - must not do;  under testConnection's right,
	//Update on gateway/serverConnections, since what it finds goes into a connection's row.  Nothing is registered and no certificate
	//is issued:  the channel is unsecured.
	struct DiscoverQLAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		DiscoverQLAwait( QL::MutationQL&& m, QL::Creds&& creds, SRCE )ι:base{ move(m), move(creds), sl }{}
		Ω Test( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{ return m.CommandName==Command ? mu<DiscoverQLAwait>( move(m), move(creds), sl ) : nullptr; }
		static constexpr sv Command{ "discover" };//a system mutation (QL::SetSystemMutations), taken whole:  discover is no verb the engine knows.
		//The longest a discover holds a discovery-pool thread, past the lookup's 3 s:  with it, DiscoveryPool::Stop's join stays inside
		//the 30 s shutdown watchdog (process.cpp), and a url that never answers frees its thread within 18 s.
		static constexpr std::chrono::milliseconds MaxTimeout{ 15'000 };
		//The rows as the request selects them - pure, so the pool thread builds them.
		Ω ServerRows( const QL::TableQL& q, const vector<Discovery::Server>& servers )ι->jarray;
		Ω EndpointRows( const QL::TableQL& q, const vector<Discovery::Endpoint>& endpoints )ι->jarray;
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}