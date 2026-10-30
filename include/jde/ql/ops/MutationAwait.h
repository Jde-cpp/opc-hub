#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/access/usings.h>
#include <jde/ql/IQLSession.h>
#include <jde/ql/types/MutationQL.h>

namespace Jde::QL{
	struct IQL;
	struct MutationAwait final: TAwait<jvalue>{
		MutationAwait( MutationQL mutation, Creds creds, sp<IQL> ql, SRCE )ι;
	private:
		α Suspend()ι->void override{ Execute(); }
		α Execute()ι->TAwait<jvalue>::Task;
		MutationQL _mutation;
		Creds _creds;
		sp<IQL> _ql;
	};
	//The stock create's own refusals, raised without writing:  the variables, the table, the Create right and the statement's (a
	//column with no default, one no create sets, a value that does not convert).  For a custom mutation with a side effect of its
	//own to take before the stock op, so a create the engine refuses never reaches it (the gateway's ServerCnnctnMAwait).
	α CheckCreate( const MutationQL& m, UserPK executer, SRCE )ε->void;
	//The right a stock op asks of a mutation's table:  Create, Update (an add or remove too), Delete (a restore too), Purge;  None
	//for start, stop and execute, which no stock op answers.  A custom mutation that acts ahead of the stock op asks it first.
	α StockRights( EMutationQL type )ι->Access::ERights;
}