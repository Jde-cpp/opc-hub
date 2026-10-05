#pragma once
//A test's files read and written whole:  what a scan or a read sees of what the library wrote.
#include <fstream>
#include "../src/io/Reader.h"

namespace Jde::Opc::Hist::Tests{
	Ξ contents( const fs::path& file )ι->string{
		std::ifstream f{ file, std::ios::binary };
		return string{ std::istreambuf_iterator<char>{f}, {} };
	}
	Ξ save( const fs::path& file, sv bytes )ι->void{
		std::ofstream f{ file, std::ios::binary | std::ios::trunc };
		f.write( bytes.data(), bytes.size() );
	}
	//Every record but the checkpoints, read from start to end, with their times absolute.
	Ξ read( google::protobuf::io::ZeroCopyInputStream& in, uint start, uint end, Ticks chain )ι->vector<Proto::HistoryRecord>{
		Reader reader{ in, start, end, chain };
		vector<Proto::HistoryRecord> y;
		Proto::HistoryRecord r;
		while( reader.Next(r) ){
			if( !r.has_checkpoint() )
				y.push_back( r );
		}
		EXPECT_EQ( reader.Stop(), EStop::End );
		EXPECT_EQ( reader.Offset(), end );
		return y;
	}
	//A file's records, all of which its scan keeps.
	Ξ readFile( const fs::path& file )ε->vector<Proto::HistoryRecord>{
		const auto scanned = Scan( file );
		EXPECT_EQ( scanned.Size, scanned.FileSize ) << file;
		std::ifstream in{ file, std::ios::binary };
		google::protobuf::io::IstreamInputStream stream{ &in };
		return read( stream, 0, scanned.Size, 0 );
	}
}
