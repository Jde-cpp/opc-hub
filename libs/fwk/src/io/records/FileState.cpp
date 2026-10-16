#include <jde/fwk/io/records/FileState.h>
#include <jde/fwk/io/durable.h>

#define let const auto
namespace Jde::IO{
	α Records::StampOf( const fs::path& path )ι->optional<Stamp>{
		std::error_code ec;
		let size = fs::file_size( path, ec );
		if( ec )
			return nullopt;
		let written = fs::last_write_time( path, ec );
		return ec ? nullopt : optional<Stamp>{ Stamp{size, written} };
	}
namespace Records{
	α FileState::Hand( uint offset, uint end )ι->void{
		if( Outstanding && Outstanding->Offset==offset )
			Outstanding->End = std::max( Outstanding->End, end );
		else
			Outstanding = Append{ offset, end };
	}
	α FileState::Committed( uint end, Records::State chain, vector<Run>&& runs )ι->void{
		Named = true;
		Outstanding.reset();
		Size = end;
		Chain = chain;
		std::ranges::move( runs, std::back_inserter(Runs) );
	}
}
	α Records::Verify( const FileState& file, bool rewrite, bool restart, SL sl )ε->Verified{
		std::error_code ec;
		let existed = fs::exists( file.Path, ec );
		let actual = existed && !ec ? fs::file_size( file.Path, ec ) : 0;
		if( ec )
			throw Failed( file.Path, ec, sl );
		let size = restart ? 0 : file.Size;
		let& outstanding = file.Outstanding;
		let scanned = rewrite && !restart && file.Unopened ? file.Unopened->FileSize : size;
		if( !actual && size )
			restart = true;//purged since, or a parent no longer resolves.
		//More than Size is what an append of this process's left when it failed part-way, which the run's write cuts off.  A
		//file no append has gone to may be one restored or remade since, which only a scan can tell from what that left.
		else if( actual<size || (actual>size && actual!=scanned && (!size || !outstanding || outstanding->Offset!=size || actual>outstanding->End)) )
			throw IOException{ sl, file.Path, ELogLevel::Error, "holds {} bytes, not the {} written to it, so its next write scans it again", actual, size };
		return { existed, restart };
	}
}