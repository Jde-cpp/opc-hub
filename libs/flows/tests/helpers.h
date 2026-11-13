#pragma once
#include <future>
#include "../../historian/tests/ManualClock.h"

#define let const auto

//A graph written out as a test reads it, a host whose sources and sinks the test scripts, a trace kept in memory, and the
//fixture that runs a flow over them on a ManualClock.  Times are on Monday 9 March 2026, UTC.
namespace Jde::Opc::Flows::Tests{
	using namespace std::chrono;
	using Hist::Tests::ManualClock;

	Ξ At( int h, int m=0, int s=0 )ι->TimePoint{ return sys_days{2026y/March/9}+hours{h}+minutes{m}+seconds{s}; }
	Ξ Iso( TimePoint t )ι->jvalue{ return jvalue{ ToIsoString(t) }; }//a time as a message carries it.
	Ξ Has( str message, sv part )ι->::testing::AssertionResult{
		return message.find( part )!=string::npos ? ::testing::AssertionSuccess() : ::testing::AssertionFailure() << "'" << message << "' lacks '" << part << "'";
	}

	//A node of a graph:  its props as json text, its wires a list of target ids per output port.
	Ξ Node( sv id, sv type, sv props="{}", std::initializer_list<std::initializer_list<sv>> wires={} )->jobject{
		jarray ports;
		for( let& port : wires ){
			jarray ids;
			for( let target : port )
				ids.emplace_back( target );
			ports.push_back( move(ids) );
		}
		return { {"id", id}, {"type", type}, {"props", parse(props)}, {"wires", move(ports)} };
	}
	Ξ Nodes( std::initializer_list<jobject> nodes )->jvalue{
		jarray y;
		for( let& node : nodes )
			y.push_back( node );
		return jobject{ {"nodes", move(y)} };
	}

	//What the suite's sinks took, by node id, in the order they took it.
	struct Taken final{
		α Add( str node, const jobject& message )ι->void{ lg _{ _mutex }; _byNode[node].push_back( message ); }
		α Of( sv node )Ι->vector<jobject>{
			lg _{ _mutex };
			let p = _byNode.find( node );
			return p==_byNode.end() ? vector<jobject>{} : p->second;
		}
		//Each message's `key`, as json text, in order:  `1 2 3`.
		α Each( sv node, sv key="payload" )Ι->string{
			string y;
			for( let& message : Of(node) ){
				let v = message.if_contains( key );
				y += Ƒ( "{}{}", y.empty() ? "" : " ", v ? serialize(*v) : "-" );
			}
			return y;
		}
	private:
		mutable mutex _mutex;
		std::map<string,vector<jobject>,std::less<>> _byNode;
	};

	//The sources and sinks a gateway would make, as a test needs them.
	//  `opc in`:  a source the test pushes values through, from its own thread, or has read a run of them a page at a time.
	//  `group in`:  refused, as a gateway with no historian refuses it.
	//  `log`:  keeps what it takes;  with `tally` in its props it counts them in its own context and in the flow's.
	//  `opc out`:  a write.  A payload of "bad" is refused, the node's error;  one of "hold" is held until the test answers
	//  it;  any other is kept and sent on with a status of 0, the node showing a green dot.
	//  `variable`:  its Start throws, as a sink whose server is gone would.
	//OnSource, when set, runs as each source is made.
	struct TestHost final : IHost{
		struct Pump final : ISource{
			α Push( jvalue payload, sv topic={} )ι->void{
				auto m = Make();
				m.Json["payload"] = move( payload );
				if( !topic.empty() )
					m.Json["topic"] = topic;
				Emit( move(m) );
			}
			α Push( const Value& value )ι->void{
				auto m = Make();
				m.SetDataChange( "plc1", "ns=2;s=Count", value );
				m.Json["topic"] = "ns=2;s=Count";
				Emit( move(m) );
			}
			//As a source that reads what it sends:  0 to `count`, `page` at a time, each page once the flow has taken the last.
			α Read( uint count, uint page )ι->void{ Post( [=, this]{ Next( 0, count, page ); } ); }
		private:
			α Next( uint from, uint count, uint page )ι->void{
				let end = std::min( from+page, count );
				for( uint i=from; i<end; ++i ){
					auto m = Make();
					m.Json["payload"] = i;
					Send( move(m) );
				}
				if( end<count )
					Idle( [=, this]{ Next( end, count, page ); } );
			}
		};
		struct Taker final : ISink{
			Taker( sp<Taken> taken )ι:_taken{ move(taken) }{}
			α Start()ε->void override{
				THROW_IF( Def().Type==ENodeType::Variable, "The flow server is not listening." );
			}
			α Receive( Message& m )ε->void override{
				let write = Def().Type==ENodeType::OpcOut;
				THROW_IF( write && m.Payload()=="bad", "Bad_NotWritable" );
				if( write && m.Payload()=="hold" ){
					Hold();
					_held.push_back( move(m) );
					return;
				}
				_taken->Add( Def().Id, m.Json );
				if( Json::FindBool(Def().Props, "tally").value_or(false) ){
					for( auto store : {&Context(), &FlowContext()} ){
						let seen = store->Get( "seen" );
						store->Set( "seen", (seen ? seen->to_number<uint>() : 0)+1 );
					}
				}
				if( !write )
					return;
				m.Json["status"] = 0;
				Send( move(m) );
				SetStatus( {"green", "dot", "Good"} );
			}
			//The server's answer to the oldest held write, from the test's thread:  Good, or `error`.
			α Answer( string error={} )ι->void{
				Post( [this, error=move(error)]()mutable{
					auto m = move( _held.front() );
					_held.erase( _held.begin() );
					let id = m.Id();
					if( !error.empty() )
						return Fail( move(m), move(error) );
					_taken->Add( Def().Id, m.Json );
					Send( move(m) );
					Done( id );
				} );
			}
			α Garble()ι->void{ Post( []{ THROW( "The answer does not read." ); } ); }//an answer that throws as it is read, on the strand.
		private:
			sp<Taken> _taken;
			vector<Message> _held;
		};

		α Source( const NodeDef& node )ε->up<ISource> override{
			THROW_IF( node.Type==ENodeType::GroupIn, "The gateway has no historian." );
			if( OnSource )
				OnSource();
			auto y = mu<Pump>();
			Pumps[node.Id] = y.get();
			return y;
		}
		α Sink( const NodeDef& node )ε->up<ISink> override{
			auto y = mu<Taker>( Took );
			Takers[node.Id] = y.get();
			return y;
		}
		sp<Taken> Took{ ms<Taken>() };
		flat_map<string,Pump*> Pumps;//the flow's, while it lives.
		flat_map<string,Taker*> Takers;
		function<void()> OnSource;
	};

	struct MemoryTrace final : HopTrace{
		using HopTrace::HopTrace;
		α Write( TraceRecord&& record )ι->void override{ lg _{ _mutex }; _records.push_back( move(record) ); }
		α Records()Ι->vector<TraceRecord>{ lg _{ _mutex }; return _records; }
		//Each record as `kind node [ports]`, with `*` where it carries json:  `birth in [0]*`, `hop sw [1]`, `error w`.
		α Lines()Ι->vector<string>{
			constexpr std::array<sv,6> kinds{ "hop", "birth", "clone", "error", "status", "drop" };
			vector<string> y;
			for( let& record : Records() ){
				auto line = Ƒ( "{} {}", kinds[(uint8)record.Kind], record.Node );
				if( record.Kind<=ERecordKind::Clone ){
					string ports;
					for( let port : record.Ports )
						ports += Ƒ( "{}{}", ports.empty() ? "" : ",", port );
					line += Ƒ( " [{}]{}", ports, record.Json ? "*" : "" );
				}
				y.push_back( move(line) );
			}
			return y;
		}
	private:
		mutable mutex _mutex;
		vector<TraceRecord> _records;
	};

	struct FlowTest : ::testing::Test{
		α TearDown()->void override{
			if( Deployed )
				Stop();
		}
		//The graph started on the clock, its nodes' Start run.
		α Run( const jvalue& graph, ETraceLevel level=ETraceLevel::Hops, uint queue=1000 )->Flow&{
			auto read = Graph::Read( graph );
			if( !read ){
				ADD_FAILURE() << read.error().NodeId << ":  " << read.error().Message;
				read = Graph{};
			}
			Trace = ms<MemoryTrace>( level, Time );
			Deployed.emplace( move(*read), Flow::Config{
				.Name="test", .Clock=Time, .Host=Host, .Trace=Trace, .Ids=ms<MessageIds>(At(0)), .Global=Global, .Queue=queue,
				.Context=move(Context), .NodeContext=move(NodeContext),
				.ContextChanged=[this]( sv node, sv key, const jvalue* value ){
					lg _{ ChangesMutex };
					Changes.push_back( Ƒ("{}/{}={}", node, key, value ? serialize(*value) : "-") );
				}
			} );
			EXPECT_TRUE( Deployed->Start() );
			Sync();
			return *Deployed;
		}
		//`f` on the flow's strand, and what it queued through the flow, which drains once `f` returns:  the wait is for a
		//second post, behind that.
		α OnStrand( function<void()> f )->void{
			Posted( move(f) );
			Sync();
		}
		α Sync()->void{ Posted( []{} ); }
		α Posted( function<void()> f )->void{
			std::promise<void> done;
			auto wait = done.get_future();
			Deployed->Post( [f=move(f), done=move(done)]()mutable{
				f();
				done.set_value();
			} );
			try{
				wait.get();
			}
			catch( const std::future_error& ){
				ADD_FAILURE() << "The flow is not running.";
			}
		}
		//The clock moved on the strand, so each timer on the way runs with the clock at its own time and what it sends is
		//through the flow before the next.
		α Advance( Duration d )->void{ OnStrand( [&]{ Time->Advance( d ); } ); }
		α Inject( jvalue payload, sv topic={}, sv node="in" )->void{
			jobject message{ {"payload", move(payload)} };
			if( !topic.empty() )
				message["topic"] = topic;
			Deployed->Inject( node, &message );
			Sync();
		}
		α Stop()->void{
			std::promise<void> done;
			auto wait = done.get_future();
			Deployed->Stop( [&done]{ done.set_value(); } );
			wait.get();
		}
		α Took( sv node="out", sv key="payload" )Ι->string{ return Host->Took->Each( node, key ); }
		α Counts( sv node )Ι->NodeCounts{
			for( auto& counts : Deployed->Counts() ){
				if( counts.Node==node )
					return counts;
			}
			ADD_FAILURE() << "no node " << node;
			return {};
		}

		sp<ManualClock> Time{ ms<ManualClock>(At(6)) };
		sp<TestHost> Host{ ms<TestHost>() };
		sp<GlobalStore> Global{ ms<GlobalStore>() };
		sp<MemoryTrace> Trace;
		jobject Context;//the flow scope's values a Run loads.
		flat_map<string,jobject> NodeContext;
		mutex ChangesMutex;
		vector<string> Changes;//each context change the host was told of, `node/key=value`.
		optional<Flow> Deployed;
	};
}