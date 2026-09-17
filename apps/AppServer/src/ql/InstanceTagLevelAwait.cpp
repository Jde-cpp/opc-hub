#include "InstanceTagLevelAwait.h"
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/app/usings.h>
#include "../LocalClient.h"
#include "../appStartup.h"
#include "../ServerSocketSession.h"
#include "../WebServer.h"
#include "jde/fwk/log/break.h"
#include <jde/fwk/co/AnyAwait.h>


#define let const auto
namespace Jde::App::Server{

	//The levels the instance runs with: its own `logSetting{ text binary appServer }` answer - tag->level per sink, with
	//`default` - fetched the way pushRuntime delivers a change: through the local ql for this process, over the socket for
	//a connected instance.  Null when it is not connected or does not answer: the rows are the record and are still
	//returned; what runs is best-effort, and a wedged instance must fail this column, not the query (install-issues #19).
	//An AnyAwait so InstanceTagLevelAwait::Execute, whose task type is the row query's, can co_await it (AnyAwait.h).
	struct RunningLevelsAwait final : AnyAwait<jvalue>{
		RunningLevelsAwait( ProgInstPK instanceId, QL::TableQL&& ql, UserPK executer, SRCE )ι:
			AnyAwait<jvalue>{sl}, _instanceId{instanceId}, _ql{move(ql)}, _executer{executer}{}
	protected:
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->TAwait<jvalue>::Task;
		ProgInstPK _instanceId;
		QL::TableQL _ql;
		UserPK _executer;
	};
	α RunningLevelsAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			if( _instanceId==AppClient()->InstancePK() )
				Resume( co_await *AppClient()->Query<jvalue>(_ql.ToString(), {}, true, _sl) );
			else if( auto session = FindInstance(_instanceId); session ){
				IWebsocketSession& base = *session;//ServerSocketSession's own QueryClient override hides the awaitable-returning overloads.
				Resume( co_await base.QueryClient(move(_ql), _executer, _sl) );
			}
			else
				Resume( jvalue{} );//not connected: nothing is running to report.
		}
		catch( runtime_error& e ){
			Exception{ move(e), ExceptionArgs{ELogLevel::Warning, ELogTags::Settings}, _sl };
			Resume( jvalue{} );
		}
	}

	//instanceTagLevel( id:42 ){ text binary appServer running } -> { "text": {"Debug":["sql",["socket","client","read"]], "Information":["default"]}, "running": {"text":{"default":"Trace","sql":"Debug",…}, "binary":{…}} }
	//Grouped by level, with the tags as the values: a multi-tag override has no name of its own - as a key ToString spells
	//it `["socket","client","read"]`, which is not a tag name and comes back as one - but as a value ToValue's array feeds
	//straight into ToLogTags( jvalue ).  Levels also repeat far more than tags do, so the object is smaller this way.
	//`running` is the instance's logSetting answer for the same sinks, verbatim (tag->level, so the other shape), or null -
	//the rows alone are only what has been saved for the instance, not what it logs at (install-issues #19).
	α InstanceTagLevelAwait::Execute()ι->TAwait<vector<DB::Row>>::Task{
		try{
			auto schema = AppSchema();
			let instanceId = _query.Id();
			let& table = schema->GetView( "instance_tag_levels" );
			DB::Sql sql{
				Ƒ( "select type, tag, level_id from {} where instance_id=?", table.DBName ),
				{ {instanceId} }
			};
			auto rows = co_await schema->DS()->SelectAsync( move(sql), _sl );

			auto text = _query.FindColumn("text") ? jobject{} : optional<jobject>{};
			auto binary = _query.FindColumn("binary") ? jobject{} : optional<jobject>{};
			auto appServer = _query.FindColumn("appServer") ? jobject{} : optional<jobject>{};
			auto add = []( optional<jobject>& group, ELogTags tag, ELogLevel level ){
				if( !group )
					return;
				auto& tags = (*group)[ ToString(level) ];
				if( !tags.is_array() )
					tags.emplace_array();
				//tag 0 is spelled "default" by the mutation, the logSetting query and the ui catalogue; ToValue spells it "none".
				tags.get_array().push_back( tag==ELogTags::None ? jvalue(jstring{"default"}) : ToValue(tag) );
			};
			for( auto&& row : rows ){
				let type = row.GetString( 0 );
				let tag = (ELogTags)row.Get<uint>( 1 );
				let level = (ELogLevel)row.Get<uint8>( 2 );
				if( type=="text" )
					add( text, tag, level );
				else if( type=="binary" )
					add( binary, tag, level );
				else if( type=="appServer" )
					add( appServer, tag, level );
			}
			jobject y;
			if( text )
				y["text"] = move(*text);
			if( binary )
				y["binary"] = move(*binary);
			if( appServer )
				y["appServer"] = move(*appServer);
			if( _query.FindColumn("running") ){
				QL::TableQL ql{ "logSetting", {}, ms<jobject>(), {}, true, _sl };
				for( let sink : {"text", "binary", "appServer"} ){//the sinks asked for here, from the instance.
					if( _query.FindColumn(sink) )
						ql.Columns.push_back( QL::ColumnQL{string{sink}, nullptr} );//not AddColumn: that resolves against a db table, and logSetting has none.
				}
				y["running"] = co_await RunningLevelsAwait{ (ProgInstPK)instanceId, move(ql), _executer, _sl };
			}
			Resume( y );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	//The rows are the record, but the instance is what has to change.  Hand it the same levels as an updateLogSetting
	//mutation so they take effect without a restart - persist:false, since we have already written them and a write-back
	//would re-enter this mutation and push again, forever.  Detached: a wedged instance must not delay the rows' result.
	Ω pushRuntime( ProgInstPK instanceId, jobject args, UserPK executer, SL sl )ι->TAwait<jvalue>::Task{
		try{
			args["persist"] = false;
			QL::TableQL ql{ "updateLogSetting", move(args), ms<jobject>(), {}, true, sl };
			if( instanceId==AppClient()->InstancePK() ) //our own levels: there is no socket back to this process.
				co_await *AppClient()->Query<jvalue>( ql.ToString(), {}, true, sl );
			else if( auto session = FindInstance(instanceId); session ){
				IWebsocketSession& base = *session;//ServerSocketSession's own QueryClient override hides the awaitable-returning overloads.
				co_await base.QueryClient( move(ql), executer, sl );
			}
			else
				TRACET( ELogTags::Settings, "[{}]Instance is not connected - the levels take effect when it next starts.", instanceId );
		}
		catch( runtime_error& e ){
			Exception{ move(e), ExceptionArgs{ELogLevel::Warning, ELogTags::Settings}, sl };//the rows are written either way; a failed push is not a failed mutation.
		}
	}

	//updateInstanceTagLevel( id:42, text:[{tags:["settings"],level:"Warning"},{tags:["crypto"],level:null}], binary:[...] )
	//A record per override rather than tag→level: a combined tag is only spellable as an array, and an array is no object
	//key - the same reason the query answers grouped by level.  `level:null` (or no level at all) deletes the row.
	α InstanceTagLevelMAwait::Update()ι->TAwait<uint32>::Task{
		try{
			auto vars = _mutation.ExtrapolateVariables();
			let instanceId = vars.at("id").to_number<ProgInstPK>();
			THROW_IF( !instanceId, "updateInstanceTagLevel needs an instance - 0 is what a session that has not registered reports, not one." );// the rows went in against instance 0, belonging to nothing, and the push then went looking for a session that matched.
			auto schema = AppSchema();
			let& table = schema->GetView( "instance_tag_levels" );
			auto sql = [&,instanceId]( str type, sv tag, jvalue level )->DB::Sql {
				DB::Value dbTag = tag=="default" ? DB::Value{0} : DB::Value{ underlying(ToLogTags(tag)) };
				if( level.is_null() )
					return { Ƒ("delete from {} where instance_id=? and type=? and tag=?", table.DBName), {{instanceId}, {type}, dbTag} };
				return {
					Ƒ("{}(?,?,?,?)", table.UpsertProcName()),
					{ {instanceId}, {type}, dbTag, {underlying(ToLogLevel(level.as_string()))} },
					true
				};
			};
			uint rowCount{};
			jobject runtime;//what pushRuntime hands the instance: the persisted tags only.
			constexpr array<sv,3> types{ "text", "binary", "appServer" };
			for( let type : types ){
				let values = vars.if_contains( type );
				if( !values )
					continue;
				let array = values->try_as_array();
				THROW_IF( !array, "{} takes a list of {{tags,level}} records, not {}.", type, Json::Kind(values->kind()) );
				for( auto&& [tag,level] : ToTagLevels(*array) ){//by name from here: the db column, the break check and the push all want the joined spelling.
					if( tag==Logging::BreakTag ){//this process's debugger trap, not a tag level - neither persisted nor pushed.
#ifndef NDEBUG
						Logging::SetBreakLevel( ToLogLevel(level.as_string()) );
#endif
						continue;
					}
					rowCount += co_await schema->DS()->Execute( sql(string{type}, tag, level) );
					auto& group = runtime[type];
					if( !group.is_object() )
						group.emplace_object();
					group.get_object()[tag] = level;
				}
			}
			if( !runtime.empty() )
				pushRuntime( instanceId, move(runtime), _executer, _sl );
			Resume( jvalue{rowCount} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}