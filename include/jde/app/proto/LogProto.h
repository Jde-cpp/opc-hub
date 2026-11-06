#pragma once
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <jde/app/usings.h>
#include <jde/app/proto/Log.pb.h>

namespace Jde::App::LogProto{
	//Where a session's string table starts again, the same at both ends:  a batch that begins with the table at either limit resets
	//it.  Bytes counts the Strings' text, since a count alone lets each String be a whole frame.
	struct StringCap final{ uint Count{10'000}; uint Bytes{16'000'000}; };
	//The Strings a client's log batches have sent its AppServer on one socket session:  each goes once, until the table reaches its
	//cap and both ends start again (ClientBatch.reset).  A new session starts empty at both ends.
	struct SentStrings final{
		explicit SentStrings( StringCap cap={} )ι:_cap{ cap }{}
		α Batch( vector<Logging::Entry>&& entries )ι->Log::Proto::ClientBatch;
	private:
		absl::flat_hash_set<uuid,std::hash<uuid>> _ids;//hashed, not sorted:  the ids are random md5s, so a sorted vector moves half the table per insert.
		uint _bytes{};
		StringCap _cap;
	};
	//The AppServer's end of SentStrings:  each String is hashed once as it arrives, to check the id it came with, and each entry is
	//rebuilt from the session's Strings with the ids it named, so no entry is hashed again.
	struct ReceivedStrings final{
		struct Entries final{ vector<Logging::Entry> Built; uint Unresolved{}; };//Unresolved:  dropped, as they name a String the session wasn't sent.
		explicit ReceivedStrings( StringCap cap={} )ι:_cap{ cap }{}
		//Takes the batch's Strings even when the caller then drops its entries.  Throws, taking nothing, for a batch that begins at
		//the cap without a reset:  SentStrings would have reset there, so the table holds at most the cap plus one batch.  Throws
		//the same way for a String whose id isn't its text's md5:  the file's dedupe and the archive's merge key a String by its id,
		//so one client could otherwise rename another's lines.
		α Read( Log::Proto::ClientBatch&& batch )ε->Entries;
		α Size()Ι->uint{ return _strings.size(); }
	private:
		absl::flat_hash_map<uuid,string,std::hash<uuid>> _strings;//hashed, as SentStrings::_ids.
		uint _bytes{};
		StringCap _cap;
	};

	struct SourceFields final{ bool File{true}; bool Function{true}; };//File carries the line:  a line without its file says nothing.
	α LogEntryFile( const Logging::Entry& m, App::ProgramPK appPK=0, App::ProgInstPK instancePK=0, SourceFields source={} )ι->Log::Proto::LogEntryFile;//non-zero pks: forwarded from that instance.
	α ToString( uuid id, string&& value )ι->Log::Proto::String;
	//A typed argument as its entry rendered it, fmt's {}:  none for a string's id, which the caller resolves.
	α ArgText( const Log::Proto::Arg& arg )ι->optional<string>;
	//Debugger-facing; see LogProto.cpp for why these are unconditional.
	α DebugString( const Log::Proto::FileEntry& f )ι->string;
	α DebugString( const Log::Proto::LogEntryFile& f )ι->string;

}