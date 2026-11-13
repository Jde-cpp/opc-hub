#include <jde/opc/openProtocol/Controller.h>
#include <future>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <jde/opc/openProtocol/logTags.h>
#include "Wire.h"

#define let const auto

namespace Jde::Opc::OpenProtocol{
	using boost::system::error_code;
	using Tcp = boost::asio::ip::tcp;
	constexpr ELogTags _tags{ LogTag };

	//Table 23's code for a revision past the controller's, by the MID's kind.
	Ω unsupported( uint16_t mid )ι->uint16_t{
		if( find_if(Topics, [=]( const Topic& t ){ return t.Subscribe==mid; })!=Topics.end() )
			return 74;
		return mid==Mid0064::Id || mid==Mid0080::Id ? 76 : 97;
	}
	//A result as MID 0065 has it:  no limits, no parameter set name and no time of its last change, and at revision 7 its station.
	Ω old( const Mid0061& r, uint32_t stationId, const string& stationName )ι->Mid0065{
		return {
			.TighteningId=r.TighteningId, .Vin=r.Vin, .JobId=r.JobId, .Pset=r.Pset, .Strategy=r.Strategy, .StrategyOptions=r.StrategyOptions,
			.BatchSize=r.BatchSize, .BatchCounter=r.BatchCounter, .Status=r.Status, .BatchStatus=r.BatchStatus, .TorqueStatus=r.TorqueStatus,
			.AngleStatus=r.AngleStatus, .RundownAngleStatus=r.RundownAngleStatus, .CurrentMonitoringStatus=r.CurrentMonitoringStatus,
			.SelfTapStatus=r.SelfTapStatus, .PrevailTorqueMonitoringStatus=r.PrevailTorqueMonitoringStatus,
			.PrevailTorqueCompensateStatus=r.PrevailTorqueCompensateStatus, .TighteningErrorStatus=r.TighteningErrorStatus, .Torque=r.Torque,
			.Angle=r.Angle, .RundownAngle=r.RundownAngle, .CurrentMonitoringValue=r.CurrentMonitoringValue, .SelfTapTorque=r.SelfTapTorque,
			.PrevailTorque=r.PrevailTorque, .JobSequenceNumber=r.JobSequenceNumber, .SyncTighteningId=r.SyncTighteningId,
			.ToolSerialNumber=r.ToolSerialNumber, .Time=r.Time, .TorqueUnit=r.TorqueUnit, .ResultType=r.ResultType,
			.IdentifierPart2=r.IdentifierPart2, .IdentifierPart3=r.IdentifierPart3, .IdentifierPart4=r.IdentifierPart4,
			.CustomerErrorCode=r.CustomerErrorCode, .PrevailTorqueCompensateValue=r.PrevailTorqueCompensateValue,
			.TighteningErrorStatus2=r.TighteningErrorStatus2, .StationId=stationId, .StationName=stationName
		};
	}
	Ω localNow( const std::chrono::time_zone* zone, Duration skew )ι->LocalTime{
		let now = Clock::now()+skew;
		try{
			return std::chrono::floor<std::chrono::seconds>( (zone ? zone : std::chrono::current_zone())->to_local(now) );
		}
		catch( const std::exception& ){//no time zone database:  UTC.
			return LocalTime{ std::chrono::floor<std::chrono::seconds>(now.time_since_epoch()) };
		}
	}

	struct Controller::Impl final : std::enable_shared_from_this<Impl>{
		using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;
		Impl( boost::asio::io_context& context, uint16_t port, Script&& script )ε;
		α Executor()Ι->const Strand&{ return _strand; }
		α Port()Ι->uint16_t{ return _port; }
		α Listen()ι->void;
		α Close()ι->void;
		α Edit( const function<void( Script& )>& edit )ι->void{ edit( _script ); }
		α Rundown( Mid0061&& result, bool here, uint32_t stationId, string&& stationName )ι->void;
		Τ α Select( optional<T>& kept, T&& selected )ι->void{ kept = move( selected ); PushAll( *kept ); }
		α Raise( Mid0071&& alarm )ι->void{ Select( Alarm, move(alarm) ); }
		α AcknowledgeAlarm()ι->void;
		α Drop()ι->void;
		α Silence( bool silent )ι->void{ _silent = silent; }
		α Delay( Duration by, uint replies )ι->void{ _delay = replies ? optional<Held>{ Held{by, replies} } : nullopt; }
		α SendRaw( const string& bytes )ι->void;
		α Retransmit()ι->void;
		α SkipSequence()ι->void;
		α Frames()Ι->vector<Received>{ rl _{ _mutex }; return _frames; }

		optional<Mid0015> Pset;
		optional<Mid0035> Job;
		optional<Mid0052> Vin;
		optional<Mid0071> Alarm;
		atomic<uint> Connected{};
		atomic<bool> ToolLockAsked{};
		atomic<bool> ToolEnabled{ true };
	private:
		struct Client{
			Client( uint id, sp<Wire> channel )ι: Id{ id }, Channel{ move(channel) }, Quiet{ Channel->Socket().get_executor() }{}
			struct Subscription{ uint16_t Revision; bool NoAck; };
			struct Out{ Body Data; uint16_t Revision; uint16_t Ack; };//Ack:  the MID that acknowledges it at application level, 0 where none does.
			const uint Id;
			const sp<Wire> Channel;
			Link Sequence;
			bool Started{};
			flat_map<uint16_t,Subscription> Subscriptions;//by the MID that subscribes.
			std::deque<Out> Queue;//behind the message that waits for its acknowledgement.
			optional<uint16_t> Awaiting;//the MID the message out waits for at application level.
			string LastFrame;
			Deadline Quiet;
		};
		struct Stored{ uint32_t StationId; string StationName; Mid0061 Result; };

		α Opened( Tcp::socket&& socket )ι->void;
		α Find( uint id )ι->Client*;
		α Forget( uint id )ι->void;
		α Touch( Client& c )ι->void;
		α OnFrame( Client& c, string&& frame )ι->void;
		α Answers( Client& c, const Header& h )ι->optional<uint16_t>;//the revision a request is answered at, none where it was refused.
		α Commanded( Client& c, uint16_t mid )ι->bool;
		α Subscribe( Client& c, const Header& h, uint16_t exists )ι->void;
		α Unsubscribe( Client& c, const Header& h, uint16_t subscribe, uint16_t missing )ι->void;
		α Old( uint32_t id )Ι->const Stored*;

		α On( Client& c, const Header& h, const Mid0001& )ι->void;
		α On( Client& c, const Header& h, const Mid0003& )ι->void;
		α On( Client& c, const Header& h, const Mid0014& )ι->void{ Subscribe( c, h, 13 ); }
		α On( Client& c, const Header& h, const Mid0017& )ι->void{ Unsubscribe( c, h, Mid0014::Id, 14 ); }
		α On( Client& c, const Header& h, const Mid0018& m )ι->void;
		α On( Client& c, const Header& h, const Mid0034& )ι->void{ Subscribe( c, h, 18 ); }
		α On( Client& c, const Header& h, const Mid0037& )ι->void{ Unsubscribe( c, h, Mid0034::Id, 19 ); }
		α On( Client& c, const Header& h, const Mid0038& m )ι->void;
		α On( Client& c, const Header& h, const Mid0042& )ι->void;
		α On( Client& c, const Header& h, const Mid0043& )ι->void;
		α On( Client& c, const Header& h, const Mid0050& m )ι->void;
		α On( Client& c, const Header& h, const Mid0051& )ι->void{ Subscribe( c, h, 6 ); }
		α On( Client& c, const Header& h, const Mid0054& )ι->void{ Unsubscribe( c, h, Mid0051::Id, 7 ); }
		α On( Client& c, const Header& h, const Mid0060& )ι->void{ Subscribe( c, h, 9 ); }
		α On( Client& c, const Header& h, const Mid0063& )ι->void{ Unsubscribe( c, h, Mid0060::Id, 10 ); }
		α On( Client& c, const Header& h, const Mid0064& m )ι->void;
		α On( Client& c, const Header& h, const Mid0070& )ι->void{ Subscribe( c, h, 11 ); }
		α On( Client& c, const Header& h, const Mid0073& )ι->void{ Unsubscribe( c, h, Mid0070::Id, 12 ); }
		α On( Client& c, const Header&, const Mid0080& )ι->void{ Reply( c, Mid0081{.Time=localNow(_script.Zone, _script.Skew)} ); }
		α On( Client& c, const Header&, const Mid9999& )ι->void{ Reply( c, Mid9999{} ); }
		α On( Client& c, const Header& h, const auto& )ι->void{ Refuse( c, h.Mid, 99 ); }//a MID a controller sends, not one it takes.

		α Accept( Client& c, uint16_t mid )ι->void{ Reply( c, Mid0005{.Mid=mid} ); }
		α Refuse( Client& c, uint16_t mid, uint16_t code )ι->void{ Reply( c, Mid0004{.Mid=mid, .Code=code}, code>99 ? 2 : 1 ); }
		α Reply( Client& c, Body data, uint16_t revision=1 )ι->void;//held where a delay is asked.
		α Push( Client& c, const Body& data )ι->void;//where the client subscribes to it, at the revision it subscribed at.
		α PushAll( const Body& data )ι->void;
		//At once where nothing acknowledges it, else behind the message that waits:  one out at a time (§3.2.1).
		α Send( Client& c, Body data, uint16_t revision, uint16_t ack )ι->void;
		α Flush( Client& c )ι->void;
		α Write( Client& c, const Body& data, uint16_t revision )ι->bool;

		const Strand _strand;
		Tcp::acceptor _acceptor;
		uint16_t _port{};
		Script _script;
		vector<up<Client>> _clients;
		uint _lastClient{};
		std::map<uint32_t,Stored> _results;
		uint32_t _nextId{ 1 };
		bool _silent{};
		struct Held{ Duration By; uint Replies; };
		optional<Held> _delay;
		vector<sp<boost::asio::steady_timer>> _held;//the delayed replies' timers.
		mutable absl::Mutex _mutex;
		vector<Received> _frames ABSL_GUARDED_BY(_mutex);
	};

	Controller::Impl::Impl( boost::asio::io_context& context, uint16_t port, Script&& script )ε:
		_strand{ boost::asio::make_strand(context) }, _acceptor{ _strand }, _script{ move(script) }
	{
		let endpoint = Tcp::endpoint{ boost::asio::ip::address_v4::loopback(), port };
		error_code ec;
		_acceptor.open( endpoint.protocol(), ec );
		if( !ec ){
#ifdef _WIN32
			_acceptor.set_option( boost::asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>{true}, ec );//SO_REUSEADDR would share a port that is listening.
#else
			_acceptor.set_option( boost::asio::socket_base::reuse_address{true}, ec );
#endif
		}
		if( !ec )
			_acceptor.bind( endpoint, ec );
		if( !ec )
			_acceptor.listen( boost::asio::socket_base::max_listen_connections, ec );
		if( !ec )
			_port = _acceptor.local_endpoint( ec ).port();
		THROW_IF( ec, "The controller cannot listen on 127.0.0.1:{}:  {}", port, ec.message() );
	}
	α Controller::Impl::Listen()ι->void{
		_acceptor.async_accept( [self=shared_from_this()]( const error_code& ec, Tcp::socket socket ){
			if( ec )
				return;//closed.
			self->Opened( move(socket) );
			self->Listen();
		} );
	}
	α Controller::Impl::Opened( Tcp::socket&& socket )ι->void{
		error_code ignored;
		socket.set_option( Tcp::no_delay{true}, ignored );
		let id = ++_lastClient;
		auto channel = ms<Wire>( move(socket), Ƒ("controller {} client {}", _port, id), _script.Log );
		auto& client = *_clients.emplace_back( mu<Client>(id, channel) );
		++Connected;
		channel->Open( {
			[self=shared_from_this(), id]( string&& frame ){
				if( let c = self->Find(id) )
					self->OnFrame( *c, move(frame) );
			},
			[]( sv ){},
			[self=shared_from_this(), id]( const error_code& ){ self->Forget( id ); }
		} );
		Touch( client );
	}
	α Controller::Impl::Find( uint id )ι->Client*{
		let client = find_if( _clients, [=]( let& c ){ return c->Id==id; } );
		return client==_clients.end() ? nullptr : client->get();
	}
	α Controller::Impl::Forget( uint id )ι->void{
		let client = find_if( _clients, [=]( let& c ){ return c->Id==id; } );
		if( client==_clients.end() )
			return;
		( *client )->Channel->Close();
		_clients.erase( client );
		--Connected;
	}
	α Controller::Impl::Drop()ι->void{
		for( let& client : _clients )
			client->Channel->Close();
		Connected -= _clients.size();
		_clients.clear();
	}
	α Controller::Impl::Close()ι->void{
		error_code ignored;
		_acceptor.close( ignored );
		Drop();
		for( let& timer : exchange(_held, {}) )
			timer->cancel();
	}
	//A controller drops a client it has exchanged nothing with for its timeout.
	α Controller::Impl::Touch( Client& c )ι->void{
		if( _script.DropSilent )
			c.Quiet.Arm( *_script.DropSilent, [self=shared_from_this(), id=c.Id]{ self->Forget( id ); } );
	}

	α Controller::Impl::OnFrame( Client& c, string&& frame )ι->void{
		let head = DecodeHeader( frame );
		{
			ul _{ _mutex };
			_frames.push_back( {c.Id, head ? head->Head : Header{}, frame, steady_clock::now()} );
		}
		Touch( c );
		if( _silent || !head )
			return;
		let& h = head->Head;
		if( h.Mid==Mid9997::Id ){
			if( c.Sequence.Acknowledged(h) )
				Flush( c );
			return;
		}
		if( h.Mid==Mid9998::Id )
			return;//the integrator's to reconnect.
		switch( c.Sequence.Receive(h.Sequence) ){
		using enum Link::EReceived;
		case Unnumbered:
			break;
		case Next:
			c.Channel->Send( Acknowledge(c.Sequence, h.Mid) );
			break;
		case Repeat:
			c.Channel->Send( string{c.Sequence.LastAck} );
			return;
		case OutOfOrder:
			c.Channel->Send( Reject(c.Sequence, h.Mid, 3) );
			return;
		}
		if( find_if(Topics, [&]( const Topic& t ){ return t.Ack==h.Mid; })!=Topics.end() ){
			if( c.Awaiting==h.Mid ){
				c.Awaiting.reset();
				Flush( c );
			}
			return;
		}
		if( !c.Started && h.Mid!=Mid0001::Id )
			return;//it answers nothing before MID 0001 (§5.2.1), a frame it cannot read among it.
		let message = Decode( frame );
		if( !message ){
			//MID 0001 revision 8's parameter 02, the tool lock at disconnection (Table 12), which no revision the codec reads carries.
			if( let data = sv{ frame }.substr( HeaderSize ); h.Mid==Mid0001::Id && h.Revision>=8 && data.size()>=6 && data.substr(3, 3)=="021" )
				ToolLockAsked = true;
			let refusal = message.error().Code;
			Refuse( c, h.Mid, refusal==ERefusal::UnknownMid ? 99 : refusal==ERefusal::UnknownRevision ? unsupported(h.Mid) : 1 );
			return;
		}
		if( let refused = _script.Refuses.find(h.Mid); refused!=_script.Refuses.end() ){
			Refuse( c, h.Mid, refused->second );
			return;
		}
		std::visit( [&]( const auto& m ){ On( c, h, m ); }, message->Data );
	}
	α Controller::Impl::Answers( Client& c, const Header& h )ι->optional<uint16_t>{
		let named = _script.Revisions.find( h.Mid );
		let own = named==_script.Revisions.end() ? MaxRevision( h.Mid ) : named->second;
		if( h.Revision<=own )
			return h.Revision;
		if( _script.Lowers.contains(h.Mid) )
			return own;
		Refuse( c, h.Mid, unsupported(h.Mid) );
		return nullopt;
	}
	α Controller::Impl::Commanded( Client& c, uint16_t mid )ι->bool{
		if( _script.Viewer )
			Refuse( c, mid, 92 );
		return !_script.Viewer;
	}
	α Controller::Impl::Subscribe( Client& c, const Header& h, uint16_t exists )ι->void{
		if( c.Subscriptions.contains(h.Mid) ){
			Refuse( c, h.Mid, exists );
			return;
		}
		let revision = Answers( c, h );
		if( !revision )
			return;
		c.Subscriptions.emplace( h.Mid, Client::Subscription{*revision, h.NoAck} );
		Accept( c, h.Mid );
		if( !_script.PushOnSubscribe )
			return;
		if( h.Mid==Mid0014::Id && Pset )
			Push( c, *Pset );
		else if( h.Mid==Mid0034::Id && Job )
			Push( c, *Job );
		else if( h.Mid==Mid0051::Id && Vin )
			Push( c, *Vin );
		else if( h.Mid==Mid0070::Id && Alarm )
			Push( c, Mid0076{.AlarmStatus=1, .ErrorCode=Alarm->ErrorCode, .ControllerReady=Alarm->ControllerReady, .ToolReady=Alarm->ToolReady, .Time=Alarm->Time, .ToolHealth=Alarm->ToolHealth} );
	}
	α Controller::Impl::Unsubscribe( Client& c, const Header& h, uint16_t subscribe, uint16_t missing )ι->void{
		if( c.Subscriptions.erase(subscribe) )
			Accept( c, h.Mid );
		else
			Refuse( c, h.Mid, missing );
	}
	α Controller::Impl::On( Client& c, const Header& h, const Mid0001& )ι->void{
		if( c.Started ){
			Refuse( c, h.Mid, 96 );
			return;
		}
		if( _script.OneActor && find_if(_clients, []( let& other ){ return other->Started; })!=_clients.end() ){
			Refuse( c, h.Mid, 35 );
			return;
		}
		if( let revision = Answers(c, h) ){
			c.Started = true;
			Reply( c, _script.Identity, *revision );
		}
	}
	α Controller::Impl::On( Client& c, const Header& h, const Mid0003& )ι->void{
		c.Started = false;
		c.Subscriptions.clear();
		c.Queue.clear();
		c.Awaiting.reset();
		Accept( c, h.Mid );
	}
	α Controller::Impl::On( Client& c, const Header& h, const Mid0018& m )ι->void{
		if( !Commanded(c, h.Mid) )
			return;
		if( !Pset )
			Pset.emplace();
		Pset->Pset = m.Pset;
		Accept( c, h.Mid );
		PushAll( *Pset );
	}
	α Controller::Impl::On( Client& c, const Header& h, const Mid0038& m )ι->void{
		if( !Commanded(c, h.Mid) || !Answers(c, h) )
			return;
		if( !Job )
			Job.emplace();
		Job->JobId = m.JobId;
		Accept( c, h.Mid );
		PushAll( *Job );
	}
	α Controller::Impl::On( Client& c, const Header& h, const Mid0042& )ι->void{
		if( Commanded(c, h.Mid) ){
			ToolEnabled = false;
			Accept( c, h.Mid );
		}
	}
	α Controller::Impl::On( Client& c, const Header& h, const Mid0043& )ι->void{
		if( Commanded(c, h.Mid) ){
			ToolEnabled = true;
			Accept( c, h.Mid );
		}
	}
	α Controller::Impl::On( Client& c, const Header& h, const Mid0050& m )ι->void{
		if( !Commanded(c, h.Mid) )
			return;
		if( !Vin )
			Vin.emplace();
		Vin->Vin = m.Vin;
		Accept( c, h.Mid );
		PushAll( *Vin );
	}
	α Controller::Impl::On( Client& c, const Header& h, const Mid0064& m )ι->void{
		let revision = Answers( c, h );
		if( !revision )
			return;
		if( let found = Old(m.TighteningId.value_or(0)) )
			Reply( c, old(found->Result, found->StationId, found->StationName), *revision );
		else
			Refuse( c, h.Mid, 15 );
	}

	α Controller::Impl::Rundown( Mid0061&& result, bool here, uint32_t stationId, string&& stationName )ι->void{
		if( !result.TighteningId )
			result.TighteningId = _nextId;
		_nextId = std::max( _nextId, *result.TighteningId+1 );
		if( here )
			PushAll( result );
		let id = *result.TighteningId;
		_results.insert_or_assign( id, here
			? Stored{_script.Identity.StationId.value_or(0), _script.Identity.StationName, move(result)}
			: Stored{stationId, move(stationName), move(result)} );
	}
	//The result with `id`, or the last before it where there is none, as the PF6000 appendix's walk asks its way back
	//through ids that are not consecutive (§2.4);  0 is the latest (Table 121).
	α Controller::Impl::Old( uint32_t id )Ι->const Stored*{
		if( !id )
			return _results.empty() ? nullptr : &_results.rbegin()->second;
		let after = _results.upper_bound( id );
		return after==_results.begin() ? nullptr : &std::prev( after )->second;
	}
	α Controller::Impl::AcknowledgeAlarm()ι->void{
		if( let alarm = exchange(Alarm, nullopt) )
			PushAll( Mid0074{.ErrorCode=alarm->ErrorCode} );
	}
	α Controller::Impl::SendRaw( const string& bytes )ι->void{
		for( let& client : _clients )
			client->Channel->Send( string{bytes} );
	}
	α Controller::Impl::Retransmit()ι->void{
		for( let& client : _clients ){
			if( !client->LastFrame.empty() )
				client->Channel->Send( string{client->LastFrame} );
		}
	}
	α Controller::Impl::SkipSequence()ι->void{
		for( let& client : _clients )
			client->Sequence.Skip();
	}

	α Controller::Impl::Reply( Client& c, Body data, uint16_t revision )ι->void{
		if( !_delay ){
			Send( c, move(data), revision, 0 );
			return;
		}
		let delay = _delay->By;
		if( !--_delay->Replies )
			_delay.reset();
		auto timer = _held.emplace_back( ms<boost::asio::steady_timer>(_strand, duration_cast<steady_clock::duration>(delay)) );
		timer->async_wait( [self=shared_from_this(), timer, id=c.Id, data=move(data), revision]( const error_code& ec )mutable{
			std::erase( self->_held, timer );
			if( let client = self->Find(id); client && !ec )
				self->Send( *client, move(data), revision, 0 );
		} );
	}
	α Controller::Impl::Push( Client& c, const Body& data )ι->void{
		let mid = MidOf( data );
		let topic = TopicOf( mid );
		let subscription = topic ? c.Subscriptions.find( topic->Subscribe ) : c.Subscriptions.end();
		if( _silent || subscription==c.Subscriptions.end() )
			return;
		//MID 0074's revisions are its own.
		let revision = mid==Mid0074::Id ? std::min( subscription->second.Revision, Mid0074::MaxRevision ) : subscription->second.Revision;
		Send( c, data, revision, subscription->second.NoAck ? 0 : topic->Ack );
	}
	α Controller::Impl::PushAll( const Body& data )ι->void{
		for( let& client : _clients )
			Push( *client, data );
	}
	α Controller::Impl::Send( Client& c, Body data, uint16_t revision, uint16_t ack )ι->void{
		if( !c.Sequence.Numbered && !ack ){
			Write( c, data, revision );
			return;
		}
		c.Queue.push_back( {move(data), revision, ack} );
		Flush( c );
	}
	α Controller::Impl::Flush( Client& c )ι->void{
		while( !c.Awaiting && !c.Sequence.Awaiting() && !c.Queue.empty() ){
			let out = move( c.Queue.front() );
			c.Queue.pop_front();
			if( Write(c, out.Data, out.Revision) && !c.Sequence.Numbered && out.Ack )
				c.Awaiting = out.Ack;
		}
	}
	α Controller::Impl::Write( Client& c, const Body& data, uint16_t revision )ι->bool{
		auto frame = Encode( {.Revision=revision, .Sequence=c.Sequence.Number()}, data );
		if( !frame ){
			ERR( "The controller on {} cannot send:  {}", _port, frame.error().What );
			return false;
		}
		c.Sequence.Sent( MidOf(data) );
		c.LastFrame = *frame;
		c.Channel->Send( move(*frame) );
		Touch( c );
		//MID 0002 opens the numbering (§2.2.5):  from 1 each way, where it goes at revision 6 and says so.
		if( let identity = get_if<Mid0002>(&data) )
			c.Sequence.Reset( revision>=6 && identity->SequenceNumberSupport==1 );
		return true;
	}

	Controller::Controller( boost::asio::io_context& context, uint16_t port, Script script )ε:
		_impl{ ms<Impl>(context, port, move(script)) }
	{
		boost::asio::post( _impl->Executor(), [impl=_impl]{ impl->Listen(); } );
	}
	//Closed when this returns, so the next controller takes the port at once, as a suite's cases do on one port.  Not waited
	//for past five seconds, a strand with no thread free to run it, nor where the close is dropped with its context.
	Controller::~Controller(){
		std::promise<void> closed;
		auto done = closed.get_future();
		boost::asio::dispatch( _impl->Executor(), [impl=_impl, closed=std::move(closed)]()mutable{
			impl->Close();
			closed.set_value();
		} );
		if( done.wait_for(std::chrono::seconds{5})==std::future_status::timeout )
			WARN( "The test controller on port {} has not closed after 5 s:  no thread ran its strand, so the port may still be held.", Port() );
	}
	α Controller::Port()Ι->uint16_t{ return _impl->Port(); }
	α Controller::Edit( function<void( Script& )> edit )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, edit=move(edit)]{ impl->Edit( edit ); } );
	}
	α Controller::Push( Mid0061 result )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, result=move(result)]()mutable{ impl->Rundown( move(result), true, 0, {} ); } );
	}
	α Controller::Keep( Mid0061 result, uint32_t stationId, string stationName )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, result=move(result), stationId, stationName=move(stationName)]()mutable{
			impl->Rundown( move(result), false, stationId, move(stationName) );
		} );
	}
	α Controller::Select( Mid0015 pset )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, pset=move(pset)]()mutable{ impl->Select( impl->Pset, move(pset) ); } );
	}
	α Controller::Select( Mid0035 job )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, job=move(job)]()mutable{ impl->Select( impl->Job, move(job) ); } );
	}
	α Controller::Select( Mid0052 vin )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, vin=move(vin)]()mutable{ impl->Select( impl->Vin, move(vin) ); } );
	}
	α Controller::Raise( Mid0071 alarm )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, alarm=move(alarm)]()mutable{ impl->Raise( move(alarm) ); } );
	}
	α Controller::AcknowledgeAlarm()ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl]{ impl->AcknowledgeAlarm(); } );
	}
	α Controller::Drop()ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl]{ impl->Drop(); } );
	}
	α Controller::Silence( bool silent )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, silent]{ impl->Silence( silent ); } );
	}
	α Controller::Delay( Duration by, uint replies )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, by, replies]{ impl->Delay( by, replies ); } );
	}
	α Controller::SendRaw( string bytes )ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl, bytes=move(bytes)]{ impl->SendRaw( bytes ); } );
	}
	α Controller::Retransmit()ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl]{ impl->Retransmit(); } );
	}
	α Controller::SkipSequence()ι->void{
		boost::asio::post( _impl->Executor(), [impl=_impl]{ impl->SkipSequence(); } );
	}
	α Controller::Frames()Ι->vector<Received>{ return _impl->Frames(); }
	α Controller::Clients()Ι->uint{ return _impl->Connected; }
	α Controller::ToolLockAsked()Ι->bool{ return _impl->ToolLockAsked; }
	α Controller::ToolEnabled()Ι->bool{ return _impl->ToolEnabled; }
}