#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/ql/types/TableQL.h>
#include <jde/app/log/ArchiveQuery.h>

namespace Jde::App{
	//logs{...}:  today's log - ProtoLog's buffer and daily file - and the day archives, read until the page is full.
	struct LogQLAwait final : TAwaitEx<jvalue,TAwait<jvalue>::Task>{
		using base = TAwaitEx<jvalue,TAwait<jvalue>::Task>;
		LogQLAwait( QL::TableQL&& ql, SRCE )ι:base{sl}, _ql{move(ql)}{}
		α Execute()ι->TAwait<jvalue>::Task;
	private:
		QL::TableQL _ql;
	};
}
