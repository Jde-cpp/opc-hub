#include <jde/fwk/io/records/Scan.h>
#include <jde/fwk/io/crc.h>
#include <jde/fwk/io/durable.h>
DISABLE_WARNINGS
#include <google/protobuf/io/coded_stream.h>
ENABLE_WARNINGS

#define let const auto
namespace Jde::IO{
	constexpr ELogTags _tags{ ELogTags::IO };
namespace Records{
	constexpr uint SealedWindowBytes{ 64<<10 };
	//What a scan that stopped short of the file's end found there, for the log and Truncate's refusal.
	Ω stopped( const Scanned& y )ι->string{
		let sealed = y.SealedAfter ? Ƒ( " since an append its owner sealed follows it, ending at byte {}", *y.SealedAfter ) : string{};
		return Ƒ( "{} at byte {}, which is {}{}", ToString(y.Stop), y.StopOffset, y.Keep() ? "no torn flush" : "a torn flush", sealed );
	}

	α Scanned::Keep()Ι->bool{
		let foreign = !StopOffset && Stop!=EStop::Length && Stop!=EStop::Empty;//a first frame no torn FileStart leaves.
		return Size<FileSize && (SealedAfter || foreign);
	}

	Scanner::Scanner( sp<OpenFile> file, uint32_t startField, uint32_t checkpointField, IDecoder& decoder, SL sl )ε:
		_file{ move(file) },
		_startField{ startField },
		_checkpointField{ checkpointField },
		_decoder{ decoder },
		_sl{ sl }{
		_y.FileSize = _file->Size( sl );//the open file's size, which a rename over its path can't pair with another file's bytes.
	}
	α Scanner::Feed( string&& bytes )ι->void{
		if( !_wants )
			return;
		if( bytes.size()<_wants->Size )
			_short = true;
		else
			_buffer.append( bytes.data(), _wants->Size );
		_wants.reset();
	}
	α Scanner::Advance( const Raw& raw )ι->void{
		_offset += raw.Frame.size();
		if( _offset-_base>WindowBytes ){//what is scanned is let go, a window at a time.
			_buffer.erase( 0, _offset-_base );
			_base = _offset;
		}
	}
	α Scanner::Next()ε->ENext{
		if( _short )
			throw IOException{ _sl, _file->Path(), ELogLevel::Error, "could not be read through to scan" };
		if( _wants )
			return ENext::More;
		if( _phase==EPhase::Records ){
			if( !Records() )
				return ENext::More;
			_decoder.Dropped();//the records after the last checkpoint, which nothing proves.
			if( _y.Size<_y.FileSize ){
				_phase = EPhase::Sealed;
				_buffer.clear();
				_base = _y.Size;
				_done = 0;
				_spans = { Span{} };
				Want( _y.Size, std::min<uint>(SealedWindowBytes, _y.FileSize-_y.Size) );
				return ENext::More;
			}
		}
		else if( _phase==EPhase::Sealed ){
			if( !Sealed() )
				return ENext::More;
			//Once, here, so a file that is only read says what its reads leave out.
			LOG( _y.Keep() ? ELogLevel::Error : ELogLevel::Warning, _tags, "'{}' holds {}:  reads serve only its first {} of {} bytes, and {}.", _file->Path().string(), stopped(_y), _y.Size, _y.FileSize, _y.Keep() ? "nothing appends to it" : "its first append drops the rest" );
		}
		_phase = EPhase::Done;
		return ENext::End;
	}
	//Every stop is just where the kept prefix ends:  whether what follows is torn or must be kept is Sealed's to judge,
	//and Keep's.
	α Scanner::Records()ε->bool{
		for( ;; ){
			_y.StopOffset = _offset;
			Cursor cursor{ sv{_buffer}.substr(_offset-_base), _offset };
			auto raw = cursor.Next();
			if( !raw ){
				let stop = *cursor.Stop();
				let have = _base+_buffer.size();
				if( (stop==EStop::Length || stop==EStop::End) && have<_y.FileSize ){//more of the file to read:  the first window, or the rest of a frame.
					let needed = stop==EStop::Length ? _offset+cursor.Needed()-have : 1;
					Want( have, std::min<uint>(std::max(WindowBytes, needed), _y.FileSize-have) );
					return false;
				}
				_y.Stop = stop;
				if( stop==EStop::Length && !_offset ){//a torn first frame is a torn FileStart only when a FileStart could fill it.
					let tag = (char)( (_startField<<3)|2 );
					if( _buffer.empty() || _buffer[0]!=tag )
						_y.Stop = EStop::NoFileStart;
					else if( cursor.Needed()>MaxStartFrame )
						_y.Stop = EStop::BadLength;
				}
				return true;
			}
			if( !_start ){
				auto start = raw->Field==_startField ? DecodeStart( raw->Body ) : nullopt;
				if( !start ){
					_y.Stop = EStop::NoFileStart;
					return true;
				}
				if( auto decoded = _decoder.Decode(*raw); !decoded ){
					_y.Stop = decoded.error();
					return true;
				}
				_start = start;
				if( start->generation() ){//an archive has no checkpoints:  all of it is kept, and read through only for a decoder that wants it.
					_archive = true;
					_y.Start = start;
					_y.Size = _y.FileSize;
					_y.StopOffset = _y.FileSize;
					if( !_decoder.Consumes() )
						return true;
					_decoder.Sealed();
				}
				else
					_crc = Crc::Extend32c( _crc, raw->Frame );
				Advance( *raw );
				continue;
			}
			if( _archive ){
				if( auto decoded = _decoder.Decode(*raw); !decoded ){
					_y.Stop = decoded.error();
					return true;
				}
				_decoder.Sealed();
				Advance( *raw );
				continue;
			}
			if( raw->Field==_startField ){//the owner writes one, so no append that holds another is one to build on.
				_y.Stop = EStop::SecondStart;
				return true;
			}
			if( raw->Field==_checkpointField ){
				let stored = DecodeCheckpoint( raw->Body );
				if( !stored ){
					_y.Stop = EStop::Body;
					return true;
				}
				if( *stored!=_crc ){
					_y.Stop = EStop::Crc;
					return true;
				}
				if( !_y.Start )
					_y.Start = _start;
				_decoder.Sealed();
				Advance( *raw );
				_y.Size = _offset;
				_y.Chain = _decoder.Chain();
				if( _run ){
					_run->End = _y.Size;
					_y.Runs.push_back( *_run );
					_run.reset();
				}
				_crc = 0;
				continue;
			}
			auto decoded = _decoder.Decode( *raw );
			if( !decoded ){
				_y.Stop = decoded.error();
				return true;
			}
			_crc = Crc::Extend32c( _crc, raw->Frame );
			if( let t = *decoded; t ){
				if( !_run )
					_run = Run{ .Offset=_y.Size, .End=0, .Chain=_y.Chain, .First=*t, .Last=*t };//_y holds where the append starts.
				_run->First = std::min( _run->First, *t );
				_run->Last = std::max( _run->Last, *t );
			}
			Advance( *raw );
		}
	}
	//Where the first append past Size that its owner sealed ends:  a checkpoint, found by its bytes, whose CRC matches the
	//bytes since the checkpoint before it, or since Size.  Found by bytes, not frames, so damage that broke the framing
	//can't hide it, though one whose own checkpoint was damaged too can't be proved.  A candidate whose crc matches no
	//span is taken as data, and a span starts after it:  a mark's three bytes turn up inside bodies now and then, and one
	//must not end the span a real checkpoint's crc covers.  An empty span proves nothing.
	α Scanner::Sealed()ι->bool{
		let mark = CheckpointMark( _checkpointField );
		const sv markBytes{ mark.data(), mark.size() };
		let extend = [&]( uint end ){
			if( end<=_done )
				return;
			const sv bytes = sv{ _buffer }.substr( _done, end-_done );
			for( auto& span : _spans ){
				span.Crc = Crc::Extend32c( span.Crc, bytes );
				span.Spanned = true;
			}
			_done = end;
		};
		for( auto p = _buffer.find(markBytes, _done); p!=string::npos && p+CheckpointBytes<=_buffer.size(); p = _buffer.find(markBytes, _done) ){
			extend( (uint)p );
			uint32_t stored;
			(void)google::protobuf::io::CodedInputStream::ReadLittleEndian32FromArray( reinterpret_cast<const uint8_t*>(_buffer.data()+p+mark.size()), &stored );
			for( let& span : _spans ){
				if( span.Spanned && span.Crc==stored ){
					_y.SealedAfter = _base+(uint)p+CheckpointBytes;
					return true;
				}
			}
			extend( (uint)p+CheckpointBytes );
			_spans.push_back( Span{} );
			if( _spans.size()>64 )
				_spans.erase( _spans.begin() );
		}
		let have = _base+_buffer.size();
		if( have>=_y.FileSize )
			return true;
		extend( std::max<uint>(_done, _buffer.size()>=CheckpointBytes ? (uint)_buffer.size()-(CheckpointBytes-1) : 0) );//the rest could start a mark.
		_base += _done;
		_buffer.erase( 0, _done );
		_done = 0;
		Want( have, std::min<uint>(SealedWindowBytes, _y.FileSize-have) );
		return false;
	}
}
	α Records::Truncate( const fs::path& path, Scanned& y, SL sl )ε->void{
		if( y.Keep() )
			throw IOException{ sl, path, ELogLevel::Error, "holds {}:  it is left as it is", Records::stopped(y) };
		if( y.Size==y.FileSize )
			return;
		std::error_code ec;
		let now = fs::file_size( path, ec );
		if( ec )
			throw Failed( path, ec, sl );
		if( now!=y.FileSize )
			throw IOException{ sl, path, ELogLevel::Error, "holds {} bytes, not the {} it was scanned at, and is left as it is", now, y.FileSize };
		WARN( "Truncating '{}' from {} bytes to {}, its last good checkpoint:  {} at byte {}.", path.string(), y.FileSize, y.Size, ToString(y.Stop), y.StopOffset );
		fs::resize_file( path, y.Size, ec );
		if( ec )
			throw Failed( path, ec, sl );
		y.FileSize = y.Size;
	}
	α Records::FetchNow( const sp<OpenFile>& file, Window window, SL sl )ε->string{
		string y( window.Size, '\0' );
		y.resize( file->Read(window.Offset, y.data(), window.Size, sl) );
		return y;
	}
	α Records::Head( const sp<OpenFile>& file, uint32_t startField, SL sl )ε->optional<Proto::FileStart>{
		auto bytes = Records::FetchNow( file, Window{0, MaxStartFrame}, sl );
		Records::Cursor cursor{ bytes, 0 };
		auto raw = cursor.Next();
		return raw && raw->Field==startField ? Records::DecodeStart( raw->Body ) : nullopt;
	}
	α Records::ScanNow( sp<OpenFile> file, uint32_t startField, uint32_t checkpointField, IDecoder& decoder, SL sl )ε->Scanned{
		Records::Scanner scanner{ move(file), startField, checkpointField, decoder, sl };
		while( scanner.Next()==ENext::More )
			Records::FeedNow( scanner, sl );
		return scanner.Result();
	}
}