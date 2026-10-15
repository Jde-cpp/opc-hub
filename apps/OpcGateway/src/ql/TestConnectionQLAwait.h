#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	struct UAClient; struct Namespaces;
	//testConnection( opc, url, certificateUri ){ applicationUri productUri applicationName applicationType gatewayServerUri
	//discoveryProfileUri discoveryUrls securityPolicyUri securityMode namespaces{ index uri } } - what serverDescription, the two
	//security attributes and namespaces answer for a saved connection, for settings that are not saved:  `opc` is the slug the
	//connection has or will have, which names its certificate and the session's credential;  `url` and `certificateUri` are the
	//values to try.  The gateway connects as it would to the saved row, reads the attributes and lets the client go - no client is
	//registered, and a failure is this query's error, never the slug's connectionStatus.  The connection page's Test Connection,
	//before the first save and to retry a saved connection without a reload.
	struct TestConnectionQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		TestConnectionQLAwait( QL::TableQL&& q, QL::Creds&& creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName=="testConnection"; }
	private:
		α Query()ι->TAwait<jvalue>::Task override;
		α Describe( const sp<UAClient>& client, const Namespaces& namespaces )ε->jobject;//strand-only:  the attribute reads are sync UA services.
	};
}