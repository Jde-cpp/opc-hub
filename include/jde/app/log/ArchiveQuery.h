#pragma once
#include <jde/ql/types/TableQL.h>
#include <jde/app/proto/Log.pb.h>

namespace Jde::App{
	//One local day's archive file on disk (Log.proto).  ArchiveQuery, below, is a logs query's result read out of these.
	using DayArchive = Log::Proto::ArchiveFile;
	//The on-disk layout - <root>/<year>/<month>/<day>/archive.binpb, one file per local day - and its inverse: every day that has one.
	α ArchiveDayFile( const fs::path& root, std::chrono::year_month_day ymd )ι->fs::path;
	α ArchiveDayFiles( const fs::path& root )ε->flat_map<std::chrono::year_month_day, fs::path>;

	//The four kinds of string a log entry names by md5;  an archive keeps one table of each (Log.proto's ArchiveQuery).
	enum class EStringKind : uint8{ Template, File, Function, Arg };
	constexpr std::array<EStringKind,4> StringKinds{ EStringKind::Template, EStringKind::File, EStringKind::Function, EStringKind::Arg };
	//Every (kind, id bytes) an entry names:  its template, file, function and each argument.
	Ξ ForEachStringId( const App::Log::Proto::LogEntryFile& e, auto&& f )->void{
		f( EStringKind::Template, e.template_id() );
		f( EStringKind::File, e.file_id() );
		f( EStringKind::Function, e.function_id() );
		for( const auto& arg : e.args() )
			f( EStringKind::Arg, arg );
	}
	//An archive proto's table of `kind`.
	α ArchiveStrings( DayArchive& af, EStringKind kind )ι->google::protobuf::RepeatedPtrField<App::Log::Proto::String>&;

	//The strings of each kind, by id.  std::map, not flat_map:  the Arg table grows with every distinct value logged, and a
	//sorted vector's insert is linear.
	struct StringTable{
		α Add( EStringKind kind, uuid id, string&& value )ι->void{ _maps[(uint8)kind].try_emplace( id, move(value) ); }//moves only when it inserts.
		α Find( EStringKind kind, sv idBytes )Ι->str;
		α operator[]( EStringKind kind )Ι->const std::map<uuid,string>&{ return _maps[(uint8)kind]; }
	private:
		std::array<std::map<uuid,string>,4> _maps;
	};

	struct ArchiveQuery{
		ArchiveQuery()ι=default;
		ArchiveQuery( ArchiveQuery&& x )ι=default;
		ArchiveQuery( const QL::Filter& q, vector<App::Log::Proto::FileEntry>&& entries )ε;
		α operator=( ArchiveQuery&& x )ι->ArchiveQuery& = default;
		α Append( const QL::TableQL& q, DayArchive&& af )ε->void;
		α Append( const QL::Filter& q, vector<App::Log::Proto::FileEntry>&& entries )ε->void;
		α ToJson( const QL::TableQL& ql )Ι->jobject;
		α IsComplete( const QL::Input& input )Ι->bool;
		α EntrySize()Ι->uint;
		α Sort( const vector<std::pair<string,bool>>& orderBy )Ι->vector<App::Log::Proto::LogEntryFile>;
		std::map<TimePoint,vector<App::Log::Proto::LogEntryFile>> Entries;
		StringTable Strings;
	private:
		α Message( const App::Log::Proto::LogEntryFile& entry )Ι->string;
		α Test( const QL::TableQL& q, TimePoint time, const App::Log::Proto::LogEntryFile& entry )Ε->bool;
		α Test( const QL::Filter& filter, TimePoint time, const App::Log::Proto::LogEntryFile& entry )Ι->bool;
		α ToEntry( const QL::TableQL& table, const App::Log::Proto::LogEntryFile& entry, optional<flat_map<uuid,string>>& strings )Ι->jobject;
	};
}