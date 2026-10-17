#pragma once
//A test's files read and written whole:  what a scan or a read sees of what the library wrote.
#include <fstream>
#include "../src/io/Records.h"

namespace Jde::Opc::Hist::Tests{
	Ξ contents( const fs::path& file )ι->string{
		std::ifstream f{ file, std::ios::binary };
		return string{ std::istreambuf_iterator<char>{f}, {} };
	}
	Ξ save( const fs::path& file, sv bytes )ι->void{
		std::ofstream f{ file, std::ios::binary | std::ios::trunc };
		f.write( bytes.data(), bytes.size() );
	}
	//Every record but the checkpoints, from start to end of bytes, with their times absolute.
	Ξ read( sv bytes, uint start, uint end, Ticks chain )ι->vector<Proto::HistoryRecord>{
		Reader reader{ Run{.Offset=start, .End=end, .Chain=chain, .First=0, .Last=0} };
		vector<Proto::HistoryRecord> y;
		Proto::HistoryRecord r;
		for( ;; ){
			const auto next = reader.Next( r );
			if( next==ENext::More ){
				const auto w = *reader.Wants();
				reader.Feed( string{bytes.substr(w.Offset, w.Size)} );
				continue;
			}
			if( next==ENext::End )
				break;
			if( !r.has_checkpoint() )
				y.push_back( r );
		}
		EXPECT_EQ( reader.Stop(), EStop::End );
		EXPECT_EQ( reader.Offset(), end );
		return y;
	}
	//A file's first-open scan, read now, each record it keeps handed to sealed.
	Ξ scan( const fs::path& file, std::function<void( Proto::HistoryRecord& )> sealed={} )ε->Scanned{
		Decoder decoder{ move(sealed) };
		return IO::Records::ScanNow( ms<IO::OpenFile>(file), StartField, CheckpointField, decoder );
	}
	//A file's records, all of which its scan keeps.
	Ξ readFile( const fs::path& file )ε->vector<Proto::HistoryRecord>{
		const auto scanned = scan( file );
		EXPECT_EQ( scanned.Size, scanned.FileSize ) << file;
		return read( contents(file), 0, scanned.Size, 0 );
	}
	//A file's FileStart, when it opens with one whose crc matches.
	Ξ head( const fs::path& file )ε->optional<Proto::FileStart>{ return IO::Records::Head( ms<IO::OpenFile>(file), StartField ); }
	//A live file's is 0.
	Ξ generation( const fs::path& file )ε->uint32_t{
		const auto start = head( file );
		if( !start )
			ADD_FAILURE() << file << " opens with no FileStart.";
		return start ? start->generation() : std::numeric_limits<uint32_t>::max();
	}
}