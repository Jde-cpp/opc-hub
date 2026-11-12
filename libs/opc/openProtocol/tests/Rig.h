#pragma once
#include <future>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <jde/fwk/process/execution.h>
#include <jde/opc/openProtocol/Controller.h>
#include <jde/opc/openProtocol/Session.h>
#include <jde/tests/WaitUntil.h>
#include "helpers.h"

//A session against the controller's side of the codec, on a loopback port the system picks, and what each side heard.
namespace Jde::Opc::OpenProtocol::Tests{
	//The suite's message of a MID at a revision (Cases.cpp).
	Τ α CaseOf( uint16_t revision=T::MaxRevision )ι->T{
		for( let& c : Cases() ){
			if( c.Expected.Head.Mid==T::Id && c.Expected.Head.Revision==revision )
				return get<T>( c.Expected.Data );
		}
		ADD_FAILURE() << "no case for MID " << T::Id << " revision " << revision;
		return {};
	}
	//A result under a tightening id of the test's own.
	Ξ Rundown( uint32_t id )ι->Mid0061{
		auto result = CaseOf<Mid0061>();
		result.TighteningId = id;
		return result;
	}
	//A station that answers every revision the codec has, and at MID 0002's revision 6 says it numbers its messages;
	//Unnumbered's says it does not.
	Ξ Scripted()ι->Controller::Script{ return { .Identity=CaseOf<Mid0002>() }; }
	//The timing keys scaled down, with the keep-alive and the reply's deadline out of a case's way.
	Ξ Quick()ι->Session::Settings{ return { .KeepAlive=5s, .ReplyTimeout=2s, .ReconnectMin=50ms, .ReconnectMax=400ms }; }

	//What a session told its caller, kept for the test's thread.
	struct Heard{
		α Events( function<void( const Message& )> onPush={} )ι->Session::Events{
			return {
				[this]( Session::Connection&& connection ){ lg _{ _mutex }; _connections.push_back( move(connection) ); },
				[this]( const Failure& why ){ lg _{ _mutex }; _losses.push_back( why ); },
				[this, onPush=move(onPush)]( Message&& m ){
					if( onPush )
						onPush( m );
					lg _{ _mutex };
					_pushes.push_back( move(m) );
				}
			};
		}
		α Connections()Ι->vector<Session::Connection>{ lg _{ _mutex }; return _connections; }
		α Losses()Ι->vector<Failure>{ lg _{ _mutex }; return _losses; }
		α Pushes()Ι->vector<Message>{ lg _{ _mutex }; return _pushes; }
	private:
		mutable mutex _mutex;
		vector<Session::Connection> _connections;
		vector<Failure> _losses;
		vector<Message> _pushes;
	};

	Ξ Of( const vector<Controller::Received>& frames, uint16_t mid )ι->vector<Controller::Received>{
		vector<Controller::Received> y;
		for( let& frame : frames ){
			if( frame.Head.Mid==mid )
				y.push_back( frame );
		}
		return y;
	}
	Ξ Revisions( const vector<Controller::Received>& frames )ι->vector<uint16_t>{
		vector<uint16_t> y;
		for( let& frame : frames )
			y.push_back( frame.Head.Revision );
		return y;
	}
	Ξ Sequences( const vector<Controller::Received>& frames )ι->vector<uint16_t>{
		vector<uint16_t> y;
		for( let& frame : frames )
			y.push_back( frame.Head.Sequence );
		return y;
	}
	Τ α As( const Controller::Received& frame )ι->T{
		let m = Decode( frame.Frame );
		if( let body = m ? get_if<T>(&m->Data) : nullptr )
			return *body;
		ADD_FAILURE() << "MID " << frame.Head.Mid << " is no MID " << T::Id;
		return {};
	}
	//Push `m` as its type, where a session got that.
	Τ α As( const Message& m )ι->T{
		if( let body = get_if<T>(&m.Data) )
			return *body;
		ADD_FAILURE() << "MID " << m.Head.Mid << " is no MID " << T::Id;
		return {};
	}

	Ξ Ask( Session& session, Body request )ι->Session::Reply{
		auto answered = ms<std::promise<Session::Reply>>();
		auto reply = answered->get_future();
		session.Request( move(request), [answered]( Session::Reply&& y ){ answered->set_value( move(y) ); } );
		if( reply.wait_for(10s)!=std::future_status::ready )
			return std::unexpected{ Failure{EFailure::Timeout, 0, 0, "The test's own wait."} };
		return reply.get();
	}
	Ξ Why( const Session::Reply& reply )ι->string{ return reply ? string{} : reply.error().What; }
	//MID 0003 where the link is up, and the close:  false where `stopped` never ran.
	Ξ Stop( Session& session )ι->bool{
		auto stopped = ms<std::promise<void>>();
		session.Stop( [stopped]{ stopped->set_value(); } );
		return stopped->get_future().wait_for( 10s )==std::future_status::ready;
	}
	Ξ Unnumbered()ι->Controller::Script{
		auto script = Scripted();
		script.Identity.SequenceNumberSupport = 0;
		return script;
	}

	struct Rig{
		Rig( Controller::Script script=Scripted(), Session::Settings settings=Quick(), function<void( const Message& )> onPush={} ):
			Station{ *Executor(), 0, move(script) },
			Ran{ boost::asio::make_strand(*Executor()) },
			Client{ Ran, "127.0.0.1", Station.Port(), move(settings), Told.Events(move(onPush)) }
		{}
		~Rig(){ Stop(); }
		//The session started, and MID 0002 here.
		α Start()ι->bool{
			Client.Start();
			return WaitUntil( [&]{ return !Told.Connections().empty(); } );
		}
		α Ask( Body request )ι->Session::Reply{ return Tests::Ask( Client, move(request) ); }
		α Revision( uint16_t mid )ι->uint16_t{
			std::promise<uint16_t> settled;
			boost::asio::post( Ran, [&]{ settled.set_value( Client.Revision(mid) ); } );
			return settled.get_future().get();
		}
		α Got( uint16_t mid )Ι->vector<Controller::Received>{ return Of( Station.Frames(), mid ); }
		α Stop()ι->bool{ return exchange( _stopped, true ) || Tests::Stop( Client ); }
		Heard Told;
		Controller Station;
		Strand Ran;
		Session Client;
	private:
		bool _stopped{};
	};

	//A client that speaks bytes, on a context of its own.
	struct RawClient{
		RawClient( uint16_t port ): _socket{ _context }{ _socket.connect( {boost::asio::ip::address_v4::loopback(), port} ); }
		α Send( sv frame )->void{ boost::asio::write( _socket, boost::asio::buffer(frame) ); }
		//The next frame through its NUL, or nullopt where none comes in time.
		α Read( Duration wait=2s )->optional<string>{
			optional<string> y;
			boost::asio::async_read_until( _socket, boost::asio::dynamic_buffer(_buffer), '\0', [&]( const boost::system::error_code& ec, size_t size ){
				if( ec )
					return;
				y = _buffer.substr( 0, size );
				_buffer.erase( 0, size );
			} );
			_context.restart();
			_context.run_for( wait );
			if( !y ){
				_socket.cancel();
				_context.restart();
				_context.run();
			}
			return y;
		}
	private:
		boost::asio::io_context _context;
		boost::asio::ip::tcp::socket _socket;
		string _buffer;
	};
}