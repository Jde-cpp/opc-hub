#include <jde/app/log/ArchiveQuery.h>
#include <boost/uuid/uuid_io.hpp>
#include <jde/fwk/chrono.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/app/proto/LogProto.h>

#define let const auto

namespace Jde::App{
	using App::Log::Proto::LogEntryFile;
	using Protobuf::ToGuid;

	ArchiveQuery::ArchiveQuery( const QL::Filter& q, vector<App::Log::Proto::FileEntry>&& entries )ε{
		Append( q, move(entries) );
	}

	α ArchiveQuery::EntrySize()Ι->uint{
		uint size{};
		for( auto& [_,entries] : Entries )
			size += ( uint )entries.size();
		return size;
	}

	α ArchiveQuery::IsComplete( const QL::Input& input )Ι->bool{
		let limit = input.Limit();
		if( !limit )
			return false;
		let& orderBy = input.OrderByJson();
		if( orderBy.empty() || orderBy.begin()->first!="time" )
			return false;
		return EntrySize()>=limit+input.Offset();
	}
	α StringTable::Find( EStringKind kind, sv idBytes )Ι->str{
		let& map = _maps[(uint8)kind];
		let it = map.find( ToGuid(idBytes) );
		return it==map.end() ? Str::Empty() : it->second;
	}
	α ArchiveStrings( DayArchive& af, EStringKind kind )ι->google::protobuf::RepeatedPtrField<App::Log::Proto::String>&{
		switch( kind ){
			using enum EStringKind;
			case Template: return *af.mutable_templates();
			case File: return *af.mutable_files();
			case Function: return *af.mutable_functions();
			case Arg: return *af.mutable_args();
		}
		return *af.mutable_args();//unreachable:  the switch names every kind.
	}

	α ArchiveQuery::Message( const LogEntryFile& entry )Ι->string{
		vector<string> args;
		for( let& argId : entry.args() )
			args.emplace_back( Strings.Find(EStringKind::Arg, argId) );
		let fmt = Strings.Find( EStringKind::Template, entry.template_id() );
		return Str::TryFormat( fmt, args );
	}

	α ArchiveQuery::Test( const QL::Filter& filter, TimePoint time, const LogEntryFile& entry )Ι->bool{
		if( !filter.Test( "time", time )
			|| !filter.TestF<string>( "text", [&](){return Strings.Find(EStringKind::Template, entry.template_id());} )
			|| !filter.Test( "level", (uint8)entry.level() )
			|| !filter.TestOr( "tags", entry.tags() )
			|| !filter.Test( "line", entry.line() )
			|| !filter.Test( "appId", entry.app_pk() )//L3: filterable as well as readable - one app's lines out of the AppServer's merged history.
			|| !filter.Test( "appInstanceId", entry.app_instance_pk() )
			|| !filter.TestF<uuid>( "templateId", [&](){return ToGuid(entry.template_id());} )
			|| !filter.TestF<string>( "message", [&](){return Message(entry);} ) )
			return false;
		if( !filter.ColumnFilters.contains("args") )
			return true;
		//Any argument, not every one.  ANDing meant `args:"timeout"` rejected every entry that also had an unrelated
		//argument, and an entry with *no* arguments passed outright.  any_of fails an empty list; TestF still ANDs the
		//operators on the column, so `args:{gt:"a", lt:"z"}` asks for one argument satisfying both.
		return std::any_of( entry.args().begin(), entry.args().end(), [&](let& argId){ return filter.TestF<string>("args", [&](){return Strings.Find(EStringKind::Arg, argId);}); } );
	}
	α ArchiveQuery::Test( const QL::TableQL& q, TimePoint time, const LogEntryFile& entry )Ε->bool{
		let& filter = q.Filter();
		bool valid = filter.Empty() || Test( filter, time, entry );

		if( valid ){
			auto testFileFunction = [&]( const auto& filter, EStringKind kind, sv id )ι->bool {
				if( auto valid = filter.template TestF<string>("id", [&](){return to_string(ToGuid(id));}); !valid )
					return false;
				if( auto valid = filter.template TestF<string>("name", [&](){return Strings.Find(kind, id);}); !valid )
					return false;
				return true;
			};
			for( auto& sub : q.Tables ){
				if( sub.JsonName=="user" ){
					if( valid = sub.Filter().Test("id", entry.user_pk()); !valid )
						break;
				}
				else if( valid = sub.JsonName=="file" ? testFileFunction(sub.Filter(), EStringKind::File, entry.file_id()) : true; !valid )
					break;
				else if( valid = sub.JsonName=="function" ? testFileFunction(sub.Filter(), EStringKind::Function, entry.function_id()) : true; !valid )
					break;
			}
		}
		return valid;
	}

	α ArchiveQuery::Append( const QL::TableQL& q, DayArchive&& af )ε->void{
		for( let kind : StringKinds ){
			for( auto& s : ArchiveStrings(af, kind) ){
				let id = ToGuid( s.id() );
				ASSERT_DESC( s.value().size() || id==EmptyStringMd5, "String with empty value must have empty md5." );
				Strings.Add( kind, id, move(*s.mutable_value()) );
			}
		}
		for( int i=0; i<af.entries_size(); ++i ){
			auto entry = af.mutable_entries( i );
			let time = Protobuf::ToTimePoint( entry->time() );
			if( Test(q, time, *entry) )
				Entries[time].emplace_back( move(*entry) );
		}
		for( int i=0; i<af.externalentries_size(); ++i ){
			auto entry = LogProto::ToEntry( move(*af.mutable_externalentries(i)) );
			let time = Protobuf::ToTimePoint( entry.time() );
			if( Test(q, time, entry) )
				Entries[time].emplace_back( move(entry) );
		}
	}
	using App::Log::Proto::FileEntry;
	α ArchiveQuery::Append( const QL::Filter& filter, vector<FileEntry>&& entries )ε->void{
		vector<LogEntryFile> logEntries;
		flat_map<uuid, string> strings;
		for( auto& fe : entries ){
			if( fe.value_case()==FileEntry::ValueCase::kEntry )
				logEntries.emplace_back( move(*fe.mutable_entry()) );
			else if( fe.value_case()==FileEntry::ValueCase::kExternalEntry )
				logEntries.emplace_back( LogProto::ToEntry(move(*fe.mutable_external_entry())) );
			else if( fe.value_case()==FileEntry::ValueCase::kStr ){
				let id = ToGuid( fe.str().id() );
				auto& value = *fe.mutable_str()->mutable_value();
				ASSERT_DESC( value.size() || id==EmptyStringMd5, "String with empty value must have empty md5." );
				strings[id] = move( value );
			}
		}
		//Collect in a pass of its own, before any Test: the filter resolves "text", "message" and "args" through these very
		//maps (Test/Message below), so populating them per surviving entry meant the first entry was compared against an
		//empty map - and every one after it, since none ever survived.  Any text/message/args filter silently dropped the
		//whole daily file.  The TableQL overload above has always collected first; this is the same order.
		//No extra memory: each value is moved out of `strings`, which we already hold in full, rather than copied.
		for( let& entry : logEntries ){
			ForEachStringId( entry, [&]( EStringKind kind, const string& idBytes ){
				let id = ToGuid( idBytes );
				Strings.Add( kind, id, move(strings[id]) );
			});
		}
		for( auto& entry : logEntries ){
			let time = Protobuf::ToTimePoint( entry.time() );
			if( Test(filter, time, entry) )
				Entries[time].emplace_back( move(entry) );
		}
	}

	α ArchiveQuery::Sort( const vector<std::pair<string,bool>>& orderBy )Ι->vector<App::Log::Proto::LogEntryFile>{
		vector<App::Log::Proto::LogEntryFile> y;
		for( let& [ts,entries] : Entries )
			y.insert( y.end(), entries.begin(), entries.end() );

		if( orderBy.empty() || (orderBy.size()==1 && orderBy[0].first=="time" && orderBy[0].second) )
			return y;

		std::stable_sort( y.begin(), y.end(), [&](let& a, let& b){
			optional<bool> lessThan;
			for( let& [field,asc] : orderBy ){
				if( field=="time" ){
					let aTime = Protobuf::ToTimePoint( a.time() );
					let bTime = Protobuf::ToTimePoint( b.time() );
					if( aTime != bTime )
						lessThan = aTime<bTime;
				}
				else if( field=="file" ){
					let& aFile = Strings.Find( EStringKind::File, a.file_id() );
					let& bFile = Strings.Find( EStringKind::File, b.file_id() );
					lessThan = aFile==bFile ? nullopt : optional<bool>{ aFile<bFile };
				}
				else if( field=="function" ){
					let& aFunction = Strings.Find( EStringKind::Function, a.function_id() );
					let& bFunction = Strings.Find( EStringKind::Function, b.function_id() );
					lessThan = aFunction==bFunction ? nullopt : optional<bool>{ aFunction<bFunction };
				}
				else if( field=="level" )
					lessThan = a.level()==b.level() ? nullopt : optional<bool>{ a.level()<b.level() };
				else if( field=="line" )
					lessThan = a.line()==b.line() ? nullopt : optional<bool>{ a.line()<b.line() };
				else if( field=="message" ){
					let aMsg = Message( a );
					let bMsg = Message( b );
					lessThan = aMsg==bMsg ? nullopt : optional<bool>{ aMsg<bMsg };
				}
				else if( field=="tags" )
					lessThan = a.tags()==b.tags() ? nullopt : optional<bool>{ a.tags()<b.tags() };
				else if( field=="user" )
					lessThan = a.user_pk()==b.user_pk() ? nullopt : optional<bool>{ a.user_pk()<b.user_pk() };
				if( lessThan )
					return asc ? *lessThan : !*lessThan;
			}
			return false;
		} );
		return y;
	}
	α ArchiveQuery::ToEntry( const QL::TableQL& table, const App::Log::Proto::LogEntryFile& entry, optional<flat_map<uuid,string>>& strings )Ι->jobject{
		//An id column's value, recording the string it names when the query also asked for strings.
		auto stringId = [&]( EStringKind kind, sv idBytes )->string{
			let id = ToGuid( idBytes );
			if( strings )
				( *strings )[id] = Strings.Find( kind, idBytes );
			return to_string( id );
		};
		jobject o;
		for( let& col : table.Columns ){
			let& name = col.JsonName;
			if( name=="templateId" )
				o[name] = stringId( EStringKind::Template, entry.template_id() );
			else if( name=="argIds" ){
				jarray args;
				for( auto&& arg : entry.args() )
					args.push_back( {stringId(EStringKind::Arg, arg)} );
				o[name] = move( args );
			}
			else if( name=="level" )
				o[name] = Jde::ToString( (ELogLevel)entry.level() );
			else if( name=="tags" ){
				auto tags = ToValue( (ELogTags)entry.tags() );//a lone tag comes back bare, but the grid column is string[] (web LogEntry.ts) - always a list.
				o[name] = tags.is_array() ? move(tags) : jvalue( jarray{move(tags)} );
			}
			else if( name=="line" )
				o[name] = entry.line();
			else if( name=="time" )
				o[name] = ToIsoString( Protobuf::ToTimePoint(entry.time()) );//ToIsoString already ends in 'Z'.
			else if( name=="userId" )
				o[name] = entry.user_pk();
			//L3: 0 for an entry the logging process wrote itself - only a forwarded one carries another app's pks.
			else if( name=="appId" )
				o[name] = entry.app_pk();
			else if( name=="appInstanceId" )
				o[name] = entry.app_instance_pk();
			else if( name=="fileId" )
				o[name] = stringId( EStringKind::File, entry.file_id() );
			else if( name=="functionId" )
				o[name] = stringId( EStringKind::Function, entry.function_id() );
		}
		return o;
	}
	//logs( limit: $limit, offset: $offset, orderBy: $orderBy ){ entries{templateId argIds level tags line time userId appId appInstanceId fileId functionId} strings{id value} }
	α ArchiveQuery::ToJson( const QL::TableQL& ql )Ι->jobject{
		let entries = Sort( ql.OrderByJson() );
		jobject o;
		jarray jentries;
		auto strings = ql.FindTable( "strings" ) ? flat_map<uuid,string>{} : optional<flat_map<uuid,string>>{};
		let entriesTable = ql.FindTable( "entries" );
		auto nameTable = [&]( const QL::TableQL& table, EStringKind kind, sv idBytes )->jobject {//file{ id name } / function{ id name }
			jobject jt;
			if( table.FindColumn("name") )
				jt["name"] = Strings.Find( kind, idBytes );
			if( table.FindColumn("id") )
				jt["id"] = to_string( ToGuid(idBytes) );
			return jt;
		};
		for( uint i=0; i<entries.size(); ++i ){
			if( i<ql.Offset() || (ql.Limit() && i>=ql.Offset()+ql.Limit()) )
				continue;
			auto& entry = entries.at( i );
			jobject jentry = entriesTable ? ToEntry( *entriesTable, entry, strings ) : jobject{};
			for( auto&& table : ql.Tables ){
				if( table.JsonName=="file" )
					jentry[table.JsonName] = nameTable( table, EStringKind::File, entry.file_id() );
				else if( table.JsonName=="function" )
					jentry[table.JsonName] = nameTable( table, EStringKind::Function, entry.function_id() );
				else if( table.JsonName=="user" ){
					jobject jt;
					if( table.FindColumn("id") )
						jt["id"] = entry.user_pk();
					jentry[table.JsonName] = move( jt );
				}
			}
			jentries.push_back( move(jentry) );
		}
		if( entriesTable )
			o["entries"] = move( jentries );
		if( strings ){
			jarray jstrings;
			for( let& [id,value] : *strings ){
				jobject s;
				s["id"] = to_string( id );
				s["value"] = value;
				jstrings.push_back( move(s) );
			}
			o["strings"] = move( jstrings );
		}
		return o;
	}
}