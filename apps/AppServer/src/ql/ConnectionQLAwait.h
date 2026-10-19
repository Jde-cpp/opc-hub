#pragma once
#include <jde/ql/types/TableQL.h>
#include <jde/ql/IQLSession.h>

namespace Jde::App::Server{
	struct ConnectionQLAwait final : TAwaitEx<jvalue, TAwait<jvalue>::Task>{
		using base = TAwaitEx<jvalue, TAwait<jvalue>::Task>;
		ConnectionQLAwait( QL::TableQL&& q, QL::Creds&& creds, SRCE )ι: base{sl}, _creds{move(creds)}, _query{move(q)}{}
		Ω IsApplicable( QL::TableQL& q )ι->bool;
	private:
		α Execute()ι->TAwait<jvalue>::Task override;
		QL::Creds _creds;
		QL::TableQL _query;
	};
}