#include <jde/web/Gzip.h>
#include <zlib.h>
#include <absl/strings/match.h>
#define let const auto

namespace Jde{
	α Web::Gunzip( sv compressed, uint limit, SL sl )ε->optional<string>{
		z_stream zs{ (Bytef*)compressed.data(), (uInt)compressed.size() };
		auto hr = inflateInit2( &zs, MAX_WBITS | 16 );
		THROW_IFSL( hr!=Z_OK, "inflateInit2 failed: {}", hr );
		up<z_stream,decltype(&inflateEnd)> end{ &zs, &inflateEnd };//on every exit, a resize that throws included.
		string y;
		hr = Z_BUF_ERROR;
		//Z_BUF_ERROR with unused output space means truncated input, not a full buffer - stop instead of growing forever.
		for( uint nextOutSize = std::max<uint>(compressed.size(), 512); hr==Z_BUF_ERROR && zs.avail_out==0 && (!limit || y.size()<=limit); nextOutSize*=2 ){
			let start = y.size();
			let wanted = limit ? std::min( nextOutSize, limit+1-start ) : nextOutSize;//one past the limit is enough to know it was passed.
			let size = std::min<uint>( wanted, std::numeric_limits<uInt>::max() );//avail_out is 32 bits - a bigger step would truncate, to 0 at 4 GiB, and stall inflate.
			y.resize( start+size );
			zs.next_out = (Bytef*)y.data()+start;
			zs.avail_out = (uInt)size;
			hr = inflate( &zs, Z_FINISH );
		}
		if( limit && zs.total_out>limit )
			return nullopt;
		THROW_IFSL( hr!=Z_STREAM_END, "Malformed gzip stream: {}", hr );
		THROW_IFSL( zs.avail_in, "{} bytes after the gzip stream.", zs.avail_in );//a second member or garbage - inflate stops at the first member's end.
		y.resize( zs.total_out );
		return y;
	}
	α Web::IsGzip( sv contentEncoding )ι->bool{
		return absl::EqualsIgnoreCase( contentEncoding, "gzip" ) || absl::EqualsIgnoreCase( contentEncoding, "x-gzip" );
	}
}