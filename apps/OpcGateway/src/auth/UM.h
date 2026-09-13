#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/access/usings.h>
#include <jde/web/client/exports.h>
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
	struct ProviderMAwait : TAwait<Access::ProviderPK>{
		ProviderMAwait( DB::Key opcKey, bool insert, SRCE )ι:TAwait<Access::ProviderPK>{sl},_insert{insert},_opcKey{move(opcKey)}{}
		α Suspend()ι->void override;
	private:
		α Execute( ServerCnnctnPK opcPK )ι->TAwait<vector<ServerCnnctn>>::Task;
		α Check( string slug )ι->ProviderAwait::Task;//reuses an existing row, else Insert.  By value: Execute's frame is gone by the time this resumes.
		α Insert( str slug )ι->TAwait<jobject>::Task;
		α Purge( str slug )ι->ProviderAwait::Task;
		α Purge( Access::ProviderPK pk )ι->TAwait<jvalue>::Task;

		bool _insert;
		DB::Key _opcKey;
	};
}