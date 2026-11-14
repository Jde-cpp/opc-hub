#include <jde/flows/Replay.h>

#define let const auto

namespace Jde::Opc::Flows{
	constexpr ELogTags _tags{ ELogTags::App };
	constexpr uint _slice{ 256 };//the timers one step runs before the strand's other work, a cancel's teardown among it, has its turn.
	constexpr std::array<sv,3> _ends{ "completed", "cancelled", "failed" };

	α StreamClock::Now()Ι->TimePoint{ lg _{ _mutex }; return _now; }
	α StreamClock::Schedule( TimePoint due, absl::AnyInvocable<void()> f )ι->TimerId{
		lg _{ _mutex };
		let id = ++_nextId;
		_timers.emplace( std::pair{due, id}, std::move(f) );
		_due.emplace( id, due );
		return id;
	}
	α StreamClock::Schedule( Duration after, absl::AnyInvocable<void()> f )ι->TimerId{
		lg _{ _mutex };
		let id = ++_nextId;
		_timers.emplace( std::pair{_now+after, id}, std::move(f) );
		_due.emplace( id, _now+after );
		return id;
	}
	α StreamClock::Cancel( TimerId id )ι->bool{
		lg _{ _mutex };
		let due = _due.find( id );
		if( due==_due.end() )
			return false;
		_timers.erase( std::pair{due->second, id} );
		_due.erase( due );
		return true;
	}
	α StreamClock::AdvanceTo( TimePoint to, uint most )ι->bool{
		for( uint ran=0;; ++ran ){
			absl::AnyInvocable<void()> f;
			{
				lg _{ _mutex };
				if( _timers.empty() || _timers.begin()->first.first>to ){
					_now = std::max( _now, to );
					return true;
				}
				if( ran==most )
					return false;
				auto timer = _timers.extract( _timers.begin() );
				_due.erase( timer.key().second );
				_now = std::max( _now, timer.key().first );
				f = std::move( timer.mapped() );
			}
			try{
				f();
			}
			catch( const std::exception& e ){
				CRITICAL( "Timer callback threw: {}", e.what() );
			}
		}
	}
	α StreamClock::Pending()Ι->uint{ lg _{ _mutex }; return _timers.size(); }

	//A source whose records the run delivers:  each a message made here, at the clock's time, and sent on.
	struct StreamSource final : ISource{
		α Deliver( jobject&& fields )ι->void{
			auto m = Make();
			for( auto& field : fields ){
				if( field.key()!="_id" && field.key()!="_parent" && field.key()!="_time" )
					m.Json[field.key()] = move( field.value() );
			}
			Send( move(m) );
		}
	};
	struct Silent final : ISource{};

	struct Run::Impl final : std::enable_shared_from_this<Impl>{
		//A source's records, a page of them in hand.
		struct Reader final{ StreamSource* Node; up<IStream> Stream; vector<StreamRecord> Page; uint Next{}; bool Done{}; };
		//The run's nodes:  a stream's records in its node's place, a `connection` silent, the rest the host's.
		struct Nodes final : IHost{
			Nodes( Impl& run )ι:_run{ run }{}
			α Source( const NodeDef& node )ε->up<ISource> override{
				if( node.Type==ENodeType::Connection )
					return mu<Silent>();
				if( node.Type!=ENodeType::OpcIn && node.Type!=ENodeType::GroupIn )
					return _run._config.Host->Source( node );
				auto stream = _run._config.Host->Stream( node );
				THROW_IF( !stream, "The host has no records of a '{}' node.", ToString(node.Type) );
				auto y = mu<StreamSource>();
				_run._readers.emplace_back( y.get(), move(stream) );
				return y;
			}
			α Sink( const NodeDef& node )ε->up<ISink> override{ return _run._config.Host->Sink( node ); }
		private:
			Impl& _run;//which holds the flow that holds this.
		};
		Ω Flowing( Flow::Config&& flow, Impl& run )ι->Flow::Config{
			flow.Clock = run._config.Clock;
			flow.Host = run._config.Host ? ms<Nodes>( run ) : nullptr;
			flow.Active = {};
			if( !flow.Run )
				flow.Run = 0;
			return move( flow );
		}
		Impl( Graph&& graph, Flow::Config&& flow, Config&& config )ι:
			_config{ move(config) },
			_flow{ move(graph), Flowing(move(flow), Prepared()) }
		{}

		α Start()ι->bool{
			return _flow.Start( [self=shared_from_this()]{
				if( self->_flow.Running() )
					self->Post();
				else{
					let fault = self->_flow.Fault();
					self->Finish( ERunEnd::Failed, fault ? Ƒ("{}{}", fault->NodeId.empty() ? string{} : Ƒ("Node '{}':  ", fault->NodeId), fault->Message) : "The run was stopped as it started." );
				}
			} );
		}
		//Any thread:  the flow stops, and Ended is told behind its teardown, once.
		α Finish( ERunEnd end, string why )ι->void{
			if( _finishing.exchange(true) )
				return;
			_flow.Stop( [self=shared_from_this(), end, why=move(why)]()mutable{
				self->_readers.clear();
				self->_config.Pace = {};
				if( auto ended = std::exchange(self->_config.Ended, {}) )//let go of, with what it holds:  a host's may hold the run.
					ended( end, move(why) );
			} );
		}

		Config _config;
		Flow _flow;
		vector<Reader> _readers;//in the graph's order, made as the flow makes its nodes;  the strand's from then on.
		std::atomic<bool> _finishing{};
		std::atomic<uint> _delivered{}, _pages{};
	private:
		α Prepared()ι->Impl&{
			if( !_config.Clock )
				_config.Clock = ms<StreamClock>( _config.From );
			return *this;
		}
		α Post()ι->void{ _flow.Post( [self=shared_from_this()]{ self->Step(); } ); }
		//The reader whose next record is the earliest, a tie the first's, and that record's time;  none at the streams' end.
		//A record past To is its stream's end.  Pending:  a reader whose page held no record, which reads again next step.
		struct Peeked final{ optional<std::pair<uint,TimePoint>> Next; bool Pending{}; };
		α Peek()ε->Peeked{
			Peeked y;
			for( uint i=0; i<_readers.size(); ++i ){
				auto& reader = _readers[i];
				if( !reader.Done && reader.Next==reader.Page.size() ){
					auto page = reader.Stream->Next();
					reader.Next = 0;
					reader.Done = !page;
					reader.Page = page ? move( *page ) : vector<StreamRecord>{};
					if( page )
						++_pages;
					if( page && reader.Page.empty() ){
						y.Pending = true;
						continue;
					}
				}
				if( !reader.Done && reader.Page[reader.Next].Time>_config.To ){
					reader.Done = true;
					reader.Page.clear();
				}
				if( reader.Done )
					continue;
				if( let time = reader.Page[reader.Next].Time; !y.Next || time<y.Next->second )
					y.Next = { i, time };
			}
			return y;
		}
		//On the strand, an entry of its own, so each timer the clock runs drains what it sends before the next:  the clock to
		//the next record's time, a slice of the timers on the way, then the record;  the step after once the queues are empty.
		α Step()ι->void{
			if( _finishing )
				return;
			try{
				let peeked = Peek();
				if( peeked.Pending ){//the strand's other work has its turn between two reads.
					Post();
					return;
				}
				let& next = peeked.Next;
				let reached = _config.Clock->AdvanceTo( next ? next->second : _config.To, _slice );//short of it with more timers due:  the step after's.
				if( reached && !next ){
					Finish( ERunEnd::Completed, {} );
					return;
				}
				if( reached ){
					auto& reader = _readers[next->first];
					reader.Node->Deliver( move(reader.Page[reader.Next++].Fields) );
					++_delivered;
				}
				_flow.Quiet( [self=shared_from_this()]{ self->Paced(); } );
			}
			catch( const std::exception& e ){
				Finish( ERunEnd::Failed, e.what() );
			}
		}
		α Paced()ι->void{
			if( _config.Pace )
				_config.Pace( [self=shared_from_this()]( string failed ){
					if( failed.empty() )
						self->Post();
					else
						self->Finish( ERunEnd::Failed, move(failed) );
				} );
			else
				Post();
		}
	};

	Run::Run( Graph graph, Flow::Config flow, Config config )ι:
		_impl{ ms<Impl>(move(graph), move(flow), move(config)) }
	{}
	Run::~Run(){
		if( _impl )
			_impl->Finish( ERunEnd::Cancelled, {} );
	}
	α Run::Start()ι->bool{ return _impl->Start(); }
	α Run::Cancel()ι->void{ _impl->Finish( ERunEnd::Cancelled, {} ); }
	α Run::Delivered()Ι->uint{ return _impl->_delivered; }
	α Run::Pages()Ι->uint{ return _impl->_pages; }
	α Run::Counts()Ι->vector<NodeCounts>{ return _impl->_flow.Counts(); }
	α Run::Fault()Ι->optional<Flows::Fault>{ return _impl->_flow.Fault(); }
}
namespace Jde::Opc{
	α Flows::ToString( ERunEnd end )ι->sv{ return (uint8)end<_ends.size() ? _ends[(uint8)end] : sv{ "?" }; }
}