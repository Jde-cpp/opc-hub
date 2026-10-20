#pragma once
#include "IQLSession.h"
#include "types/MutationQL.h"
#include "types/TableQL.h"

namespace Jde::QL{
 struct IQLAwaitExe : TAwait<jvalue>, noncopyable{
		using base = TAwait<jvalue>;
		IQLAwaitExe( QL::Creds&& creds, SRCE )ι:base{sl}, _creds{move(creds)}{}
		β Suspend()ι->void override{ Query(); }
		α Session()ι->sp<Web::Server::SessionInfo>{ auto p = _creds.Session(); return p ? dynamic_pointer_cast<Web::Server::SessionInfo>(p) : nullptr; }
		α RequireSession( SRCE )ε->sp<Web::Server::SessionInfo>{ auto p = Session(); THROW_IFSL( !p, "No Session for query" ); return p; }
		β Input()ι->const QL::Input& = 0;
		β Query()ι->TAwait<jvalue>::Task = 0;
	protected:
		α UserPK()ι->Jde::UserPK{ return _creds.UserPK(); }

		QL::Creds _creds;
	};

 struct IQLTableAwaitExe : IQLAwaitExe{
		using base = IQLAwaitExe;
		IQLTableAwaitExe( QL::TableQL&& query, QL::Creds&& creds, SRCE )ι:
			base{move(creds), sl}, _query{move(query)}
		{}
	protected:
		α Input()ι->const QL::Input& override{ return _query; }
		QL::TableQL _query;
	};

 struct IQLTableMutationExe : IQLAwaitExe{
		using base = IQLAwaitExe;
		IQLTableMutationExe( QL::MutationQL&& query, QL::Creds&& creds, SRCE )ι:base{move(creds), sl}, _query{move(query)}{}
	protected:
		α Input()ι->const QL::Input& override{ return _query; }
		QL::MutationQL _query;
	};
}