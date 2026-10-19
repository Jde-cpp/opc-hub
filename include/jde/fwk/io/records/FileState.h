#pragma once
#include "Scan.h"

#define Φ Γ auto
namespace Jde::IO::Records{
	//A file's size and last write, which tell that something changed it.
	struct Stamp final{
		uint Size;
		fs::file_time_type Written;
		α operator==( const Stamp& )Ι->bool = default;
	};
	Φ StampOf( const fs::path& path )ι->optional<Stamp>;//none for a file that isn't there, or can't be stat'ed.

	//A file as the process knows it:  from its first-open scan, and from its own appends since.  A live file's, or one its
	//owner leaves alone.
	struct Γ FileState{//an owner adds its own, as the historian does its map.
		fs::path Path;
		uint32_t Generation{};//past 0, an archive, with no runs:  Size is all of it.
		uint Size{};//through its last checkpoint, where the next append goes.  At 0 that append opens with the FileStart.
		State Chain{};//where the owner's state stands.
		vector<Run> Runs;//each append that holds a record with a time, as the first-open scan would rebuild them.
		optional<Scanned> Unopened;//its scan, until the first append drops a torn tail by it.
		bool Named{};//this process made it, or has fsynced its name into its directory:  one that crashed may have made it and not.
		string Refused;//why the owner won't write to it; empty when it will.
		//As its first-open scan began, or its rewrite left it:  a refused file or an archive changed since, repaired perhaps,
		//is scanned again.
		optional<Stamp> Stamped;
		uint Discarded{};//the appends refused so far.
		//The append handed out and not yet committed, which may have failed part-way:  the Size it went at, and the farthest
		//it could have written.  The bytes past that Size, up to End, are its own, which the next append cuts.
		struct Append final{ uint Offset; uint End; };
		optional<Append> Outstanding;
		//An append handed out at offset, reaching end:  one that failed before cutting the file to its offset leaves what an
		//earlier one did.
		α Hand( uint offset, uint end )ι->void;
		//Its write has returned:  the file holds end bytes, the owner's state stands at chain, and runs are its new runs.
		α Committed( uint end, State chain, vector<Run>&& runs )ι->void;
		//A refused file or an archive the disk no longer holds as Stamped, to scan again.  Never a live file, which its own
		//appends change.
		α Changed()Ι->bool;
	};
	struct Verified final{
		bool Existed;
		bool Restart;//the file was removed or purged since, so what was known of it goes.
	};
	//Whether the file on disk is the one the process knows, before a write:  there and the size written, or past it by an
	//outstanding append's own bytes, or gone.  rewrite:  the write renames over the file, so a torn tail the scan found
	//goes whole with it and its size counts as known.  Throws at Error when the file holds other bytes, for the caller to
	//forget what it knew so the next write scans it again.
	Φ Verify( const FileState& file, bool rewrite, bool restart, SRCE )ε->Verified;
}
#undef Φ