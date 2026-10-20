#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/access/usings.h>
#include <jde/app/proto/App.FromServer.pb.h>
#include <jde/db/Key.h>
#include <jde/web/client/socket/ClientSocketAwait.h>
#include "../types/ServerCnnctn.h"

namespace Jde::Opc::Gateway{
	//CRD - Insert/Purge/Select from um_providers table
	struct ProviderAwait final : TAwaitEx<Access::ProviderPK, TAwait<jobject>::Task>{
		using base = TAwaitEx<Access::ProviderPK, TAwait<jobject>::Task>;
		ProviderAwait( string opcId, SRCE )ι:base{sl},_opcId{move(opcId)}{};//select
	private:
		α Execute()ι->TAwait<jobject>::Task;
		ServerCnnctnNK _opcId;
	};
	//A connection's provider row, inserted (or the existing one reused) or purged.
	struct ProviderMAwait : TAwaitEx<Access::ProviderPK, VoidTask>{
		using base = TAwaitEx<Access::ProviderPK, VoidTask>;
		ProviderMAwait( DB::Key opcKey, bool insert, SRCE )ι:base{sl},_insert{insert},_opcKey{move(opcKey)}{}
	private:
		α Execute()ι->VoidTask override;//one frame, every await through Any():  the slug, the existing row, then the insert or the purge.

		bool _insert;
		DB::Key _opcKey;
	};
}