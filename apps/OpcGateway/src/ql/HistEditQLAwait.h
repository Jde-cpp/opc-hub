#pragma once
#include <jde/ql/types/MutationQL.h>
#include "HistQL.h"
#include "../async/HistoryUpdateAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//histInsert, histReplace, histUpdate, histDelete and histDeleteAtTime with `opc`:  a server's own history, edited for
	//the caller over the caller's session (spec *Pass-through*), one HistoryUpdate entry per node.  An UpdateData's values
	//take the type ValueTypesAwait reads for their node, as updateVariable's do.  The server keeps its own audit trail, so
	//the gateway records nothing:  each value's result is the server's operation result, or, for an entry the server
	//refuses whole, that status.
	struct HistEditQLAwait final : TAwaitEx<jvalue,TAwait<HistoryUpdateResponse>::Task>{
		using base = TAwaitEx<jvalue,TAwait<HistoryUpdateResponse>::Task>;
		HistEditQLAwait( QL::MutationQL&& mutation, HistQL::EEdit edit, sp<UAClient> client, SRCE )ι:base{ sl }, _client{ move(client) }, _edit{ edit }, _mutation{ move(mutation) }{}
		α Execute()ι->TAwait<HistoryUpdateResponse>::Task override;
	private:
		sp<UAClient> _client;
		HistQL::EEdit _edit;
		QL::MutationQL _mutation;
	};
}