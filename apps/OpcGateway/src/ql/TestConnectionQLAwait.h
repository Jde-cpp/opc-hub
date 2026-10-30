#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	struct UAClient; struct Namespaces;
	//testConnection( opc, url, certificateUri, securityMode, securityPolicy, sessionTimeout, requestTimeout, pingInterval, ttl, locale ){
	//applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls securityPolicyUri
	//securityMode namespaces{ index uri } } - what serverDescription, the two security attributes and namespaces answer for a saved
	//connection, for settings that are not saved:  `opc` is the slug the connection has or will have, which names its certificate
	//and the session's credential;  `url`, `certificateUri` and the spec's connection settings (optional, null the gateway's value as
	//the row's null is) are the values to try, under the save's rules (ServerCnnctn::SettingsError).  The gateway connects as it
	//would to the saved row, reads the attributes and lets the client go - no client is
	//registered, and a failure is this mutation's error, never the slug's connectionStatus.  The connection page's Test Connection,
	//before the first save and to retry a saved connection without a reload.  A mutation, not a query:  it connects to a url of the
	//caller's choosing and issues the slug's certificate, which a GET - a cross-site <img> on an unenforced install - must not do.
	struct TestConnectionQLAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		TestConnectionQLAwait( QL::MutationQL&& m, QL::Creds&& creds, SRCE )ι:base{ move(m), move(creds), sl }{}
		Ω Test( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{ return m.CommandName==Command ? mu<TestConnectionQLAwait>( move(m), move(creds), sl ) : nullptr; }
		static constexpr sv Command{ "testConnection" };//a system mutation (QL::SetSystemMutations), taken whole:  test is no verb.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
		α Describe( const QL::TableQL& q, const sp<UAClient>& client, const Namespaces* namespaces )ε->jobject;//strand-only:  the attribute reads are sync UA services.  namespaces:  read when namespaces{} is selected.
	};
}