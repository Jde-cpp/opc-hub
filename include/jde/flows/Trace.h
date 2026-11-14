#pragma once
#include "Node.h"

//The trace (docs/flows/spec.md *Trace*):  the hooks on a flow's message path, and their fold into one record per node per
//message.  The day files are FileTrace.h's;  what writes them derives from HopTrace.
namespace Jde::Opc::Flows{
	//A node's figures:  since the flow started, or a minute's.
	struct NodeCounts final{
		string Node;
		uint Received{};
		vector<uint> Sent;//per output port.
		uint Drops{};
		uint Errors{};
		α Any()Ι->bool{ return Received || Drops || Errors || !Sent.empty(); }
	};
	struct MinuteCounts final{
		TimePoint Minute;//its start, on the flow's clock.
		vector<NodeCounts> Nodes;//those with a figure.
	};

	//Each on the flow's strand, in the order things happen, but Minute, which comes from whatever rolls the minute.
	struct ITrace{
		virtual ~ITrace()=default;
		β Birth( const NodeDef& node, const Message& m )ι->void=0;//made at the node and about to be sent:  a source's, a split's part, what a catch takes.
		β Clone( const NodeDef& node, const Message& clone, uint port )ι->void=0;//the clone the port's second wire and each after it takes, queued once.
		β Receive( const NodeDef& node, const Message& m )ι->void=0;
		β Send( const NodeDef& node, uint port, const Message& m, uint wires )ι->void=0;//`wires` 0:  the port feeds nothing, or its wires took clones.
		β Complete( const NodeDef& node, MessageId id )ι->void=0;//the node finished the message, or sent the one it made.
		β Error( const NodeDef& node, MessageId id, sv error )ι->void=0;//the node's error with the message, which ends at it.
		β Status( const NodeDef& node, const NodeStatus& status )ι->void=0;
		β Drop( const NodeDef& node, MessageId id )ι->void=0;//off the node's queue, or held by it when the flow stopped.
		β Capture( const NodeDef& node, MessageId id, sv json )ι->void=0;//what a sink of a replay would have sent with the message it holds, in place of sending it (spec *Replay*).
		//A minute that ended with a figure, on the flow's clock, as the engine's own timer, a count in a later minute, a
		//TakeMinutes or the flow's stop ends it, whichever first:  from any thread.  A flow stopped and started again within a
		//minute gives that minute twice, each run's part.
		β Minute( const MinuteCounts& minute )ι->void=0;
	};

	enum class ETraceLevel : uint8{ Off, Counts, Errors, Hops };
	α ToString( ETraceLevel level )ι->sv;
	α ToTraceLevel( sv name )ι->optional<ETraceLevel>;

	enum class ERecordKind : uint8{ Hop, Birth, Clone, Error, Status, Drop, Captured };
	α ToString( ERecordKind kind )ι->sv;//"hop", "birth", "clone", "error", "status", "drop", "captured".
	α ToRecordKind( sv name )ι->optional<ERecordKind>;
	struct TraceRecord final{
		TimePoint Time;//the flow's clock:  a hop's, when the node received the message.
		MessageId Id{};
		MessageId Parent{};
		string Node;
		ERecordKind Kind{};
		vector<uint> Ports;//the output ports it was sent on:  a clone's, the one it left by.
		optional<string> Json;//a birth's message, a hop's as sent where the node changed it, and what a capture's sink would have sent.
		string Error;
		optional<NodeStatus> Status;
		uint32 Run{};//0 live, a replay's otherwise.
		bool Cut{};//Json is cut at the store's valueLimit:  read back from a file alone.
		vector<string> Blocks;//the ids of the blocks a block-built Function body passed with the message, in order.
	};

	//The fold:  a hop is made at Receive, takes its ports and its json at Send, and is written at Complete;  a birth the same
	//from Birth;  a clone, an error, a status change, a drop and a capture are records of their own.  By level:  `Off`
	//writes nothing;  `Counts` the minute figures alone;  `Hops` everything;  `Errors` the minute figures, the errors, drops,
	//status changes and captures, and the hops, births and clones of a message kept while it or a message made from it is in
	//flight, written when one ends in an error.  One per flow, called on its strand alone, but Minute.
	struct HopTrace : ITrace{
		HopTrace( ETraceLevel level, sp<Hist::IClock> clock )ι;
		~HopTrace();
		α Level()Ι->ETraceLevel;
		α Birth( const NodeDef& node, const Message& m )ι->void final;
		α Clone( const NodeDef& node, const Message& clone, uint port )ι->void final;
		α Receive( const NodeDef& node, const Message& m )ι->void final;
		α Send( const NodeDef& node, uint port, const Message& m, uint wires )ι->void final;
		α Complete( const NodeDef& node, MessageId id )ι->void final;
		α Error( const NodeDef& node, MessageId id, sv error )ι->void final;
		α Status( const NodeDef& node, const NodeStatus& status )ι->void final;
		α Drop( const NodeDef& node, MessageId id )ι->void final;
		α Capture( const NodeDef& node, MessageId id, sv json )ι->void final;
		α Minute( const MinuteCounts& minute )ι->void final;
	protected:
		β Write( TraceRecord&& record )ι->void=0;
		β WriteMinute( const MinuteCounts& )ι->void{}//at `Counts` and above, from any thread.
	private:
		struct Impl;
		up<Impl> _impl;
	};
}