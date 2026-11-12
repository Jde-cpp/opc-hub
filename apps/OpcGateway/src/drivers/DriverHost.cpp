#include "DriverHost.h"
#include <latch>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/process/thread.h>
#include <jde/opc/server/INodeAccess.h>
#include "../GatewayAppClient.h"
#include "DriverServer.h"
#include "openProtocol/OpenProtocolDriver.h"

#define let const auto
namespace Jde::Opc{
	//The kinds with a driver.
	constexpr array<std::pair<sv,Gateway::MakeDriver>,1> _kinds{{ {Gateway::ServerCnnctn::KindOpenProtocol, &Gateway::OpenProtocolDriver::Make} }};
	α Gateway::FindDriver( sv kind )ι->MakeDriver{
		let p = std::ranges::find( _kinds, kind, &std::pair<sv,MakeDriver>::first );
		return p==_kinds.end() ? nullptr : p->second;
	}
}
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };
	constexpr Duration _closeWait{ 2s };//how long a stop waits for the sessions its clients are closing.

	//A driver server's INodeAccess:  browse and reads open to every session, #237's posture, and nothing writable, edited or,
	//until the commands' lookup, called (docs/open-protocol/spec.md *Sessions and rights*).
	struct DriverNodeAccess final : Opc::Server::INodeAccess{
		α NodeRights( const NodeId&, UserPK )ι->Access::ERights override{ return Access::ERights::Read; }
		α EditRights( const NodeId&, UserPK )ι->Access::ERights override{ return Access::ERights::None; }
		α MayBrowse( const NodeId&, UserPK )ι->bool override{ return true; }
		α UserRights( const NodeId&, UserPK )ι->Opc::Server::EAccess override{ return Opc::Server::EAccess::Read; }
	};

	//The running hosts, and the thread their starts and stops run on after the gateway's own start.
	struct DriverHosts final{
		~DriverHosts(){ Work.reset(); }
		α Add( const ServerCnnctn& row )ε->void{
			auto host = ms<DriverHost>( row );
			lg _{ Mutex };
			Running.insert_or_assign( row.Id, move(host) );
		}
		α Find( const DB::Key& row )ι->sp<DriverHost>{
			lg _{ Mutex };
			let p = row.IsPK() ? Running.find( (ServerCnnctnPK)row.PK() ) : std::ranges::find_if( Running, [&]( let& host ){ return host.second->Slug()==row.NK(); } );
			return p==Running.end() ? nullptr : p->second;
		}
		//A stopped host leaves Running once it has stopped:  until then it is the row's, stopping.
		α Apply( const DB::Key& row, bool removed )ι->void{
			if( auto host = Find(row); host ){
				DriverHost::Stop( {&host, 1}, true );
				lg _{ Mutex };
				Running.erase( host->Id() );
			}
			if( removed )
				return;
			try{
				let rows = BlockAwait<ServerCnnctnAwait,vector<ServerCnnctn>>( ServerCnnctnAwait{row} );
				if( rows.size() && rows.front().HasDriver() )
					Add( rows.front() );
			}
			catch( const std::exception& e ){
				ERR( "{}", e.what() );
			}
		}
		α StopAll( bool wait )ι->void{
			vector<sp<DriverHost>> hosts;
			{
				lg _{ Mutex };
				for( let& [_, host] : Running )
					hosts.push_back( host );
			}
			DriverHost::Stop( hosts, wait );
			lg _{ Mutex };
			Running.clear();
		}
		std::mutex Mutex;
		flat_map<ServerCnnctnPK,sp<DriverHost>> Running;//under Mutex;  changed on Thread alone.
		boost::asio::io_context Context{ 1 };
		boost::asio::executor_work_guard<boost::asio::io_context::executor_type> Work{ boost::asio::make_work_guard(Context) };
		std::jthread Thread{ [this]{ Jde::Thread::SetName( "DriverHosts" ); Context.run(); } };//last:  joined before what it runs goes.
	};
	static up<DriverHosts> _hosts; static std::mutex _hostsMutex;//_hosts under it, and every post to its thread:  none comes after Shutdown's.

	DriverHost::DriverHost( const ServerCnnctn& row, SL sl )ε:
		_id{ row.Id },
		_slug{ row.Slug },
		_port{ row.DriverPort(sl) }
	{
		bool made{};
		try{
			let make = FindDriver( row.Kind );
			THROW_IFSL( !make, "kind '{}' has no driver.", row.Kind );
			_driver = make( row, sl );
			auto settings = DriverServer::Settings( row.Kind, _slug, _port, sl );
			_driver->Configure( settings );
			_server = mu<Opc::Server::UAServer>( settings, AppClient(), ms<DriverNodeAccess>(), nullptr );
			let ns = UA_Server_addNamespace( _server->Ptr(), NamespaceUri(_slug, row.Kind).c_str() );
			THROW_IFSL( ns!=ServerCnnctn::DriverBrowseNs, "its namespace is {}, not {}.", ns, ServerCnnctn::DriverBrowseNs );
			_driver->Build( *_server->Ptr(), ns );
			made = true;
			_server->Run();//fails where it cannot listen.
			_sessions = _server->Sessions();
		}
		catch( const std::exception& e ){
			throw Exception{ sl, {_tags}, "Connection '{}' has no driver server:  it could not {} 127.0.0.1:{} - /gateway/drivers/portBase, {}, plus the row's id, {}:  {}", _slug, made ? "listen on a port something else may hold," : "be made for", _port, ServerCnnctn::DriverPortBase(), _id, e.what() };
		}
		_driver->Start();
		INFO( "Connection '{}':  its {} driver's server listens on 127.0.0.1:{}.", _slug, row.Kind, _port );
	}
	//Each driver's `stopped` counts down a latch it shares:  the waiter may be gone before the last count_down has returned.
	Ω stopDrivers( std::span<IDriver* const> drivers )ι->void{
		auto stopped = ms<std::latch>( (std::ptrdiff_t)drivers.size() );
		for( let driver : drivers )
			driver->Stop( [stopped]{ stopped->count_down(); } );
		stopped->wait();
	}
	DriverHost::~DriverHost(){
		if( _server ){//not through Stop:  as one.
			IDriver* const driver{ _driver.get() };
			stopDrivers( {&driver, 1} );
		}
	}
	α DriverHost::Stop( std::span<const sp<DriverHost>> hosts, bool wait )ι->void{
		if( hosts.empty() )
			return;
		vector<IDriver*> drivers;
		for( let& host : hosts )
			drivers.push_back( host->_driver.get() );
		stopDrivers( drivers );
		//The clients were told to go before this:  each closes its session on a live server, as a UA client should.  Those open
		//now, not one a reconnect opens meanwhile, which the server's stop ends and the next server takes.
		if( wait ){
			flat_map<const DriverHost*,flat_set<string>> closing;
			for( let& host : hosts ){
				for( let& session : host->Sessions() )
					closing[host.get()].emplace( session.Id );
			}
			let open = [&]{
				uint y{};
				for( let& host : hosts ){
					for( let& session : host->Sessions() )
						y += closing[host.get()].contains( session.Id );
				}
				return y;
			};
			let deadline = steady_clock::now()+_closeWait;
			while( open() && steady_clock::now()<deadline )
				std::this_thread::sleep_for( 5ms );
			if( let left = open(); left )
				WARN( "{} session(s) of the gateway's own clients were still open when their driver server stopped.", left );
		}
		for( let& host : hosts ){
			host->_driver.reset();
			host->_server.reset();
			INFO( "Connection '{}':  its driver's server on 127.0.0.1:{} stopped.", host->_slug, host->_port );
		}
	}

	α DriverHost::Start()ε->void{
		DriverHosts* hosts;
		{
			lg _{ _hostsMutex };
			_hosts = mu<DriverHosts>();
			hosts = _hosts.get();
		}
		struct Started final{ std::latch Done{ 1 }; std::exception_ptr Error; };
		auto started = ms<Started>();
		boost::asio::post( hosts->Context, [hosts, started]{
			try{
				for( let& row : BlockAwait<ServerCnnctnAwait,vector<ServerCnnctn>>(ServerCnnctnAwait{}) ){
					if( row.HasDriver() )
						hosts->Add( row );
				}
			}
			catch( ... ){
				started->Error = std::current_exception();
			}
			started->Done.count_down();
		} );
		started->Done.wait();
		if( started->Error ){
			Shutdown( true );
			std::rethrow_exception( started->Error );
		}
	}
	α DriverHost::Shutdown( bool terminate )ι->void{
		up<DriverHosts> hosts;
		{
			lg _{ _hostsMutex };
			hosts = move( _hosts );
		}
		if( !hosts )
			return;
		boost::asio::post( hosts->Context, [&]{ hosts->StopAll( !terminate ); } );
		hosts->Work.reset();
		hosts->Thread.join();//what a row's change posted runs first.
	}
	α DriverHost::RowChanged( DB::Key row, bool removed )ι->void{
		lg _{ _hostsMutex };
		if( _hosts )
			boost::asio::post( _hosts->Context, [hosts=_hosts.get(), row=move(row), removed]{ hosts->Apply( row, removed ); } );
	}
	α DriverHost::Find( const ServerCnnctnNK& slug )ι->sp<DriverHost>{
		lg _{ _hostsMutex };
		if( !_hosts )
			return nullptr;
		lg _2{ _hosts->Mutex };
		let p = std::ranges::find_if( _hosts->Running, [&]( let& host ){ return host.second->Slug()==slug; } );
		return p==_hosts->Running.end() ? nullptr : p->second;
	}
}