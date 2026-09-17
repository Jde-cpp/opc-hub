#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/ql/types/TableQL.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/app/log/ArchiveFile.h>

namespace Jde::App{
	struct IApp;
	α ValidateTagKeys( const jobject& args )ε->void;

	struct LogSettingsAwait : TAwait<jvalue>{
		using base = TAwait<jvalue>;
		LogSettingsAwait( QL::TableQL&& ql, SRCE )ι:base{sl}, _ql{move(ql)}{}
		α await_ready()ι->bool override;
		α Suspend()ι->void override{ ASSERT(false); }
		α await_resume()ε->jvalue override;
	protected:
		Ŧ ToJson()ι->jobject;
		α ToJson( const Logging::ILogger& logger )ι->jobject;
		α CalcResult()ι->jobject;
		QL::TableQL _ql;
		jobject _result;
	};

	struct LogSettingsMAwait : TAwait<jvalue>{
		using base = TAwait<jvalue>;
		LogSettingsMAwait( QL::MutationQL&& m, sp<App::IApp> appClient, UserPK executer, SRCE )ι:
			base{sl}, _mutation{move(m)}, _appClient{move(appClient)}, _executer{executer}{}
		α Suspend()ι->void override;
		Ω IsApplicable( const QL::MutationQL& m )ι->bool{ return m.CommandName.starts_with("updateLogSetting"); }
	protected:
		α UpdateApp( QL::MutationQL&& m )ι->TAwait<jvalue>::Task;
		α Update( jobject&& args )ι->void;
		Ŧ UpdateRuntime( const jobject& args, str type )ε;

		QL::MutationQL _mutation;
		sp<App::IApp> _appClient;
		UserPK _executer;
	};

#define let const auto
	Ŧ LogSettingsAwait::ToJson()ι->jobject{
		auto logger = Logging::FindLogger<T>();
		return logger ? ToJson( *logger ) : jobject{};
	}

	Ŧ LogSettingsMAwait::UpdateRuntime( const jobject& args, str type )ε{
		ValidateTagKeys( args );
		auto logger = Logging::FindLogger<T>();
		if( !logger )
			return;
		let loggerArgs = args.if_contains( type );
		if( !loggerArgs || !loggerArgs->is_object() )
			return;
		bool defaultChanged{};
		for( auto&& [key, value] : loggerArgs->as_object() ){
			if( key==Logging::BreakTag )//process-local trap level, not a tag - and ToLogTags would fold it into None, silently overwriting the default level.
				continue;
			if( key=="default" ){
				if( value.is_string() )
					logger->SetDefaultLevel( ToLogLevel(value.as_string()) );
				else
					logger->ClearDefaultLevel();//null deletes the row: back to the settings' default, which the logger remembers (install-issues #21).
				defaultChanged = true;
				continue;
			}
			let tags = ToLogTags( string{key} );
			if( value.is_string() )
				logger->SetLevel( tags, ToLogLevel(value.as_string()) );
			else
				logger->ClearLevel( tags );//null: the override was deleted, fall back to the level the settings gave the tag, or the default.
		}
		if( defaultChanged )//SetLevel/ClearLevel refresh the cumulative filter themselves, SetDefaultLevel does not.
			Logging::UpdateCumulative( Logging::Loggers() );
	};
}
#undef let