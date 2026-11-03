#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	//trustServer( opc, thumbprint ) - the Connection tab's Trust (docs/clients/spec.md *Trust*):  moves the <thumbprint>.der our
	//verifier wrote under /gateway/rejectedCertDir (ServerTrust) into its parent, the ssl/servers trusted dir, which must be among
	///gateway/trustedCertDirs for the move to mean anything - the mutation is refused with that message when it is not - and
	//reconnects the slug through UAClient::ConnectionEdited, since the trust list is loaded per connect (ServerTrust.h).  `opc` is
	//the slug, which names the connection to reconnect;  `thumbprint` the SHA-1 rejectedCertificate{} answered, the name of the
	//file.  A system mutation under Administer on gateway/serverConnections - trusting a server is an admin's decision from the page
	//that shows the certificate (spec *Open questions* #8) - a mutation, not a query, as testConnection is:  it changes the trust
	//list and must not run over a GET.  Answers `true`.
	struct TrustServerQLAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		TrustServerQLAwait( QL::MutationQL&& m, QL::Creds&& creds, SRCE )ι:base{ move(m), move(creds), sl }{}
		Ω Test( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{ return m.CommandName==Command ? mu<TrustServerQLAwait>( move(m), move(creds), sl ) : nullptr; }
		static constexpr sv Command{ "trustServer" };//a system mutation (QL::SetSystemMutations), taken whole:  trust is no verb the engine knows.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}