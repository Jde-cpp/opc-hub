#include <jde/app/log/LogSettingsAwait.h>
#include <jde/app/IApp.h>
#include <jde/app/log/ProtoLog.h>
#include <jde/fwk/str.h>
#include <jde/fwk/log/SpdLog.h>	//no longer reachable through <jde/fwk.h>

#define let const auto

namespace Jde{
	Ω toTagName( const jvalue& tags )ι->string{//["socket","client","read"] | "sql" -> "socket.client.read" | "sql" - the TagSeparator spelling ToLogTags splits.
		string y;
		if( let s = tags.try_as_string(); s )
			y = string{ (sv)*s };
		else if( let parts = tags.try_as_array(); parts ){
			for( let& part : *parts ){
				if( let p = part.try_as_string(); p ){
					if( !y.empty() )
						y += TagSeparator;
					y += (sv)*p;
				}
			}
		}
		return y;
	}

	//A combined tag can only be spelled as an array, and an array is no object key - so the wire carries tags as *values*,
	//both in the instanceTagLevel answer (grouped under the level) and in the updateInstanceTagLevel argument (a record per
	//override).  SetLevels and the config file key on the tag, so these flatten.  Lossless: the array joins back with the
	//'.' ToLogTags splits on, and "default" passes through untouched.
	α App::ToTagLevels( const jobject& levelTags )ι->jobject{
		jobject y;
		for( let& [level, tags] : levelTags ){
			let array = tags.try_as_array();
			if( !array )
				continue;
			for( let& tag : *array ){
				if( let name = toTagName(tag); !name.empty() )
					y[name] = jstring{ level };
			}
		}
		return y;
	}
	α App::ToTagLevels( const jarray& tagLevels )ι->jobject{
		jobject y;
		for( let& entry : tagLevels ){
			let o = entry.try_as_object();
			if( !o )
				continue;
			let tags = o->if_contains( "tags" );
			let name = tags ? toTagName( *tags ) : string{};
			if( name.empty() )
				continue;
			let level = o->if_contains( "level" );
			y[name] = level ? *level : jvalue{};//no level at all reads as null - the same "remove this override" a null does.
		}
		return y;
	}
	α App::ToTagLevelArray( const jobject& tagLevels )ι->jarray{
		jarray y;
		for( let& [name, level] : tagLevels ){
			jarray tags;
			for( let& part : Str::Split(name, TagSeparator) )
				tags.push_back( jstring{part} );
			jobject entry;
			entry["tags"] = move( tags );
			entry["level"] = level;
			y.push_back( move(entry) );
		}
		return y;
	}

	struct LogTarget{ sv Column; std::atomic<LogLevels*(*)()ι> Find; };
	std::array<LogTarget,3> _targets{{
		{ "text", +[]()ι->LogLevels*{ return Logging::FindLogger<Logging::SpdLog>(); } },
		{ "binary", +[]()ι->LogLevels*{ return Logging::FindLogger<App::ProtoLog>(); } },
		{ "appServer", nullptr }
	}};
	α toJson( const LogLevels& logger )ι->jobject{
		let& levels = logger.Effective();
		jobject y;
		y["default"] = ToString( levels.DefaultLevel() );
		for( let& [tags, level] : levels.Levels )
			y[ToName( tags )] = Jde::ToString( level );
		return y;
	}
	//args[column] is tag->level, `default` and the break tag included; null clears an override.
	α updateRuntime( const jobject& args, sv column, LogLevels& logger )ε->void{
		let loggerArgs = args.if_contains( column );
		if( !loggerArgs || !loggerArgs->is_object() )
			return;
		logger.Edit( [&]( TagLevels& overrides ){//one publish for the whole batch.
			for( auto&& [key, value] : loggerArgs->as_object() ){
				if( key==Logging::BreakTag )//process-local trap level, not a tag - and ToLogTags would fold it into None, silently overwriting the default level.
					continue;
				if( key=="default" ){
					if( value.is_string() )
						overrides.Default = ToLogLevel( value.as_string() );
					else
						overrides.Default.reset();//null deletes the row: back to the settings' default, which the logger remembers (install-issues #21).
					continue;
				}
				let tags = ToLogTags( string{key} );
				if( value.is_string() )
					overrides.Levels.insert_or_assign( tags, ToLogLevel(value.as_string()) );
				else
					overrides.Levels.erase( tags );//null: the override was deleted, fall back to the level the settings gave the tag, or the default.
			}
		} );
	}
	α App::ForEachLogTarget( const function<void(sv column, LogLevels* tags)>& f )ε->void{
		for( let& target : _targets ){
			let find = target.Find.load();
			f( target.Column, find ? find() : nullptr );
		}
	}
	α App::SetLogTarget( sv column, LogLevels*(*find)()ι )ι->void{
		let target = std::ranges::find( _targets, column, &LogTarget::Column );
		ASSERT( target!=_targets.end() );
		if( target!=_targets.end() )
			target->Find.store( find );
	}
namespace App{
	α IApp::LoadLogSettings( SL sl )ι->void{
		try{
			string columns;
			ForEachLogTarget( [&](sv column, LogLevels*){ columns += Ƒ(" {}", column); } );
			let settings = QuerySync( Ƒ("instanceTagLevel(id:$id){{{} }}", columns), {{"id",InstancePK()}}, true, sl );
			//instanceTagLevel groups the tags under their level; SetLevels keys on the tag.
			ForEachLogTarget( [&](sv column, LogLevels* tags){
				if( let levels = settings.if_contains(column); tags && levels && levels->is_object() )
					tags->SetLevels( ToTagLevels(levels->get_object()) );
			});
			Logging::Log( ELogLevel::Trace, ELogTags::Settings, sl, "Loaded log settings." );
		}
		catch( Exception& e ){
			e.SetLevel( ELogLevel::Critical );
		}
		catch( runtime_error& e ){
			Exception{ move(e), ExceptionArgs{ELogLevel::Critical}, sl };
		}
	}
}
	//L9: an unrecognised name was not rejected, it was dropped - ToLogTags warns and returns whatever it did recognise, which for a
	//name it recognises nothing of is ELogTags::None.  SetLevel then wrote a None row into _configuredTags that MinLevel can never
	//match, ToJson reported it back to the ui as a "none" tag, and the typo went on to updateInstanceTagLevel to be persisted as tag
	//0 - the row "default" uses.  Component-wise, because ToLogTags splits on TagSeparator and ORs the parts it knows: "socket.bogus"
	//resolved to a plain socket override, wider than what was asked for, with nothing to say so.
	α App::ValidateTagKeys( const jobject& args )ε->void{
		let catalogue = Logging::Tags( true );//the catalogue the logSetting query answers with - a name the ui was offered must not then be refused.
		ForEachLogTarget( [&](sv column, LogLevels*){
			let group = args.if_contains( column );
			if( !group || !group->is_object() )
				return;
			for( let& [key, _] : group->get_object() ){
				if( key==Logging::BreakTag || key=="default" )//neither is an ELogTags; UpdateRuntime handles both ahead of ToLogTags.
					continue;
				let name = string{ key };
				let parts = Str::Split( sv{name}, TagSeparator );
				THROW_IF( parts.empty(), "'{}' is not a log tag.", name );
				for( let& part : parts ){
					if( catalogue.contains(string{part}) )
						continue;
					THROW( "'{}' is not a log tag{}.", part, part==name ? string{} : Ƒ(" - in '{}'", name) );
				}
			}
		});
	}
namespace App{
	α LogSettings( const QL::TableQL& ql )ι->jobject{
		jobject y;
		ForEachLogTarget( [&](sv column, LogLevels* tags){
			if( ql.FindColumn(column) )
				y[column] = tags ? toJson( *tags ) : jobject{};//asked of a logger this process does not run: an empty object, no `default`.
		});
		if( auto tags = ql.FindColumn("tags"); tags ){
			jobject jtags;
			for( let& [tag,value] : Logging::Tags(true) )
				jtags[tag] = value;
			y["tags"] = move( jtags );
		}
		return y;
	}
	α LogSettingsAwait( QL::TableQL&& ql, SL sl )ι->up<TAwait<jvalue>>{
		return mu<CompletedAwait<jvalue>>( [ql=ms<QL::TableQL>(move(ql))]{ return jvalue{ LogSettings(*ql) }; }, sl );//sp: std::function wants a copyable body.
	}

	//updateLogSettings( text(default: Information,settings: Warning), binary(...) )
	α LogSettingsMAwait::Suspend()ι->void{
			Update( _mutation.ExtrapolateVariables() );
	}
	α LogSettingsMAwait::Update( jobject&& args )ι->void{
		try{
			ValidateTagKeys( args );
			ForEachLogTarget( [&](sv column, LogLevels* tags){
				if( tags )
					updateRuntime( args, column, *tags );
			});
			//persist:false - the app server pushed levels it has already written to instance_tag_levels.  Writing them back
			//would re-enter updateInstanceTagLevel there, which would push again, and neither side would ever settle.
			if( let persist = args.if_contains("persist"); persist && persist->is_bool() && !persist->get_bool() ){
				Resume( jvalue{true} );
				return;
			}
			THROW_IF( !_appClient->InstancePK(), "No App InstancePK available for LogSettings update." );
			auto m = _mutation;
			m.Args.erase( "persist" );
			//updateLogSetting takes tag->level; updateInstanceTagLevel takes a record per override, since a combined tag is
			//no object key there.  Convert on the way out or the app server sees an object where it wants a list.
			ForEachLogTarget( [&](sv column, LogLevels*){
				if( auto group = m.Args.if_contains(column); group && group->is_object() )
					*group = ToTagLevelArray( group->get_object() );
			});
			m.Args["id"] = _appClient->InstancePK();
			m.JsonTableName = "instanceTagLevels";
			m.CommandName = "updateInstanceTagLevel";
			UpdateApp( move(m) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α LogSettingsMAwait::UpdateApp( QL::MutationQL&& m )ι->TAwait<jvalue>::Task{
		try{
			Resume( co_await *_appClient->Query<jvalue>(m.ToString(), m.Variables ? *m.Variables : jobject{}) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}}