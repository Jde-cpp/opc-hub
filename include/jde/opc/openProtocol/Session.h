#pragma once
#include <absl/functional/any_invocable.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include "Codec.h"

//The integrator's side of an Open Protocol connection (docs/open-protocol/spec.md *The protocol*):  a TCP socket on the
//caller's strand that starts communication, keeps it alive, acknowledges what the controller pushes and opens it again
//when it goes.
namespace Jde::Opc::OpenProtocol{
	using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;

	enum class EFailure : uint8_t{
		Socket,//the socket's error:  no connection, a reset, the controller's close.
		Refused,//MID 0004:  `Code` is Table 23's.
		Timeout,//nothing within replyTimeout.
		Sequence,//link level:  a number out of order, or MID 9998, whose `Code` is Table 9's.
		NotConnected,//a request made while the link is down, or still queued when it went.
		Stopped,
		Value//a request the codec does not encode:  nothing was sent.
	};
	//Why a link went or a request failed.
	struct Failure{
		EFailure Kind;
		uint16_t Mid{};//the MID it is about, 0 where there is none.
		uint16_t Code{};
		string What;
	};

	struct Session final{
		struct Settings{
			Duration KeepAlive{ std::chrono::seconds{10} };//MID 9999 after this long without a message either way (§5.28.1).
			Duration ReplyTimeout{ std::chrono::seconds{10} };//a connection, a reply or a MID 9997 not here by then is a lost link (§3.3.5).
			Duration ReconnectMin{ std::chrono::seconds{1} };//the wait before the next attempt, doubling to ReconnectMax until MID 0002 answers.
			Duration ReconnectMax{ std::chrono::minutes{1} };
			//A MID's highest revision to ask, under the codec's own.  A ceiling named for what a request asks for bounds the
			//request too:  0061's is 0060's.
			flat_map<uint16_t,uint16_t> Revisions;
			bool Log{};//every frame in and out at Trace under the openProtocol tag.
		};
		//What MID 0002 answered, read by the revision in its own header.
		struct Connection{
			Mid0002 Identity;
			uint16_t Revision;
			bool LinkLevel;//revision 6 says the controller numbers its messages:  MID 9997 and 9998 acknowledge, and no per-topic MID does.
		};
		//Each on the strand.
		struct Events{
			function<void( Connection&& )> Connected;
			function<void( const Failure& )> Lost;//the link went, or an attempt to open it failed;  the next attempt follows the backoff.
			function<void( Message&& )> Push;//a subscription's message, decoded by its own header, its acknowledgement already sent.
		};
		using Reply = std::expected<Message,Failure>;//MID 0005, or the data a request is answered by.
		using OnReply = absl::AnyInvocable<void( Reply&& )>;

		Session( Strand strand, string host, uint16_t port, Settings settings, Events events )ι;
		Session( const Session& )=delete;
		α operator=( const Session& )->Session& =delete;
		~Session();//closes with no MID 0003, and calls nothing after it unless a call is already running:  Stop first.
		//These three from any thread.
		α Start()ι->void;
		//Sent behind what is outstanding, one request at a time (§3.2.1), at the revision settled for its MID or else the
		//highest under its ceiling, a step down on each MID 0004 with code 74, 76 or 97.  `onReply` runs on the strand.
		α Request( Body request, OnReply onReply )ι->void;
		//MID 0003 where the link is up, then the close.  After `stopped`, on the strand, nothing is called and nothing of the
		//caller's is held.
		α Stop( absl::AnyInvocable<void()> stopped={} )ι->void;
		//These on the strand.  A revision is settled for the connection's life by what the controller answered:  the revision
		//a request was accepted at, or the one in the header of its reply or of a push it subscribed to.
		α Revision( uint16_t mid )Ι->uint16_t;//the settled one, or what the next request of `mid` starts at.
		α Revisions()Ι->flat_map<uint16_t,uint16_t>;//the settled ones, by the MID asked.
	private:
		struct Impl;
		const sp<Impl> _impl;
	};
}