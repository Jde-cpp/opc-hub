#include <jde/web/Gzip.h>
#include <zlib.h>
#define let const auto

namespace Jde{
	α Web::Gunzip( sv compressed, uint limit, SL sl )ε->optional<string>{
		z_stream zs{ (Bytef*)compressed.data(), (uInt)compressed.size() };
		auto hr = inflateInit2( &zs, MAX_WBITS | 16 );
		THROW_IFSL( hr!=Z_OK, "inflateInit2 failed: {}", hr );
		string y;
		hr = Z_BUF_ERROR;
		//Z_BUF_ERROR with unused output space means truncated input, not a full buffer - stop instead of growing forever.
		for( uint nextOutSize = std::max<uint>(compressed.size(), 512); hr==Z_BUF_ERROR && zs.avail_out==0 && (!limit || y.size()<=limit); nextOutSize*=2 ){
			let start = y.size();
			let size = limit ? std::min( nextOutSize, limit+1-start ) : nextOutSize;//one past the limit is enough to know it was passed.
			y.resize( start+size );
			zs.next_out = (Bytef*)y.data()+start;
			zs.avail_out = (uInt)size;
			hr = inflate( &zs, Z_FINISH );
		}
		let totalOut = zs.total_out;
		inflateEnd( &zs );
		if( limit && totalOut>limit )
			return nullopt;
		THROW_IFSL( hr!=Z_STREAM_END, "Malformed gzip stream: {}", hr );
		y.resize( totalOut );
		return y;
	}
}