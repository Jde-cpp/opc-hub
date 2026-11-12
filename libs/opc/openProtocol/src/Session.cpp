#include <jde/opc/openProtocol/Session.h>
#include <boost/asio/connect.hpp>
#include <boost/asio/post.hpp>
#include <jde/opc/openProtocol/logTags.h>
#include "Fields.h"
#include "Wire.h"

#define let const auto

namespace Jde::Opc::OpenProtocol{
	using boost::system::error_code;
	using Tcp = boost::asio::ip::tcp;
	constexpr ELogTags _tags{ LogTag };

	//The MIDs a request's revision is the revision of, beside its own:  its reply, or what its subscription pushes.
	Ω asks( uint16_t request )ι->array<uint16_t,2>{
		switch( request ){
			case Mid0001::Id: return { Mid0002::Id };
			case Mid0014::Id: return { Mid0015::Id };
			case Mid0034::Id: return { Mid0035::Id };
			case Mid0051::Id: return { Mid0052::Id };
			case Mid0060::Id: return { Mid0061::Id };
			case Mid0064::Id: return { Mid0065::Id };
			case Mid0070::Id: return { Mid0071::Id, Mid0076::Id };
			case Mid0080::Id: return { Mid0081::Id };
			default: return {};
		}
	}
	//The MID a request is answered by, 0 for one MID 0005 answers.
	Ω replyOf( uint16_t request )ι->uint16_t{
		switch( request ){
			case Mid0001::Id: return Mid0002::Id;
			case Mid0064::Id: return Mid0065::Id;
			case Mid0080::Id: return Mid0081::Id;
			case Mid9999::Id: return Mid9999::Id;
			default: return 0;
		}
	}
	Ω named( sv name )ι->string{ return name.empty() ? string{} : Ƒ( ", {}", name ); }
	Ω millis( steady_clock::duration d )ι->_int{ return duration_cast<std::chrono::milliseconds>( d ).count(); }
	Ω socketFailure( const error_code& ec )ι->Failure{
		return { EFailure::Socket, 0, 0, ec==boost::asio::error::eof ? "The controller closed the connection." : ec.message() };
	}

	struct Session::Impl final : std::enable_shared_from_this<Impl>{
		Impl( Strand&& strand, string&& host, uint16_t port, Settings&& settings, Events&& events )ι;
		α Start()ι->void;
		α Enqueue( Body&& request, OnReply&& onReply )ι->void;
		α Stop( absl::AnyInvocable<void()>&& stopped )ι->void;
		α Finish()ι->void;
		α Revision( uint16_t mid )Ι->uint16_t;
		α Revisions()Ι->const flat_map<uint16_t,uint16_t>&{ return _revisions; }
		α Executor()Ι->const Strand&{ return _strand; }
		atomic<bool> Abandoned{};//~Session's:  nothing of the caller's is called from then on.
	private:
		enum class EState : uint8_t{ Idle, Connecting, Starting, Up, Waiting, Stopping, Stopped };
		//A request, the session's own where it has no OnReply:  MID 0001, 0003 and 9999.
		struct Pending{
			Body Request;
			uint16_t Mid;
			uint16_t Revision;//0 until it is sent, and a step down from then.
			Session::OnReply OnReply;
		};
		α Connect()ι->void;
		α Dial( sp<Wire> wire, vector<Tcp::endpoint> endpoints )ι->void;
		α Opened()ι->void;
		α OnFrame( string&& frame )ι->void;
		α OnLinkAnswer( const Header& head, const Expected<Message>& message )ι->void;
		α Dispatch( Message&& m )ι->void;
		α Answers( const Message& m )Ι->bool;//whether `m` is what the outstanding request waits for.
		α Answered( Message&& m )ι->void;
		α Started( Message&& m )ι->void;
		α Pump()ι->void;
		α Settle()ι->void;
		α OnIdle()ι->void;
		α Lose( Failure why )ι->void;//by value:  it resets _losing, which is what a caller hands it.
		α LoseAfter( string&& frame, Failure&& why )ι->void;//once `frame` is written.
		α Close()ι->void;
		α Fail( EFailure kind, sv why )ι->void;//every request the caller made.
		α FailQueued( EFailure kind, sv why )ι->void;//those not sent.
		α Answer( Pending& asked, Session::Reply&& reply )Ι->void;
		α Ceiling( uint16_t mid )Ι->uint16_t;

		const Strand _strand;
		const string _host;
		const uint16_t _port;
		const string _name;
		const Settings _settings;
		Events _events;
		EState _state{ EState::Idle };
		sp<Wire> _wire;
		Tcp::resolver _resolver;
		Link _link;
		Deadline _reply;
		Deadline _idle;
		Deadline _retry;
		Duration _backoff;
		std::deque<Pending> _queue;
		optional<Pending> _outstanding;
		optional<Failure> _losing;
		flat_map<uint16_t,uint16_t> _revisions;
		steady_clock::time_point _traffic;
		vector<absl::AnyInvocable<void()>> _stopped;
	};

	Session::Impl::Impl( Strand&& strand, string&& host, uint16_t port, Settings&& settings, Events&& events )ι:
		_strand{ move(strand) }, _host{ move(host) }, _port{ port }, _name{ Ƒ("{}:{}", _host, _port) }, _settings{ move(settings) }, _events{ move(events) },
		_resolver{ _strand }, _reply{ _strand }, _idle{ _strand }, _retry{ _strand }, _backoff{ _settings.ReconnectMin }
	{}

	α Session::Impl::Start()ι->void{
		if( _state==EState::Idle )
			Connect();
	}
	α Session::Impl::Connect()ι->void{
		_state = EState::Connecting;
		_wire = ms<Wire>( Tcp::socket{_strand}, _name, _settings.Log );
		_reply.Arm( _settings.ReplyTimeout, [self=shared_from_this()]{
			self->Lose( {EFailure::Timeout, 0, 0, Ƒ("No connection within {} ms.", millis(self->_settings.ReplyTimeout))} );
		} );
		error_code notAnAddress;
		if( let address = boost::asio::ip::make_address(_host, notAnAddress); !notAnAddress ){
			Dial( _wire, {Tcp::endpoint{address, _port}} );
			return;
		}
		_resolver.async_resolve( _host, std::to_string(_port), [self=shared_from_this(), wire=_wire]( const error_code& ec, const Tcp::resolver::results_type& found ){
			if( wire!=self->_wire )
				return;
			if( ec ){
				self->Lose( socketFailure(ec) );
				return;
			}
			vector<Tcp::endpoint> endpoints;
			for( let& entry : found )
				endpoints.push_back( entry.endpoint() );
			self->Dial( wire, move(endpoints) );
		} );
	}
	α Session::Impl::Dial( sp<Wire> wire, vector<Tcp::endpoint> endpoints )ι->void{
		boost::asio::async_connect( wire->Socket(), endpoints, [self=shared_from_this(), wire]( const error_code& ec, const Tcp::endpoint& ){
			if( wire!=self->_wire )
				return;
			if( ec )
				self->Lose( socketFailure(ec) );
			else
				self->Opened();
		} );
	}
	α Session::Impl::Opened()ι->void{
		error_code ignored;
		_wire->Socket().set_option( Tcp::no_delay{true}, ignored );//an acknowledgement goes when it is written.
		_wire->Open( {
			[self=shared_from_this()]( string&& frame ){ self->OnFrame( move(frame) ); },
			[self=shared_from_this()]( sv start ){ WARN( "({}) {} begins no frame:  dropped through the next NUL.", self->_name, Quote(start) ); },
			[self=shared_from_this()]( const error_code& ec ){ self->Lose( self->_losing ? move(*self->_losing) : socketFailure(ec) ); }
		} );
		_state = EState::Starting;
		_revisions.clear();
		_queue.push_back( {Mid0001{}, Mid0001::Id, 0, {}} );
		Pump();
	}

	α Session::Impl::OnFrame( string&& frame )ι->void{
		_traffic = steady_clock::now();
		if( _losing )
			return;
		let head = DecodeHeader( frame );
		if( !head ){
			WARN( "({}) refused a frame:  {}", _name, head.error().What );
			return;
		}
		let& h = head->Head;
		auto message = Decode( frame );
		if( h.Mid==Mid9997::Id || h.Mid==Mid9998::Id ){
			OnLinkAnswer( h, message );
			return;
		}
		switch( _link.Receive(h.Sequence) ){
		using enum Link::EReceived;
		case Unnumbered:
			break;
		case Next:
			_wire->Send( Acknowledge(_link, h.Mid) );
			break;
		case Repeat:
			DBG( "({}) MID {:04} again under sequence number {}:  acknowledged again and dropped.", _name, h.Mid, h.Sequence );
			_wire->Send( string{_link.LastAck} );
			return;
		case OutOfOrder:
			LoseAfter( Reject(_link, h.Mid, 3), {EFailure::Sequence, h.Mid, 3, Ƒ("MID {:04} came under sequence number {} where {} was expected.", h.Mid, h.Sequence, _link.Expected())} );
			return;
		}
		if( message ){
			Dispatch( move(*message) );
			return;
		}
		WARN( "({}) refused a frame:  {}", _name, message.error().What );
		if( let topic = TopicOf(h.Mid); topic && (!_link.Numbered || !h.Sequence) )//received, as its header says:  the controller sends no more until it is.
			_wire->Send( Frame({}, Empty(topic->Ack)) );
	}
	α Session::Impl::OnLinkAnswer( const Header& head, const Expected<Message>& message )ι->void{
		if( head.Mid==Mid9997::Id ){
			if( _link.Acknowledged(head) ){
				Settle();
				Pump();
			}else
				WARN( "({}) MID 9997 under sequence number {} acknowledges nothing sent:  dropped.", _name, head.Sequence );
			return;
		}
		let rejected = message ? get_if<Mid9998>( &message->Data ) : nullptr;
		const uint16_t mid = rejected ? rejected->Mid.value_or( 0 ) : 0;
		const uint16_t code = rejected ? rejected->Code.value_or( 0 ) : 0;
		Lose( {EFailure::Sequence, mid, code, Ƒ("MID 9998:  code {}{}, sequence number {} expected.", code, named(LinkErrorName(code)), head.Sequence)} );
	}
	α Session::Impl::Dispatch( Message&& m )ι->void{
		let mid = m.Head.Mid;
		if( _outstanding && Answers(m) ){
			Answered( move(m) );
			return;
		}
		let topic = TopicOf( mid );
		if( !topic ){
			WARN( "({}) MID {:04} answers nothing asked and is no subscription's:  dropped.", _name, mid );
			return;
		}
		if( !_link.Numbered || !m.Head.Sequence )//what MID 9997 has not acknowledged:  a number on an unnumbered link is no link level.
			_wire->Send( Frame({}, Empty(topic->Ack)) );
		if( mid!=Mid0074::Id )//whose revisions are its own, not the subscription's.
			_revisions[topic->Subscribe] = m.Head.Revision;
		if( _state==EState::Up && _events.Push && !Abandoned )
			_events.Push( move(m) );
	}
	α Session::Impl::Answers( const Message& m )Ι->bool{
		let asked = _outstanding->Mid;
		if( let refused = get_if<Mid0004>(&m.Data) )
			return !refused->Mid || *refused->Mid==asked;
		let reply = replyOf( asked );
		if( let accepted = get_if<Mid0005>(&m.Data) )
			return !reply && ( !accepted->Mid || *accepted->Mid==asked );
		return reply==m.Head.Mid;
	}
	α Session::Impl::Answered( Message&& m )ι->void{
		auto asked = move( *_outstanding );
		_outstanding.reset();
		Settle();
		if( let refused = get_if<Mid0004>(&m.Data) ){
			let code = refused->Code.value_or( 0 );
			if( (code==74 || code==76 || code==97) && asked.Revision>1 ){
				--asked.Revision;
				_queue.push_front( move(asked) );
			}
			else if( asked.Mid==Mid0003::Id ){
				Finish();
				return;
			}
			else{
				Failure failure{ EFailure::Refused, asked.Mid, code, Ƒ("MID 0004 to MID {:04}:  code {}{}.", asked.Mid, code, named(ErrorName(code))) };
				if( asked.Mid==Mid0001::Id ){
					Lose( move(failure) );
					return;
				}
				Answer( asked, std::unexpected{move(failure)} );
			}
		}
		else if( asked.Mid==Mid0003::Id ){
			Finish();
			return;
		}
		else if( asked.Mid!=Mid9999::Id ){
			_revisions[asked.Mid] = replyOf( asked.Mid ) ? m.Head.Revision : asked.Revision;
			if( asked.Mid==Mid0001::Id )
				Started( move(m) );
			else
				Answer( asked, move(m) );
		}
		Pump();
	}
	α Session::Impl::Started( Message&& m )ι->void{
		auto& identity = get<Mid0002>( m.Data );
		let numbered = m.Head.Revision>=6 && identity.SequenceNumberSupport==1;
		_link.Reset( numbered );
		_state = EState::Up;
		_backoff = _settings.ReconnectMin;
		_traffic = steady_clock::now();
		_idle.Arm( _settings.KeepAlive, [self=shared_from_this()]{ self->OnIdle(); } );
		DBG( "({}) up:  MID 0002 at revision {}, acknowledged at {} level.", _name, m.Head.Revision, numbered ? "link" : "application" );
		if( _events.Connected && !Abandoned )
			_events.Connected( {move(identity), m.Head.Revision, numbered} );
	}

	α Session::Impl::Enqueue( Body&& request, OnReply&& onReply )ι->void{
		let mid = MidOf( request );
		Pending asked{ move(request), mid, 0, std::move(onReply) };
		if( _state==EState::Up ){
			_queue.push_back( move(asked) );
			Pump();
		}
		else if( _state==EState::Stopping || _state==EState::Stopped )
			Answer( asked, std::unexpected{Failure{EFailure::Stopped, asked.Mid, 0, "The session is stopped."}} );
		else
			Answer( asked, std::unexpected{Failure{EFailure::NotConnected, asked.Mid, 0, "The link is down."}} );
	}
	//One request out at a time, and at link level none until the last has its MID 9997.
	α Session::Impl::Pump()ι->void{
		while( _wire && !_losing && !_outstanding && !_link.Awaiting() && !_queue.empty() ){
			auto next = move( _queue.front() );
			_queue.pop_front();
			if( !next.Revision )
				next.Revision = Revision( next.Mid );
			auto frame = Encode( {.Revision=next.Revision, .Sequence=_link.Number()}, next.Request );
			if( !frame ){
				Answer( next, std::unexpected{Failure{EFailure::Value, next.Mid, 0, move(frame.error().What)}} );
				continue;
			}
			_link.Sent( next.Mid );
			_outstanding = move( next );
			_wire->Send( move(*frame) );
			_traffic = steady_clock::now();
			_reply.Arm( _settings.ReplyTimeout, [self=shared_from_this()]{
				let waiting = self->_outstanding ? self->_outstanding->Mid : self->_link.Awaiting().value_or( 0 );
				self->Lose( {EFailure::Timeout, waiting, 0, Ƒ("MID {:04} had no {} within {} ms.", waiting, self->_outstanding ? "answer" : "MID 9997", millis(self->_settings.ReplyTimeout))} );
			} );
		}
	}
	α Session::Impl::Settle()ι->void{
		if( !_outstanding && !_link.Awaiting() )
			_reply.Disarm();
	}
	α Session::Impl::OnIdle()ι->void{
		if( _state!=EState::Up )
			return;
		let quiet = steady_clock::now()-_traffic;
		let keepAlive = duration_cast<steady_clock::duration>( _settings.KeepAlive );
		if( quiet>=keepAlive && !_outstanding && _queue.empty() ){
			_queue.push_back( {Mid9999{}, Mid9999::Id, 0, {}} );
			Pump();
		}
		_idle.Arm( quiet<keepAlive ? keepAlive-quiet : keepAlive, [self=shared_from_this()]{ self->OnIdle(); } );
	}

	α Session::Impl::Lose( Failure why )ι->void{
		if( _state==EState::Stopping ){
			Finish();
			return;
		}
		if( _state!=EState::Connecting && _state!=EState::Starting && _state!=EState::Up )
			return;
		Close();
		_state = EState::Waiting;
		let wait = _backoff;
		_backoff = std::min( _backoff*2, std::max(_settings.ReconnectMax, _settings.ReconnectMin) );
		DBG( "({}) down, the next attempt in {} ms:  {}", _name, millis(wait), why.What );
		_retry.Arm( wait, [self=shared_from_this()]{ self->Connect(); } );
		if( auto outstanding = exchange(_outstanding, nullopt) )
			Answer( *outstanding, std::unexpected{why} );
		Fail( EFailure::NotConnected, Ƒ("The link went first:  {}", why.What) );
		if( _events.Lost && !Abandoned )
			_events.Lost( why );
	}
	α Session::Impl::LoseAfter( string&& frame, Failure&& why )ι->void{
		_losing = move( why );
		_wire->Send( move(frame), [self=shared_from_this()]{ self->Lose( move(*self->_losing) ); } );
	}
	α Session::Impl::Close()ι->void{
		if( auto wire = exchange(_wire, nullptr) )
			wire->Close();
		_resolver.cancel();
		_reply.Disarm();
		_idle.Disarm();
		_retry.Disarm();
		_link.Reset( false );
		_losing.reset();
	}
	α Session::Impl::Fail( EFailure kind, sv why )ι->void{
		if( auto outstanding = exchange(_outstanding, nullopt) )
			Answer( *outstanding, std::unexpected{Failure{kind, outstanding->Mid, 0, string{why}}} );
		FailQueued( kind, why );
	}
	α Session::Impl::FailQueued( EFailure kind, sv why )ι->void{
		for( auto& queued : exchange(_queue, {}) )
			Answer( queued, std::unexpected{Failure{kind, queued.Mid, 0, string{why}}} );
	}
	α Session::Impl::Answer( Pending& asked, Session::Reply&& reply )Ι->void{
		if( asked.OnReply && !Abandoned )
			asked.OnReply( move(reply) );
	}

	α Session::Impl::Stop( absl::AnyInvocable<void()>&& stopped )ι->void{
		if( stopped )
			_stopped.push_back( std::move(stopped) );
		if( _state==EState::Stopping )
			return;
		if( _state!=EState::Up ){
			Finish();
			return;
		}
		_state = EState::Stopping;
		_idle.Disarm();
		//The request out keeps its answer, which the controller owes before it takes MID 0003 (§3.2.1);  the rest is not sent.
		FailQueued( EFailure::Stopped, "The session is stopped." );
		//Finish at replyTimeout without the request out's answer;  MID 0003's send arms it again for its own.
		_reply.Arm( _settings.ReplyTimeout, [self=shared_from_this()]{ self->Finish(); } );
		_queue.push_back( {Mid0003{}, Mid0003::Id, 0, {}} );
		Pump();
	}
	α Session::Impl::Finish()ι->void{
		Close();
		_state = EState::Stopped;
		Fail( EFailure::Stopped, "The session is stopped." );
		_events = {};
		for( auto& stopped : std::exchange(_stopped, {}) ){
			if( !Abandoned )
				stopped();
		}
	}

	α Session::Impl::Ceiling( uint16_t mid )Ι->uint16_t{
		auto y = std::max<uint16_t>( MaxRevision(mid), 1 );
		let bound = [&]( uint16_t named ){
			if( let ceiling = _settings.Revisions.find(named); ceiling!=_settings.Revisions.end() && ceiling->second )
				y = std::min( y, ceiling->second );
		};
		bound( mid );
		for( let data : asks(mid) ){
			if( data )
				bound( data );
		}
		return y;
	}
	α Session::Impl::Revision( uint16_t mid )Ι->uint16_t{
		let settled = _revisions.find( mid );
		return settled==_revisions.end() ? Ceiling( mid ) : settled->second;
	}

	Session::Session( Strand strand, string host, uint16_t port, Settings settings, Events events )ι:
		_impl{ ms<Impl>(move(strand), move(host), port, move(settings), move(events)) }
	{}
	Session::~Session(){
		_impl->Abandoned = true;
		boost::asio::post( _impl->Executor(), [impl=_impl]{ impl->Finish(); } );
	}
	α Session::Start()ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl]{ impl->Start(); } );
	}
	α Session::Request( Body request, OnReply onReply )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, request=move(request), onReply=std::move(onReply)]()mutable{ impl->Enqueue( move(request), std::move(onReply) ); } );
	}
	α Session::Stop( absl::AnyInvocable<void()> stopped )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, stopped=std::move(stopped)]()mutable{ impl->Stop( std::move(stopped) ); } );
	}
	α Session::Revision( uint16_t mid )Ι->uint16_t{
		ASSERT( _impl->Executor().running_in_this_thread() );
		return _impl->Revision( mid );
	}
	α Session::Revisions()Ι->flat_map<uint16_t,uint16_t>{
		ASSERT( _impl->Executor().running_in_this_thread() );
		return _impl->Revisions();
	}
}