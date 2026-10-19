#include "InstanceTagLevelAwait.h"
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/access/Authorize.h>
#include <jde/app/usings.h>
#include "../LocalClient.h"
#include "../appStartup.h"
#include "jde/fwk/log/break.h"
#include "QueryInstanceAwait.h"


#define let const auto
namespace Jde::App::Server{
	constexpr array<sv,3> _sinks{ "text", "binary", "appServer" };//instance_tag_levels.type - and the query's columns and the mutation's arguments.
	//tag 0 is spelled "default" by the mutation, the logSetting query and the ui catalogue; ToValue spells it "none".
	Ω tagName( ELogTags tag )ι->jvalue{ return tag==ELogTags::None ? jvalue(jstring{"default"}) : ToValue(tag); }
	Ω tagValue( sv tag )ι->DB::Value{ return tag=="default" ? DB::Value{0} : DB::Value{ underlying(ToLogTags(tag)) }; }

	//The instance's logs resource - Read to see its levels, Update to change them.  Open to anyone while it is unenforced;
	//an enforced one answers an anonymous caller 401.
	Ω testLogs( ProgInstPK instanceId, Access::ERights rights, UserPK executer, SL sl )ε->void{
		Authorizer()->Test( InstanceLogSchema(instanceId), "logs", rights, executer, sl );
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
			testLogs( (ProgInstPK)instanceId, Access::ERights::Read, _executer, _sl );
			let& table = schema->GetView( "instance_tag_levels" );
			DB::Sql sql{
				Ƒ( "select type, tag, level_id from {} where instance_id=?", table.DBName ),
				{ {instanceId} }
			};
			auto rows = co_await schema->DS()->SelectAsync( move(sql), _sl );

			flat_map<sv,jobject> groups;//the sinks asked for
			for( let sink : _sinks ){
				if( _query.FindColumn(sink) )
					groups.emplace( sink, jobject{} );
			}
			for( auto&& row : rows ){
				let group = groups.find( row.GetString(0) );
				if( group==groups.end() )
					continue;
				auto& tags = group->second[ ToString((ELogLevel)row.Get<uint8>(2)) ];
				if( !tags.is_array() )
					tags.emplace_array();
				tags.get_array().push_back( tagName((ELogTags)row.Get<uint>(1)) );
			}
			jobject y;
			for( let sink : _sinks ){
				if( let group = groups.find(sink); group!=groups.end() )
					y[sink] = move( group->second );
			}
			if( _query.FindColumn("running") ){
				QL::TableQL ql{ "logSetting", {}, ms<jobject>(), {}, true, _sl };
				for( let sink : _sinks ){//the sinks asked for here, from the instance.
					if( _query.FindColumn(sink) )
						ql.Columns.push_back( QL::ColumnQL{string{sink}, nullptr} );//not AddColumn: that resolves against a db table, and logSetting has none.
				}
				//asked the way pushRuntime delivers a change.  Null when it is not connected or does not answer:  the rows are the
				//record and are still returned;  what runs is best-effort, and a wedged instance must fail this column, not the query
				//(install-issues #19).
				try{
					y["running"] = (co_await QueryInstanceAwait{ (ProgInstPK)instanceId, move(ql), _executer, _sl }).value_or( jvalue{} );
				}
				catch( runtime_error& e ){
					Exception{ move(e), ExceptionArgs{ELogLevel::Warning, ELogTags::Settings}, _sl };
					y["running"] = nullptr;
				}
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
			if( !co_await QueryInstanceAwait{instanceId, move(ql), executer, sl} )
				TRACET( ELogTags::Settings, "[{}]Instance is not connected - the levels take effect when it next starts.", instanceId );
		}
		catch( runtime_error& e ){
			Exception{ move(e), ExceptionArgs{ELogLevel::Warning, ELogTags::Settings}, sl };//the rows are written either way; a failed push is not a failed mutation.
		}
	}

	//updateInstanceTagLevel( id:42, text:[{tags:["settings"],level:"Warning"},{tags:["crypto"],level:null}], binary:[...] )
	//A record per override rather than tag→level: a combined tag is only spellable as an array, and an array is no object
	//key - the same reason the query answers grouped by level.  `level:null` (or no level at all) deletes the row.
	α InstanceTagLevelMAwait::Execute()ι->TAwait<uint32>::Task{
		try{
			auto vars = _mutation.ExtrapolateVariables();
			let instanceId = vars.at("id").to_number<ProgInstPK>();
			THROW_IF( !instanceId, "updateInstanceTagLevel needs an instance - 0 is what a session that has not registered reports, not one." );// the rows went in against instance 0, belonging to nothing, and the push then went looking for a session that matched.
			testLogs( instanceId, Access::ERights::Update, _executer, _sl );
			auto schema = AppSchema();
			let& table = schema->GetView( "instance_tag_levels" );
			auto sql = [&,instanceId]( str type, sv tag, jvalue level )->DB::Sql {
				if( level.is_null() )
					return { Ƒ("delete from {} where instance_id=? and type=? and tag=?", table.DBName), {{instanceId}, {type}, tagValue(tag)} };
				return {
					Ƒ("{}(?,?,?,?)", table.UpsertProcName()),
					{ {instanceId}, {type}, tagValue(tag), {underlying(ToLogLevel(level.as_string()))} },
					true
				};
			};
			uint rowCount{};
			jobject runtime;//what pushRuntime hands the instance: the persisted tags only.
			for( let type : _sinks ){
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