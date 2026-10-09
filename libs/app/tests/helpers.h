#pragma once
//Builders shared by the suites:  log entries, the two archive wire shapes ArchiveQuery is fed, and schema-free TableQLs.
#include <jde/fwk/log/Entry.h>
#include <jde/app/proto/Log.pb.h>
#include <jde/app/proto/LogProto.h>
#include <jde/app/log/ArchiveQuery.h>
#include <jde/ql/types/Parser.h>
#include <jde/ql/types/TableQL.h>

#define let const auto

namespace Jde::App::Tests{
	Ξ tp( uint seconds )ι->TimePoint{ return Clock::from_time_t( 1'700'000'000 )+std::chrono::seconds{seconds}; }

	Ξ entry( TimePoint time, ELogLevel level, uint32_t line, string text, vector<string> args={}, string file="src/x.cpp", string function="Fn", ELogTags tags=ELogTags::Test, UserPK user=UserPK{7} )ι->Logging::Entry{
		return Logging::Entry{ level, tags, line, time, user, move(text), move(file), move(function), move(args) };
	}

	//A forwarded entry as builds before 2026-10 wrote it.  The two messages share their field numbers, so the bytes parse as either.
	Ξ asOlderExternal( const Log::Proto::LogEntryFile& e )ι->Log::Proto::LogEntryFileExternal{
		Log::Proto::LogEntryFileExternal y;
		[[maybe_unused]] let parsed = y.ParseFromString( e.SerializeAsString() );//shared field numbers - it cannot fail.
		return y;
	}
	//The strings an entry references:  (kind, id, text) for its template, file, function and each argument.
	Ξ entryStrings( const Logging::Entry& e )ι->vector<std::tuple<EStringKind,uuid,string>>{
		vector<std::tuple<EStringKind,uuid,string>> y{
			{EStringKind::Template, e.Id(), string{e.Text}}, {EStringKind::File, e.FileId(), string{e.File()}}, {EStringKind::Function, e.FunctionId(), string{e.Function()}} };
		for( let& arg : e.Arguments )
			y.emplace_back( EStringKind::Arg, Logging::Entry::GenerateId(arg), arg );
		return y;
	}
	//...in the archive file's per-kind tables.
	Ξ addStrings( DayArchive& af, const Logging::Entry& e )ι->void{
		for( auto&& [kind, id, text] : entryStrings(e) )
			*ArchiveStrings( af, kind ).Add() = LogProto::ToString( id, move(text) );
	}

	//What ArchiveAwait reads back out of an archived day:  entries plus the whole string table.
	Ξ archiveProto( const vector<Logging::Entry>& entries )ι->DayArchive{
		DayArchive af;
		for( let& e : entries ){
			*af.add_entries() = LogProto::LogEntryFile( e );
			addStrings( af, e );
		}
		return af;
	}

	//What the daily file/ProtoLog buffer holds:  a flat FileEntry stream, each string once, ahead of the entry naming it.
	Ξ fileEntries( const vector<Logging::Entry>& entries )ι->vector<Log::Proto::FileEntry>{
		vector<Log::Proto::FileEntry> y;
		auto addString = [&]( uuid id, string value ){
			Log::Proto::FileEntry fe;
			*fe.mutable_str() = LogProto::ToString( id, move(value) );
			y.emplace_back( move(fe) );
		};
		for( let& e : entries ){
			for( auto&& [_, id, text] : entryStrings(e) )
				addString( id, move(text) );
			Log::Proto::FileEntry fe;
			*fe.mutable_entry() = LogProto::LogEntryFile( e );
			y.emplace_back( move(fe) );
		}
		return y;
	}

	//`system` leaves _dbTable null - which is what the log tables are:  they have no view, and nothing here opens a data source.
	Ξ table( sv jsonName, sv args="{}" )ε->QL::TableQL{
		static const vector<sp<DB::AppSchema>> noSchemas;
		return QL::TableQL{ string{jsonName}, QL::Parser::ParseArgs(string{args}), ms<jobject>(), noSchemas, true };
	}
	//TableQL::AddColumn resolves the db column off _dbTable, which is null here;  the archive reads JsonName only.
	Ξ addColumns( QL::TableQL& t, std::initializer_list<sv> columns )ι->QL::TableQL&{
		for( let& c : columns )
			t.Columns.push_back( QL::ColumnQL{string{c}, {}} );
		return t;
	}
	Ξ addTable( QL::TableQL& parent, sv jsonName, std::initializer_list<sv> columns, sv args="{}" )ε->QL::TableQL&{
		parent.Tables.emplace_back( table(jsonName, args) );
		return addColumns( parent.Tables.back(), columns );
	}
}
#undef let
