//ExNodeId is NodeId plus namespaceUri/serverIndex, with its own json shape (ns is omitted when 0) and its own hash.
//Same round trips as NodeIdTests, plus the ownership handoff NodeId is missing.
#include <concepts>
#include <gtest/gtest.h>
#include <absl/hash/hash.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/NodeId.h>

#define let const auto

namespace Jde::Opc::Tests{
	Ω exFromJson( sv json )ε->ExNodeId{ return ExNodeId{ parse(json) }; }

	TEST( ExNodeIdTests, FromJsonForms ){
		EXPECT_EQ( *exFromJson(R"({"ns":2,"i":5002})").Numeric(), 5002u );
		EXPECT_EQ( exFromJson(R"({"ns":2,"i":5002})").NsIndex(), 2 );
		EXPECT_EQ( *exFromJson(R"({"s":"tag"})").String(), "tag" );
		EXPECT_EQ( exFromJson(R"({"s":"tag"})").NsIndex(), 0 );
		EXPECT_EQ( exFromJson(R"({"i":7})").serverIndex, 0u );
		EXPECT_EQ( exFromJson(R"({"i":7})").namespaceUri.length, 0u );
	}

	//The shape apps/OpcGateway/tests BrowseTests feeds straight to ExNodeId - the result of a
	//`node( opc:, path: ){ id name parents{} }` query, where the identifier is nested under "id" and the sibling members
	//are ignored.  That test needs a live gateway and asserts no more than `Numeric()>0`, so pin the conversion here.
	TEST( ExNodeIdTests, FromNodeQueryResult ){
		let numeric = exFromJson( R"({"id":{"ns":4,"i":6001},"name":"Lamp1","parents":[]})" );
		ASSERT_TRUE( numeric.Numeric() );
		EXPECT_EQ( *numeric.Numeric(), 6001u );
		EXPECT_EQ( numeric.NsIndex(), 4 );

		let stringId = exFromJson( R"({"id":{"ns":4,"s":"Lamp1"},"name":"Lamp1"})" );
		ASSERT_TRUE( stringId.String() );
		EXPECT_EQ( *stringId.String(), "Lamp1" );
		EXPECT_EQ( stringId.NsIndex(), 4 );
	}

	//Unlike NodeId's, ExNodeId's serializer omits ns when it is 0 - both spellings have to survive FromJson.  The
	//nsu/serverindex cases only became meaningful with #17: before it they round tripped vacuously, because ToJson wrote
	//both fields and the ctor dropped them again.
	TEST( ExNodeIdTests, JsonRoundTrip ){
		for( let json : { R"({"ns":2,"i":5002})", R"({"i":85})", R"({"ns":2,"s":"tag.one"})", R"({"ns":7,"s":"x"})",
				R"({"ns":2,"g":"12345678-1234-5678-1234-567812345678"})",
				R"({"ns":2,"i":7,"nsu":"urn:test"})", R"({"i":7,"serverindex":3})",
				R"({"ns":2,"s":"tag","nsu":"urn:test","serverindex":3})" } ){
			let original = exFromJson( json );
			let round = ExNodeId{ jvalue{original.ToJson()} };
			EXPECT_TRUE( round==original ) << json;
		}
	}

	TEST( ExNodeIdTests, ToJsonOmitsDefaults ){
		let j = exFromJson( R"({"i":85})" ).ToJson();
		EXPECT_FALSE( j.contains("ns") );          //ns 0 is implied...
		EXPECT_FALSE( j.contains("nsu") );         //...as are an empty namespaceUri...
		EXPECT_FALSE( j.contains("serverindex") ); //...and server 0.
		EXPECT_EQ( j.at("i").to_number<int>(), 85 );
		EXPECT_EQ( serialize(exFromJson(R"({"i":85})").ToJson()), R"({"i":85})" );
		EXPECT_EQ( exFromJson(R"({"i":85})").ToString(), "i=85" );//ToString is the UA spelling, as NodeId's.
		EXPECT_EQ( exFromJson(R"({"ns":2,"s":"tag"})").ToString(), "ns=2;s=tag" );

		let full = exFromJson( R"({"i":85,"nsu":"urn:test","serverindex":3})" ).ToJson();
		EXPECT_EQ( full.at("nsu").as_string(), "urn:test" );
		EXPECT_EQ( full.at("serverindex").to_number<int>(), 3 );
	}

	TEST( ExNodeIdTests, CopyIsDeepMoveTransfers ){
		ExNodeId src = exFromJson( R"({"s":"tag","nsu":"urn:test"})" );
		ExNodeId copy{ src };
		EXPECT_TRUE( copy==src );
		EXPECT_NE( copy.nodeId.identifier.string.data, src.nodeId.identifier.string.data );
		EXPECT_NE( copy.namespaceUri.data, src.namespaceUri.data );

		ExNodeId moveSource{ src };
		let* identifier = moveSource.nodeId.identifier.string.data;
		ExNodeId moved{ move(moveSource) };
		EXPECT_EQ( moved.nodeId.identifier.string.data, identifier ); //transferred, not reallocated.
		EXPECT_EQ( moveSource.nodeId.identifier.string.data, nullptr );
	}

	//The accessor NodeId lacks (reviews/opc-review2.md #2):  Move() hands the identifier over and nulls the source, so
	//`raw.nodeId = x.Move()` transfers ownership instead of aliasing it.
	template<class T> concept HasNodeIdMove = requires( T t ){ { t.Move() } -> std::same_as<UA_NodeId>; };
	TEST( ExNodeIdTests, MoveHandsOwnershipOver ){
		EXPECT_TRUE( HasNodeIdMove<ExNodeId> );

		ExNodeId x{ parse(R"({"ns":2,"s":"tag"})") };
		let* identifier = x.nodeId.identifier.string.data;
		auto raw = x.Move();
		EXPECT_EQ( raw.identifier.string.data, identifier );
		EXPECT_EQ( x.nodeId.identifier.string.data, nullptr );
		UA_NodeId_clear( &raw ); //Move() made us the owner.
	}

	TEST( ExNodeIdTests, Ordering ){
		EXPECT_TRUE( exFromJson(R"({"i":1})") < exFromJson(R"({"i":2})") );
		EXPECT_TRUE( exFromJson(R"({"ns":0,"i":5})") < exFromJson(R"({"ns":1,"i":1})") );
		EXPECT_TRUE( exFromJson(R"({"i":5})") == exFromJson(R"({"i":5})") );
		EXPECT_FALSE( exFromJson(R"({"i":5})") == exFromJson(R"({"i":5,"serverindex":1})") );  //serverIndex is part of identity...
		EXPECT_FALSE( exFromJson(R"({"i":5})") == exFromJson(R"({"i":5,"nsu":"urn:test"})") );  //...and so is namespaceUri.
		EXPECT_TRUE( exFromJson(R"({"i":5,"serverindex":1})") == exFromJson(R"({"i":5,"serverindex":1})") );
	}

	//ExNodeId's hash folds namespaceUri, serverIndex, the namespace index and the identifier.  The namespace index was missing
	//(C4), so ids differing only by namespace collided while operator< - what equality is built on - separated them.
	TEST( ExNodeIdTests, HashAgreesWithEquality ){
		EXPECT_EQ( absl::HashOf(exFromJson(R"({"ns":2,"s":"tag"})")), absl::HashOf(exFromJson(R"({"ns":2,"s":"tag"})")) );
		EXPECT_EQ( absl::HashOf(exFromJson(R"({"i":5})")), absl::HashOf(exFromJson(R"({"i":5})")) );
		EXPECT_NE( absl::HashOf(exFromJson(R"({"s":"tag"})")), absl::HashOf(exFromJson(R"({"s":"other"})")) );
		EXPECT_NE( absl::HashOf(exFromJson(R"({"i":5})")), absl::HashOf(exFromJson(R"({"i":5,"serverindex":1})")) );
		EXPECT_NE( absl::HashOf(exFromJson(R"({"i":5})")), absl::HashOf(exFromJson(R"({"i":5,"nsu":"urn:test"})")) );
		EXPECT_NE( absl::HashOf(exFromJson(R"({"ns":2,"i":5})")), absl::HashOf(exFromJson(R"({"ns":3,"i":5})")) );//the namespace index
		EXPECT_NE( absl::HashOf(exFromJson(R"({"ns":2,"s":"tag"})")), absl::HashOf(exFromJson(R"({"s":"tag"})")) );//...including against ns 0.
	}

	//ExNodeId's AbslHashValue covers every field operator< compares (reviews/abseil.md D3):  each identifier kind, alone and with a
	//namespaceUri or serverIndex, differs from every other and equals its copy.  A null and an empty namespaceUri are distinct
	//under the vendor's order (reviews/opc-refactor.md A1), as for NodeId's identifier.
	TEST( ExNodeIdTests, AbslHashAgreesWithEquality ){
		vector<ExNodeId> ids;
		for( let json : {R"({"ns":2,"i":5})", R"({"ns":3,"i":5})", R"({"ns":2,"s":"5"})", R"({"ns":2,"g":"12345678-1234-5678-1234-567812345678"})", R"({"ns":2,"b":"BQ=="})"} ){
			ids.push_back( exFromJson(json) );
			auto withUri = exFromJson( json );
			withUri.namespaceUri = UA_String_fromChars( "urn:test" );
			ids.push_back( move(withUri) );
			auto withServer = exFromJson( json );
			withServer.serverIndex = 1;
			ids.push_back( move(withServer) );
		}
		const vector<ExNodeId> copies = ids;
		for( uint i=0; i<ids.size(); ++i ){
			for( uint j=0; j<copies.size(); ++j ){
				ASSERT_EQ( ids[i]==copies[j], i==j ) << ids[i].ToString() << " vs " << copies[j].ToString();
				if( i==j )
					EXPECT_EQ( absl::HashOf(ids[i]), absl::HashOf(copies[j]) ) << ids[i].ToString();
				else
					EXPECT_NE( absl::HashOf(ids[i]), absl::HashOf(copies[j]) ) << ids[i].ToString() << " vs " << copies[j].ToString();
			}
		}
		auto emptyUri = exFromJson( R"({"ns":2,"i":5})" );
		emptyUri.namespaceUri = UA_String_fromChars( "" );
		EXPECT_FALSE( emptyUri==ids[0] );
	}

	//L16, the ExNodeId half: the namespaceUri is the one field NodeId has no twin for.
	TEST( ExNodeIdTests, EmbeddedNulSurvivesTheIdentifierAndUri ){
		const string identifier{ "a\0b", 3 };
		const string uri{ "urn:\0test", 9 };
		let x = exFromJson( R"({"s":"a\u0000b","nsu":"urn:\u0000test","serverindex":3})" );
		ASSERT_TRUE( x.String() );
		EXPECT_EQ( *x.String(), identifier );
		EXPECT_EQ( ToString(x.namespaceUri), uri );
		EXPECT_EQ( x.namespaceUri.length, 9u );
	}


	TEST( ExNodeIdTests, ConstructFromNodeId ){
		let n = NodeId{ parse(R"({"ns":2,"s":"tag"})") };
		ExNodeId x{ n };
		ASSERT_TRUE( x.String() );
		EXPECT_EQ( *x.String(), "tag" );
		EXPECT_NE( x.nodeId.identifier.string.data, n.identifier.string.data ); //a deep copy, not an alias.
		EXPECT_EQ( x.NsIndex(), 2 );
		EXPECT_EQ( x.serverIndex, 0u );
		EXPECT_EQ( x.namespaceUri.length, 0u );
	}

	//#17 (fixed): ExNodeId( const jvalue& ) read its two extra fields with Json::FindDefaultSV( v, "nsu" ) /
	//Json::FindNumber<UA_UInt32>( v, "serverindex" ).  The jvalue overloads of those take a *json pointer path*
	//(value::try_at_pointer), not a member name, and a pointer that does not start with '/' never resolves - so both
	//always came back empty and the json ctor could only ever produce a plain NodeId.
	TEST( ExNodeIdTests, JsonCtorReadsNamespaceUriAndServerIndex ){
		let x = exFromJson( R"({"ns":2,"i":7,"nsu":"urn:test","serverindex":3})" );
		EXPECT_EQ( ToSV(x.namespaceUri), "urn:test" );
		EXPECT_EQ( x.serverIndex, 3u );
		EXPECT_EQ( x.NsIndex(), 2 );

		//each field on its own, so a fix that reads only one of them is caught.
		EXPECT_EQ( ToSV(exFromJson(R"({"i":7,"nsu":"urn:test"})").namespaceUri), "urn:test" );
		EXPECT_EQ( exFromJson(R"({"i":7,"nsu":"urn:test"})").serverIndex, 0u );
		EXPECT_EQ( exFromJson(R"({"i":7,"serverindex":3})").serverIndex, 3u );
		EXPECT_EQ( exFromJson(R"({"i":7,"serverindex":3})").namespaceUri.length, 0u );
	}

	//The bare forms NodeId::FromJson accepts have no object to read those fields off - if_object, not as_object.
	TEST( ExNodeIdTests, JsonCtorAcceptsBareIdentifiers ){
		let numeric = ExNodeId{ parse("5002") };
		ASSERT_TRUE( numeric.Numeric() );
		EXPECT_EQ( *numeric.Numeric(), 5002u );
		EXPECT_EQ( numeric.namespaceUri.length, 0u );
		EXPECT_EQ( numeric.serverIndex, 0u );

		let stringId = ExNodeId{ parse(R"("tag")") };
		ASSERT_TRUE( stringId.String() );
		EXPECT_EQ( *stringId.String(), "tag" );
	}

	//#7 (fixed), the ExNodeId twin:  hex out, base64 in.  base64 both ways now.
	TEST( ExNodeIdTests, ByteStringJsonRoundTrip ){
		UA_NodeId ua{};
		ua.namespaceIndex = 2;
		ua.identifierType = UA_NODEIDTYPE_BYTESTRING;
		UA_ByteString_allocBuffer( &ua.identifier.byteString, 4 );
		::memcpy( ua.identifier.byteString.data, "\xde\xad\xbe\xef", 4 );
		ExNodeId original{ move(ua) };
		ASSERT_TRUE( original.IsBytes() );
		EXPECT_EQ( original.ToJson().at("b").as_string(), "3q2+7w==" );
		let round = ExNodeId{ jvalue{original.ToJson()} };
		EXPECT_TRUE( round==original ) << original.ToString();
	}
}
