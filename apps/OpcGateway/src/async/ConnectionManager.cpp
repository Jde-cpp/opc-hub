#include "ConnectionManager.h"
#include <open62541/util.h>
#include <jde/fwk/process/thread.h>
#define let const auto

namespace Jde::Opc::Gateway{
	namespace asio = boost::asio;
	using tcp = asio::ip::tcp;
	using boost::system::error_code;
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Net };
	constexpr size_t rxSize{ 1<<16 };//the stock manager's recv-bufsize.
	constexpr char tcpName[]{ "tcp" };
	constexpr uint resolverThreads{ 4 };//getaddrinfo blocks:  a nameserver that never answers holds one for ~10 s.
	constexpr sv esName{ "asio tcp connection manager" };

	//The one thread every connection's socket runs on, made for the first manager and gone with the last:  a UA_Client_delete after
	//the gateway's shutdown still spins el->run until the loop stops, which a thread gone for good could never answer.  The thread
	//owns the io_context, so a NetThread released on its own thread - the last sp<Connection> can drop there - detaches it.
	struct NetThread final{
		Ω Acquire()ε->sp<NetThread>{
			static std::mutex mutex; static wp<NetThread> weak;
			lg _{ mutex };
			auto y = weak.lock();
			if( !y ){
				y = ms<NetThread>();
				weak = y;
			}
			return y;
		}
		NetThread()ε:
			Ctx{ Context() },
			Guard{ mu<asio::executor_work_guard<asio::io_context::executor_type>>(Ctx->get_executor()) },
			Worker{ [ctx=Ctx]{ Thread::SetName( "UANet" ); ctx->run(); } }
		{}
		~NetThread(){
			Guard.reset();
			if( Worker.get_id()==std::this_thread::get_id() )
				Worker.detach();
			else
				Worker.join();
		}
		//The first resolver starts the resolver threads and the first socket the reactor - its epoll and eventfd:  here, where a
		//failure throws, not in Connection's ι constructor.
		Ω Context()ε->sp<asio::io_context>{
			auto y = ms<asio::io_context>( asio::config_from_string{Ƒ("scheduler.concurrency_hint=1\nresolver.threads={}", resolverThreads)} );
			tcp::resolver{ *y };
			tcp::socket{ *y };
			return y;
		}
		const sp<asio::io_context> Ctx;
		up<asio::executor_work_guard<asio::io_context::executor_type>> Guard;
		std::jthread Worker;
	};

	//One tcp connection.  Its socket, state and queues belong to the net thread;  the loop thread sees it through Deliver, which
	//takes the one pending event under Handoff, and through the manager's map.  `Context` is the client's - it writes its channel
	//into it during the synchronous OPENING - and only Deliver passes it on.
	struct Connection final : std::enable_shared_from_this<Connection>{
		enum class EState : uint8{ Resolving, Connecting, Established, Closing, Closed };
		enum class EEvent : uint8{ None, Established, Data, Closing };
		Connection( AsioConnectionManager& cm, uintptr_t id, string host, optional<asio::ip::address> address, uint16 port, void* application, void* context, UA_ConnectionManager_connectionCallback callback, sp<NetThread> net )ι:
			Id{ id }, ClientHandle{ cm._client }, El{ cm.eventSource.eventLoop }, Application{ application }, Callback{ callback }, Host{ move(host) }, Address{ move(address) }, Port{ port }, Net{ move(net) }, Wake{ cm._wake },
			Context{ context }, Resolver{ *Net->Ctx }, Socket{ *Net->Ctx }{
			Dc.callback = &AsioConnectionManager::Deliver;
			Dc.application = &cm;
			Dc.context = (void*)Id;
		}
		~Connection(){//a failed write leaves what queued behind it, and Fail can't drain it:  the write's completion still reads Tx.front().
			for( auto& buf : Tx )
				UA_ByteString_clear( &buf );
		}
		α Post( auto&& f )ι->void{ asio::post( *Net->Ctx, [self=shared_from_this(), f=std::forward<decltype(f)>(f)]() mutable { (self.get()->*f)(); } ); }
		α LogPrefix()Ι->string{ return Ƒ("[{:x}.{}]", ClientHandle, Id); }

		//net thread.  An IP address skips the resolver, as getaddrinfo answers one without DNS.
		α Start()ι->void{
			if( State!=EState::Resolving )//closed before it started.
				return;
			if( Address ){
				State = EState::Connecting;
				Connect( std::array{tcp::endpoint{*Address, (asio::ip::port_type)Port}} );
				return;
			}
			++Inflight;
			Resolver.async_resolve( Host, std::to_string(Port), [self=shared_from_this()]( const error_code& ec, tcp::resolver::results_type results ){
				--self->Inflight;
				if( self->State!=EState::Resolving || ec ){
					self->Fail( ec, "resolve" );
					return;
				}
				self->State = EState::Connecting;
				self->Connect( results );
			});
		}
		α Connect( const auto& endpoints )ι->void{
			++Inflight;
			asio::async_connect( Socket, endpoints, [self=shared_from_this()]( const error_code& ec, const tcp::endpoint& endpoint ){
				--self->Inflight;
				if( self->State!=EState::Connecting || ec ){
					self->Fail( ec, "connect" );
					return;
				}
				error_code ignored;
				self->Socket.set_option( tcp::no_delay{true}, ignored );
				self->State = EState::Established;
				self->Writable = true;
				TRACE( "{}connected to {}", self->LogPrefix(), endpoint.address().to_string() );
				self->Raise( EEvent::Established );
			});
		}
		α ArmRead()ι->void{
			if( State!=EState::Established || CloseRequested ){
				if( State==EState::Established && (Writing || !Tx.empty()) )
					return;//RequestClose's rule:  the last write's completion closes behind what is queued - the CLO.
				if( State!=EState::Closed )
					FinishClose();
				return;
			}
			++Inflight;
			Socket.async_read_some( asio::buffer(Rx), [self=shared_from_this()]( const error_code& ec, size_t n ){
				--self->Inflight;
				if( ec || self->State!=EState::Established ){
					self->Fail( ec, "read" );
					return;
				}
				self->RxLength = n;
				self->Raise( EEvent::Data );
			});
		}
		α Enqueue( UA_ByteString buf )ι->void{
			if( State!=EState::Established || CloseRequested ){
				UA_ByteString_clear( &buf );
				return;
			}
			Tx.push_back( buf );
			if( !Writing )
				WriteNext();
		}
		α WriteNext()ι->void{
			Writing = true;
			++Inflight;
			asio::async_write( Socket, asio::buffer(Tx.front().data, Tx.front().length), [self=shared_from_this()]( const error_code& ec, size_t ){
				--self->Inflight;
				self->Writing = false;
				UA_ByteString_clear( &self->Tx.front() );
				self->Tx.pop_front();
				if( ec || self->State!=EState::Established ){
					self->Fail( ec, "write" );
					return;
				}
				if( !self->Tx.empty() )
					self->WriteNext();
				else if( self->CloseRequested )
					self->FinishClose();
			});
		}
		//closeConnection's, and the manager's stop:  what is queued still goes out - the CLO the client queued just before - and the
		//socket closes behind it;  the read then fails and raises the CLOSING.
		α RequestClose()ι->void{
			if( State==EState::Closed || CloseRequested )
				return;
			CloseRequested = true;
			Writable = false;
			if( State<EState::Established ){
				State = EState::Closing;//a lookup already in getaddrinfo can't be cancelled:  its success finds the state moved on, and fails.
				Resolver.cancel();
				error_code ignored;
				Socket.close( ignored );
			}
			else if( Tx.empty() && !Writing )
				FinishClose();
			MaybeRaiseClosing();
		}
		α FinishClose()ι->void{
			if( State==EState::Closed )
				return;
			State = EState::Closing;
			Writable = false;
			error_code ignored;
			Socket.shutdown( tcp::socket::shutdown_both, ignored );
			Socket.close( ignored );
			MaybeRaiseClosing();
		}
		//A failed resolve, connect, read or write ends the connection:  a cancelled one (RequestClose, or Fail itself) is reported once.
		//Before the connection is established it is a Warning, the only word on why:  open62541 logs the url alone, and reports any close
		//of a CONNECTING channel as BadConnectionRejected - a name that doesn't resolve too.
		α Fail( const error_code& ec, sv what )ι->void{
			if( State==EState::Closed )
				return;
			if( State!=EState::Closing && ec!=asio::error::operation_aborted )
				LOG( State<EState::Established ? ELogLevel::Warning : ELogLevel::Debug, _tags, "{}{} {}:{} failed:  {}", LogPrefix(), what, Host, Port, ec==asio::error::eof ? "closed by the server" : ec.message() );
			Writable = false;
			State = EState::Closing;
			Resolver.cancel();
			error_code ignored;
			Socket.close( ignored );
			MaybeRaiseClosing();
		}
		//CLOSING is the connection's last word:  once every operation has completed and the loop has taken the event before it.
		//With one pending, the re-arm Deliver posts back comes here through ArmRead.
		α MaybeRaiseClosing()ι->void{
			if( Inflight || State==EState::Closed )
				return;
			{
				lg _{ Handoff };
				if( Pending!=EEvent::None )
					return;
			}
			State = EState::Closed;
			Raise( EEvent::Closing );
		}
		//The hand-off:  the delayed callback into the loop, the cancel for a run blocked in its wait, and the wake for the processing
		//loop's.  Under Handoff with the first two:  Deliver takes it before it reads the event, so a CLOSING can set the manager STOPPED
		//- and the loop be freed - only once this is through with the loop.  The wake is the connection's own copy:  by then the
		//manager may be gone.
		α Raise( EEvent event )ι->void{
			{
				lg _{ Handoff };
				ASSERT( Pending==EEvent::None );
				Pending = event;
				El->addDelayedCallback( El, &Dc );
				El->cancel( El );
			}
			(*Wake)();
		}

		const uintptr_t Id;
		const Jde::Handle ClientHandle;
		UA_EventLoop* const El;
		void* const Application;
		const UA_ConnectionManager_connectionCallback Callback;
		const string Host;
		const optional<asio::ip::address> Address;//Host, where it is one.
		const uint16 Port;
		const sp<NetThread> Net;
		const ConnectionWake Wake;
		void* Context;//loop thread
		bool CloseCalled{};//loop thread:  closeConnection, or the manager's stop
		bool ClosingDelivered{};//loop thread
		tcp::resolver Resolver;//net thread, with the rest
		tcp::socket Socket;
		EState State{ EState::Resolving };
		std::array<UA_Byte,rxSize> Rx;
		size_t RxLength{};
		std::deque<UA_ByteString> Tx;
		bool Writing{};
		uint Inflight{};//resolve, connect, read and write outstanding
		bool CloseRequested{};
		std::atomic<bool> Writable{};//sendWithConnection reads it on the loop thread
		std::mutex Handoff;
		EEvent Pending{};
		UA_DelayedCallback Dc{};
	};

	α AsioConnectionManager::Create( Jde::Handle client, ConnectionWake wake )ε->UA_ConnectionManager*{
		return new AsioConnectionManager{ client, move(wake) };
	}
	AsioConnectionManager::AsioConnectionManager( Jde::Handle client, ConnectionWake wake )ε:
		UA_ConnectionManager{},
		_client{ client },
		_wake{ move(wake) },
		_net{ NetThread::Acquire() }{
		eventSource.eventSourceType = UA_EVENTSOURCETYPE_CONNECTIONMANAGER;
		let name = UA_STRING( (char*)esName.data() );
		UA_String_copy( &name, &eventSource.name );
		eventSource.start = &AsioConnectionManager::Start;
		eventSource.stop = &AsioConnectionManager::Stop;
		eventSource.free = &AsioConnectionManager::Free;
		protocol = UA_STRING( (char*)tcpName );
		openConnection = &AsioConnectionManager::OpenConnection;
		sendWithConnection = &AsioConnectionManager::SendWithConnection;
		closeConnection = &AsioConnectionManager::CloseConnection;
		allocNetworkBuffer = &AsioConnectionManager::AllocNetworkBuffer;
		freeNetworkBuffer = &AsioConnectionManager::FreeNetworkBuffer;
	}
	AsioConnectionManager::~AsioConnectionManager(){
		ASSERT( _connections.empty() );
		UA_String_clear( &eventSource.name );
		UA_KeyValueMap_clear( &eventSource.params );
	}
	α AsioConnectionManager::Stats( UA_Client* ua )ι->optional<Counts>{
		for( auto es = UA_Client_getConfig(ua)->eventLoop->eventSources; es; es = es->next ){
			if( es->eventSourceType!=UA_EVENTSOURCETYPE_CONNECTIONMANAGER || sv{(const char*)es->name.data, es->name.length}!=esName )
				continue;
			let& self = Self( es );
			return Counts{ self._opened, self._established, self._closings, self._resolves };
		}
		return nullopt;
	}
	α AsioConnectionManager::LogPrefix()Ι->string{ return Ƒ("[{:x}]", _client); }
	α AsioConnectionManager::Find( uintptr_t id )ι->sp<Connection>{
		lg _{ _mutex };
		auto p = _connections.find( id );
		return p==_connections.end() ? sp<Connection>{} : p->second;
	}
	α AsioConnectionManager::Erase( uintptr_t id )ι->void{
		lg _{ _mutex };
		_connections.erase( id );
	}
	α AsioConnectionManager::CheckStopped()ι->void{
		lg _{ _mutex };
		if( eventSource.state==UA_EVENTSOURCESTATE_STOPPING && _connections.empty() ){
			eventSource.state = UA_EVENTSOURCESTATE_STOPPED;
			DBG( "{}connection manager stopped", LogPrefix() );
		}
	}

	α AsioConnectionManager::Start( UA_EventSource* es )ι->UA_StatusCode{
		es->state = UA_EVENTSOURCESTATE_STARTED;
		return UA_STATUSCODE_GOOD;
	}
	α AsioConnectionManager::Stop( UA_EventSource* es )ι->void{
		auto& self = Self( es );
		{
			lg _{ self._mutex };
			es->state = UA_EVENTSOURCESTATE_STOPPING;
			for( let& [id, connection] : self._connections ){
				connection->CloseCalled = true;
				connection->Post( &Connection::RequestClose );
			}
		}
		self.CheckStopped();
	}
	α AsioConnectionManager::Free( UA_EventSource* es )ι->UA_StatusCode{
		if( es->state!=UA_EVENTSOURCESTATE_STOPPED && es->state!=UA_EVENTSOURCESTATE_FRESH )
			return UA_STATUSCODE_BADINTERNALERROR;
		delete &Self( es );
		return UA_STATUSCODE_GOOD;
	}

	α AsioConnectionManager::OpenConnection( UA_ConnectionManager* cm, const UA_KeyValueMap* params, void* application, void* context, UA_ConnectionManager_connectionCallback callback )ι->UA_StatusCode{
		auto& self = Self( cm );
		if( self.eventSource.state!=UA_EVENTSOURCESTATE_STARTED )
			return UA_STATUSCODE_BADINTERNALERROR;
		let listen = (const UA_Boolean*)UA_KeyValueMap_getScalar( params, UA_QUALIFIEDNAME(0, (char*)"listen"), &UA_TYPES[UA_TYPES_BOOLEAN] );
		if( listen && *listen )
			return UA_STATUSCODE_BADNOTSUPPORTED;//the gateway never reverse-connects.
		let port = (const UA_UInt16*)UA_KeyValueMap_getScalar( params, UA_QUALIFIEDNAME(0, (char*)"port"), &UA_TYPES[UA_TYPES_UINT16] );
		let address = (const UA_String*)UA_KeyValueMap_getScalar( params, UA_QUALIFIEDNAME(0, (char*)"address"), &UA_TYPES[UA_TYPES_STRING] );
		if( !port || !address || !address->length )
			return UA_STATUSCODE_BADINVALIDARGUMENT;
		let validate = (const UA_Boolean*)UA_KeyValueMap_getScalar( params, UA_QUALIFIEDNAME(0, (char*)"validate"), &UA_TYPES[UA_TYPES_BOOLEAN] );
		if( validate && *validate )
			return UA_STATUSCODE_GOOD;
		string host{ (const char*)address->data, address->length };
		error_code notIp;
		let ip = asio::ip::make_address( host, notIp );
		sp<Connection> connection;
		{
			lg _{ self._mutex };
			let id = self._nextId++;
			connection = ms<Connection>( self, id, move(host), notIp ? nullopt : optional{ip}, *port, application, context, callback, self._net );
			self._connections.emplace( id, connection );
		}
		++self._opened;
		if( !connection->Address )
			++self._resolves;
		DBG( "{}opening {}:{}", connection->LogPrefix(), connection->Host, connection->Port );
		callback( cm, connection->Id, application, &connection->Context, UA_CONNECTIONSTATE_OPENING, &UA_KEYVALUEMAP_NULL, UA_BYTESTRING_NULL );
		connection->Post( &Connection::Start );
		return UA_STATUSCODE_GOOD;
	}
	α AsioConnectionManager::SendWithConnection( UA_ConnectionManager* cm, uintptr_t connectionId, const UA_KeyValueMap*, UA_ByteString* buf )ι->UA_StatusCode{
		auto connection = Self( cm ).Find( connectionId );
		if( !connection || !connection->Writable ){
			UA_ByteString_clear( buf );
			return UA_STATUSCODE_BADCONNECTIONCLOSED;
		}
		let owned = *buf;
		UA_ByteString_init( buf );
		asio::post( *connection->Net->Ctx, [connection, owned]{ connection->Enqueue( owned ); } );
		return UA_STATUSCODE_GOOD;
	}
	α AsioConnectionManager::CloseConnection( UA_ConnectionManager* cm, uintptr_t connectionId )ι->UA_StatusCode{
		auto connection = Self( cm ).Find( connectionId );
		if( !connection )
			return UA_STATUSCODE_BADNOTFOUND;
		connection->CloseCalled = true;
		connection->Post( &Connection::RequestClose );
		return UA_STATUSCODE_GOOD;
	}
	α AsioConnectionManager::AllocNetworkBuffer( UA_ConnectionManager*, uintptr_t, UA_ByteString* buf, size_t bufSize )ι->UA_StatusCode{
		return UA_ByteString_allocBuffer( buf, bufSize );
	}
	α AsioConnectionManager::FreeNetworkBuffer( UA_ConnectionManager*, uintptr_t, UA_ByteString* buf )ι->void{
		UA_ByteString_clear( buf );
	}
	//Inside the loop's run, with its mutex held, as the stock manager's callbacks are.  Nothing of the manager's is held across the
	//client's callback:  it reopens and closes connections from inside it.  A CLOSING erases the connection first, so a close the
	//callback makes for the same id finds nothing, as the stock answers after its own removal, and checks the stop after.
	//After closeConnection or the stop, only the CLOSING reaches the client, as the stock's poll skips a socket it shut down:  a
	//chunk read before the close - the server's answers to the client's last Publishes - would land on a channel already CLOSING,
	//which refuses it with BadInvalidState and makes that the connect status, so open62541 would not reconnect.
	//The cancel at the end:  run processes delayed callbacks before its wait, and returns early from the wait for a socket event
	//alone - the stock manager's events are those, ours are not - so a synchronous call spinning run would get its answer here and
	//then sit out the wait, up to the housekeeping second.  Cancelling while run executes writes the self-pipe, and the wait returns.
	α AsioConnectionManager::Deliver( void* application, void* context )ι->void{
		auto& self = Self( (UA_ConnectionManager*)application );
		let id = (uintptr_t)context;
		auto connection = self.Find( id );
		if( !connection ){
			ERR( "{}delayed callback for unknown connection {}", self.LogPrefix(), id );
			return;
		}
		Connection::EEvent event;
		{
			lg _{ connection->Handoff };
			event = std::exchange( connection->Pending, Connection::EEvent::None );
		}
		let cm = static_cast<UA_ConnectionManager*>( &self );
		switch( event ){
		case Connection::EEvent::Established:
			if( !connection->CloseCalled ){
				++self._established;
				connection->Callback( cm, id, connection->Application, &connection->Context, UA_CONNECTIONSTATE_ESTABLISHED, &UA_KEYVALUEMAP_NULL, UA_BYTESTRING_NULL );
			}
			connection->Post( &Connection::ArmRead );
			break;
		case Connection::EEvent::Data:{
			if( !connection->CloseCalled ){
				const UA_ByteString msg{ connection->RxLength, connection->Rx.data() };
				connection->Callback( cm, id, connection->Application, &connection->Context, UA_CONNECTIONSTATE_ESTABLISHED, &UA_KEYVALUEMAP_NULL, msg );
			}
			connection->Post( &Connection::ArmRead );
			break;
		}
		case Connection::EEvent::Closing:
			ASSERT( !connection->ClosingDelivered );
			connection->ClosingDelivered = true;
			++self._closings;
			self.Erase( id );
			DBG( "{}closed", connection->LogPrefix() );
			connection->Callback( cm, id, connection->Application, &connection->Context, UA_CONNECTIONSTATE_CLOSING, &UA_KEYVALUEMAP_NULL, UA_BYTESTRING_NULL );
			self.CheckStopped();
			break;
		case Connection::EEvent::None:
			ERR( "{}delayed callback with no event", connection->LogPrefix() );
			return;
		}
		connection->El->cancel( connection->El );
	}
}