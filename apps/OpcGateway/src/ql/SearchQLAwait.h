#pragma once
#include <jde/ql/IQLAwaitExe.h>
#include "../NodeIndex.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//search( opc?, text, limit?, refresh? ){ connection{slug name} id path name browse{ns name} nodeClass depth } - node names
	//matched (case-insensitive substring on display and browse name) against the per-connection NodeIndex.
	//Never connects:  an explicit opc uses the session's live client for that connection (none ⇒ []);  no opc fans out over
	//every live client whose credential is the one ConnectAwait would hand this session (SessionCredential) - clients other
	//identities opened are invisible by design, an address space differs per user;  anonymous ones are shared.
	struct SearchQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		SearchQLAwait( QL::TableQL&& q, QL::Creds&& creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		static constexpr sv Command{ "search" };
	private:
		α Query()ι->TAwait<jvalue>::Task override;
		α Row( const UAClient& client, const NodeIndex::Entry& e )ι->jobject;
	};
}
