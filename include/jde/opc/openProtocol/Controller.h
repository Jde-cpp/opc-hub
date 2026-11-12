#pragma once
#include <boost/asio/io_context.hpp>
#include "Codec.h"

//The controller's side of the codec, for the suites and the emulator (docs/open-protocol/spec.md *Tests*, *Emulator*):
//an acceptor on a loopback port and a session per client, answering as its script says.  It shares the driver's codec,
//so it proves the two sides agree and not that either agrees with a controller.
namespace Jde::Opc::OpenProtocol{
	struct Controller final{
		//What a case sets before the controller listens, and changes through Edit while it runs.  A MID here is the one a
		//client sends:  0060 for the results it subscribes to.
		struct Script{
			//MID 0002's fields.  SequenceNumberSupport 1 numbers the controller's messages where MID 0002 goes at revision 6.
			Mid0002 Identity;
			//The last revision the controller has of a MID, the codec's where it names none.  Asked for one past it, it answers
			//MID 0004 with Table 23's code for the MID's kind, 74 for a subscription, 76 for MID 0064 and 0080 and 97 for the
			//rest, or, where Lowers names the MID, the reply or the pushes at its own last revision (§1.4).
			flat_map<uint16_t,uint16_t> Revisions;
			flat_set<uint16_t> Lowers;
			flat_map<uint16_t,uint16_t> Refuses;//a MID answered by MID 0004 with this code, whatever it asks.
			bool PushOnSubscribe{ true };//the selected set, job and VIN and an active alarm's status go out as their subscription is accepted.
			Duration Skew{};//MID 0081's time ahead of the clock's.
			const std::chrono::time_zone* Zone{};//the zone MID 0081's time is local to, the process's own where null.
			optional<Duration> DropSilent;//a client with no message either way for this long is dropped:  15 s on a controller (§5.28.1).
			bool OneActor{};//session control (§3.3.4):  a MID 0001 while another client has started is refused with code 35.
			bool Viewer{};//every client is a Viewer:  MID 0018, 0038, 0042, 0043 and 0050 are refused with code 92.
			bool Log{};//every frame in and out at Trace under the openProtocol tag.
		};
		//A frame a client sent.
		struct Received{
			uint Client;//1 for the first accepted.
			Header Head;//all defaults where the header did not read.
			string Frame;
			steady_clock::time_point At;
		};

		Controller( boost::asio::io_context& context, uint16_t port, Script script )ε;//`port` 0 takes a free one.  Throws where it cannot listen.
		Controller( const Controller& )=delete;
		α operator=( const Controller& )->Controller& =delete;
		~Controller();//closes the acceptor and every client.
		α Port()Ι->uint16_t;
		α Edit( function<void( Script& )> edit )ι->void;

		//A rundown at the station:  kept for MID 0064 and pushed to each client subscribed to results.  One with no
		//tightening id takes the next, a run the controller's stations share.
		α Push( Mid0061 result )ι->void;
		//A rundown at another of the controller's stations:  kept under the same run of ids, and pushed to no client here.
		α Keep( Mid0061 result, uint32_t stationId, string stationName )ι->void;
		//The selected parameter set, the job and the VIN:  kept, and pushed to their subscribers.
		α Select( Mid0015 pset )ι->void;
		α Select( Mid0035 job )ι->void;
		α Select( Mid0052 vin )ι->void;
		α Raise( Mid0071 alarm )ι->void;//an alarm:  pushed, and active for MID 0076 until acknowledged.
		α AcknowledgeAlarm()ι->void;//on the controller:  MID 0074 to the alarm's subscribers.

		//The faults.
		α Drop()ι->void;//closes every client's socket.
		α Silence( bool silent=true )ι->void;//reads on and answers nothing:  no acknowledgement, reply or push.
		α Delay( Duration by )ι->void;//holds the next reply this long.
		α SendRaw( string bytes )ι->void;//to each client as they are:  a malformed frame.
		α Retransmit()ι->void;//each client's last message again, under the number it went with.
		α SkipSequence()ι->void;//each client's next message a sequence number late.

		//These from any thread, of what has happened so far.
		α Frames()Ι->vector<Received>;
		α Clients()Ι->uint;//the clients connected now.
		α ToolLockAsked()Ι->bool;//a MID 0001 at revision 8 asked for the tool lock at disconnection, and was refused as every revision past the codec's is.
		α ToolEnabled()Ι->bool;//MID 0042 and 0043.
	private:
		struct Impl;
		const sp<Impl> _impl;
	};
}