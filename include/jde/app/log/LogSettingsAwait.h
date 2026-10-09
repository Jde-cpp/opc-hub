#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/ql/types/TableQL.h>
#include <jde/ql/types/MutationQL.h>

namespace Jde::App{
	struct IApp;
	//The loggers an instance's levels address, by the column naming them in logSetting and instanceTagLevel:  text, binary,
	//appServer.  `tags` is null for a logger this process does not run - the AppServer and the hub have no appServer log.
	α ForEachLogTarget( const function<void(sv column, LogTags* tags)>& f )ε->void;
	α SetLogTarget( sv column, LogTags*(*find)()ι )ι->void;//RemoteLog::Init plugs in `appServer`, which the shared library cannot name.
	α ValidateTagKeys( const jobject& args )ε->void;

	//logSetting{ text binary appServer tags }:  this process's live loggers, so there is nothing to wait for.
	α LogSettings( const QL::TableQL& ql )ι->jobject;
	α LogSettingsAwait( QL::TableQL&& ql, SRCE )ι->up<TAwait<jvalue>>;//the same, as the await IQL::LogSettingsQuery answers with.

	struct LogSettingsMAwait final : TAwait<jvalue>{
		using base = TAwait<jvalue>;
		LogSettingsMAwait( QL::MutationQL&& m, sp<App::IApp> appClient, UserPK executer, SRCE )ι:
			base{sl}, _mutation{move(m)}, _appClient{move(appClient)}, _executer{executer}{}
		α Suspend()ι->void override;
		Ω IsApplicable( const QL::MutationQL& m )ι->bool{ return m.CommandName.starts_with("updateLogSetting"); }
	private:
		α UpdateApp( QL::MutationQL&& m )ι->TAwait<jvalue>::Task;
		α Update( jobject&& args )ι->void;

		QL::MutationQL _mutation;
		sp<App::IApp> _appClient;
		UserPK _executer;
	};
}
