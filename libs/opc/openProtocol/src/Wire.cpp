#include "Wire.h"
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <jde/opc/openProtocol/logTags.h>
#include "Fields.h"

#define let const auto

namespace Jde::Opc::OpenProtocol{
	constexpr ELogTags _tags{ LogTag };

	α Frame( Header head, const Body& body )ι->string{
		auto frame = Encode( head, body );
		ASSERT( frame );
		return frame ? move( *frame ) : string{};
	}
	α Empty( uint16_t mid )ι->Body{
		auto body = DecodeBody( {.Mid=mid}, {} );
		ASSERT( body );
		return body ? move( *body ) : Body{};
	}

	α Link::Sent( uint16_t mid )ι->void{
		if( !Numbered )
			return;
		_send = NextSequence( _send );
		_awaiting = Awaited{ mid, _send };
	}
	α Link::Receive( uint8_t sequence )ι->EReceived{
		if( !Numbered || !sequence )
			return EReceived::Unnumbered;
		if( sequence==_expect ){
			_acked = sequence;
			_expect = NextSequence( sequence );
			return EReceived::Next;
		}
		return sequence==_acked ? EReceived::Repeat : EReceived::OutOfOrder;
	}
	α Link::Acknowledged( const Header& ack )ι->bool{
		if( !_awaiting || ack.Sequence!=_awaiting->Number )
			return false;
		_awaiting.reset();
		return true;
	}
	α Acknowledge( Link& link, uint16_t mid )ι->string{
		link.LastAck = Frame( {.Sequence=link.Expected()}, Mid9997{.Mid=mid} );
		return link.LastAck;
	}
	α Reject( const Link& link, uint16_t mid, uint16_t code )ι->string{
		return Frame( {.Sequence=link.Expected()}, Mid9998{.Mid=mid, .Code=code} );
	}

	α Framer::Next()ι->Read{
		if( _lost ){
			let nul = _buffer.find( '\0' );
			_buffer.erase( 0, nul==string::npos ? nul : nul+1 );
			if( nul==string::npos )
				return {};
			_lost = false;
		}
		let size = FrameSize( _buffer );
		if( !size ){
			_lost = true;
			return Noise{ _buffer.substr(0, HeaderSize) };
		}
		if( !*size || _buffer.size()<**size )
			return {};
		auto frame = _buffer.substr( 0, **size );
		_buffer.erase( 0, **size );
		return frame;
	}

	Ω withoutNul( sv frame )ι->sv{ return frame.ends_with( '\0' ) ? frame.substr( 0, frame.size()-1 ) : frame; }

	α Wire::Open( Events events )ι->void{
		_events = move( events );
		Read();
	}
	α Wire::Read()ι->void{
		_socket.async_read_some( boost::asio::buffer(_chunk), [self=shared_from_this()]( const boost::system::error_code& ec, size_t size ){
			if( self->_closed )
				return;
			if( ec ){
				self->Fail( ec );
				return;
			}
			self->_framer.Add( {self->_chunk.data(), size} );
			while( !self->_closed ){//a frame's handler may close the wire.
				auto next = self->_framer.Next();
				if( let noise = get_if<Framer::Noise>(&next) )
					self->_events.Noise( noise->Start );
				else if( let frame = get_if<string>(&next) ){
					if( self->_log )
						TRACE( "({}) < {}", self->_name, Quote(withoutNul(*frame)) );
					self->_events.Frame( move(*frame) );
				}
				else
					break;
			}
			if( !self->_closed )
				self->Read();
		} );
	}
	α Wire::Send( string frame, function<void()> sent )ι->void{
		if( _closed )
			return;
		if( _log )
			TRACE( "({}) > {}", _name, Quote(withoutNul(frame)) );
		_queue.emplace_back( move(frame), move(sent) );
		if( _queue.size()==1 )
			Write();
	}
	α Wire::Write()ι->void{
		boost::asio::async_write( _socket, boost::asio::buffer(_queue.front().Frame), [self=shared_from_this()]( const boost::system::error_code& ec, size_t ){
			if( self->_closed )
				return;
			if( ec ){
				self->Fail( ec );
				return;
			}
			let sent = move( self->_queue.front().Sent );
			self->_queue.pop_front();
			if( !self->_queue.empty() )
				self->Write();
			if( sent )
				sent();
		} );
	}
	α Wire::Fail( const boost::system::error_code& ec )ι->void{
		Close();
		_events.Closed( ec );
	}
	α Wire::Close()ι->void{
		if( exchange(_closed, true) )
			return;
		boost::system::error_code ignored;
		_socket.close( ignored );
		//Not here:  one of the events may be what called.
		boost::asio::post( _socket.get_executor(), [self=shared_from_this()]{ self->_events = {}; self->_queue.clear(); } );
	}
}