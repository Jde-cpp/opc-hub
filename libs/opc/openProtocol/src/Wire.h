#pragma once
#include <deque>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <jde/opc/openProtocol/Codec.h>

//What the session and the controller's side of one share:  a subscription's MIDs, link level's sequence numbers, a byte
//stream's frames and a socket that reads and writes them.  None of it is safe off its strand.
namespace Jde::Opc::OpenProtocol{
	//A subscription on the wire:  the MID that asks for it, the one that ends it, a MID it pushes and the MID that
	//acknowledges that push at application level.  The alarm subscription pushes three.
	struct Topic{ uint16_t Subscribe; uint16_t Unsubscribe; uint16_t Data; uint16_t Ack; };
	constexpr array<Topic,7> Topics{{
		{14, 17, 15, 16}, {34, 37, 35, 36}, {51, 54, 52, 53}, {60, 63, 61, 62}, {70, 73, 71, 72}, {70, 73, 74, 75}, {70, 73, 76, 77}
	}};
	Ξ TopicOf( uint16_t data )ι->const Topic*{
		const auto topic = find_if( Topics, [=]( const Topic& t ){ return t.Data==data; } );
		return topic==Topics.end() ? nullptr : &*topic;
	}

	α Frame( Header head, const Body& body )ι->string;//Encode, for a message that cannot be refused.
	α Empty( uint16_t mid )ι->Body;//a MID with no data field, as its body.

	constexpr α NextSequence( uint8_t n )ι->uint8_t{ return n>=99 ? 1 : n+1; }
	//Link level's sequence numbers as one side keeps them (§2.2.5, §3.2.2):  1 to 99 and around, a run each way, both
	//from 1 after MID 0001 and 0002.  A message is acknowledged by MID 9997 carrying the number after its own, and one
	//message out waits for its acknowledgement at a time.
	struct Link final{
		enum class EReceived : uint8_t{
			Unnumbered,//the link is not numbered, or the message carries no number:  nothing to acknowledge here.
			Next,//the number expected.
			Repeat,//the number last acknowledged:  a retransmission.
			OutOfOrder
		};
		α Reset( bool numbered )ι->void{ *this = {}; Numbered = numbered; }
		α Number()Ι->uint8_t{ return Numbered ? _send : 0; }//what the next message out carries, 0 where the link is not numbered.
		α Sent( uint16_t mid )ι->void;//a message out under Number().
		α Skip()ι->void{ _send = NextSequence( _send ); }//a fault:  the next message out a number late.
		α Receive( uint8_t sequence )ι->EReceived;
		α Expected()Ι->uint8_t{ return _expect; }
		α Awaiting()Ι->optional<uint16_t>{ return _awaiting ? optional{ _awaiting->Mid } : nullopt; }//the MID out that waits for its MID 9997.
		α Acknowledged( const Header& ack )ι->bool;//a MID 9997 in:  false where it is not the one awaited.
		bool Numbered{};
		string LastAck;//the acknowledgement last sent, which a retransmission gets again.
	private:
		struct Awaited{ uint16_t Mid; uint8_t Number; };
		uint8_t _send{ 1 };
		uint8_t _expect{ 1 };
		uint8_t _acked{};
		optional<Awaited> _awaiting;
	};
	α Acknowledge( Link& link, uint16_t mid )ι->string;//MID 9997 for the message Receive just took, kept as LastAck.
	α Reject( const Link& link, uint16_t mid, uint16_t code )ι->string;//MID 9998 with Table 9's code, carrying the number expected.

	//A byte stream cut into frames by their lengths, never by the NUL (§3.8).  Bytes whose length does not read have lost
	//their place:  they are reported once and dropped through the next NUL, where a frame can start again.
	struct Framer final{
		struct Noise{ string Start; };//the first bytes of what begins no frame.
		using Read = variant<std::monostate,string,Noise>;//nothing whole yet, a frame with its NUL, or noise.
		α Add( sv bytes )ι->void{ _buffer += bytes; }
		α Next()ι->Read;
	private:
		string _buffer;
		bool _lost{};
	};

	//A timer armed again and again:  what an earlier Arm was to run never does, whether its wait was cancelled in time or
	//had already ended, nor does anything once this is gone.
	struct Deadline final{
		Deadline( const boost::asio::any_io_executor& executor )ι: _timer{ executor }{}
		~Deadline(){ ++*_armed; }
		Τ α Arm( steady_clock::duration after, T f )ι->void{
			_timer.expires_after( after );
			_timer.async_wait( [armed=_armed, mine=++*_armed, f=move(f)]( const boost::system::error_code& ec )mutable{
				if( !ec && mine==*armed )
					f();
			} );
		}
		α Disarm()ι->void{ ++*_armed; _timer.cancel(); }
	private:
		boost::asio::steady_timer _timer;
		const sp<uint> _armed{ ms<uint>() };//the count of Arms and Disarms, which a wait outlives this to read.
	};

	//One TCP connection's frames on its strand:  in by their lengths, out whole and in the order sent, each at Trace when
	//`log` is on.  Its pending reads and writes hold it, and it holds what its events capture until it closes, so whoever
	//opens it closes it.
	struct Wire final : std::enable_shared_from_this<Wire>{
		using Tcp = boost::asio::ip::tcp;
		struct Events{
			function<void( string&& frame )> Frame;
			function<void( sv start )> Noise;
			function<void( const boost::system::error_code& ec )> Closed;//the socket failed or the peer closed it, never Close.
		};
		Wire( Tcp::socket socket, string name, bool log )ι: _socket{ move(socket) }, _name{ move(name) }, _log{ log }{}
		α Socket()ι->Tcp::socket&{ return _socket; }
		α Open( Events events )ι->void;//reads from here on.
		α Send( string frame, function<void()> sent={} )ι->void;//`sent` runs once it is written, where the wire is still open.
		α Close()ι->void;
	private:
		struct Out{ string Frame; function<void()> Sent; };
		α Read()ι->void;
		α Write()ι->void;
		α Fail( const boost::system::error_code& ec )ι->void;
		Tcp::socket _socket;
		const string _name;
		const bool _log;
		Events _events;
		Framer _framer;
		array<char,4096> _chunk;
		std::deque<Out> _queue;
		bool _closed{};
	};
}