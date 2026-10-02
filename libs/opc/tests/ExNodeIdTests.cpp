//ExNodeId is NodeId plus namespaceUri/serverIndex, with its own json shape (ns is omitted when 0), its own hash and a
//7-column insert contract.  Same round trips as NodeIdTests, plus the ownership handoff NodeId is missing.
#include <concepts>
#include <gtest/gtest.h>
#include <absl/hash/hash.h>
#include <jde/db/Value.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/NodeId.h>

#define let const auto

namespace Jde::Opc::Tests{
	Ω exFromJson( sv json )ε->ExNodeId{ return ExNodeId{ parse(json) }; }
	//The rest/query-string spelling.  Both ctors read nsu/serverindex since #17; this one is kept as the independent path,
	//so a round-trip test cannot pass by both halves being wrong the same way.
	Ω exFromParams( sv idKey, sv idValue, sv nsu, UA_UInt32 serverIndex )ε->ExNodeId{
		flat_map<string,string> params;
		params.emplace( string{idKey}, string{idValue} );
		if( nsu.size() )
			params.emplace( "nsu", string{nsu} );
		if( serverIndex )
			params.emplace( "serverindex", std::to_string(serverIndex) );
		return ExNodeId{ params };
	}

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

	//The rest/query-string spelling: flat lowercase keys rather than json.  Both ctors read nsu and serverindex since
	//review2 #17 - see exFromParams above, which is why this is the independent path rather than the only one.
	TEST( ExNodeIdTests, FromRestParams ){
		flat_map<string,string> params;
		params.emplace( "ns", "2" );
		params.emplace( "s", "tag" );
		params.emplace( "nsu", "urn:test" );
		params.emplace( "serverindex", "3" );
		ExNodeId x{ params };
		ASSERT_TRUE( x.String() );
		EXPECT_EQ( *x.String(), "tag" );
		EXPECT_EQ( x.NsIndex(), 2 );
		EXPECT_EQ( ToSV(x.namespaceUri), "urn:test" );
		EXPECT_EQ( x.serverIndex, 3u );

		flat_map<string,string> numericOnly;
		numericOnly.emplace( "i", "5002" );
		ExNodeId n{ numericOnly };
		EXPECT_EQ( *n.Numeric(), 5002u );
		EXPECT_EQ( n.namespaceUri.length, 0u );
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
		EXPECT_EQ( exFromJson(R"({"i":85})").to_string(), R"({"i":85})" );

		let full = exFromParams( "i", "85", "urn:test", 3 ).ToJson();
		EXPECT_EQ( full.at("nsu").as_string(), "urn:test" );
		EXPECT_EQ( full.at("serverindex").to_number<int>(), 3 );
	}

	TEST( ExNodeIdTests, CopyIsDeepMoveTransfers ){
		ExNodeId src = exFromParams( "s", "tag", "urn:test", 0 );
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

	TEST( ExNodeIdTests, CopyReturnsAnIndependentNodeId ){
		ExNodeId x{ parse(R"({"ns":2,"s":"tag"})") };
		auto raw = x.Copy();
		EXPECT_NE( raw.identifier.string.data, x.nodeId.identifier.string.data );
		EXPECT_EQ( ToSV(raw.identifier.string), "tag" );
		EXPECT_EQ( ToSV(x.nodeId.identifier.string), "tag" ); //the source keeps its own.
		UA_NodeId_clear( &raw );
	}

	TEST( ExNodeIdTests, Ordering ){
		EXPECT_TRUE( exFromJson(R"({"i":1})") < exFromJson(R"({"i":2})") );
		EXPECT_TRUE( exFromJson(R"({"ns":0,"i":5})") < exFromJson(R"({"ns":1,"i":1})") );
		EXPECT_TRUE( exFromJson(R"({"i":5})") == exFromJson(R"({"i":5})") );
		EXPECT_FALSE( exFromJson(R"({"i":5})") == exFromParams("i", "5", "", 1) );          //serverIndex is part of identity...
		EXPECT_FALSE( exFromJson(R"({"i":5})") == exFromParams("i", "5", "urn:test", 0) );  //...and so is namespaceUri.
		EXPECT_TRUE( exFromParams("i", "5", "", 1) == exFromParams("i", "5", "", 1) );
	}

	//ExNodeId's hash folds namespaceUri, serverIndex, the namespace index and the identifier.  The namespace index was missing
	//(C4), so ids differing only by namespace collided while operator< - what equality is built on - separated them.
	TEST( ExNodeIdTests, HashAgreesWithEquality ){
		EXPECT_EQ( absl::HashOf(exFromJson(R"({"ns":2,"s":"tag"})")), absl::HashOf(exFromJson(R"({"ns":2,"s":"tag"})")) );
		EXPECT_EQ( absl::HashOf(exFromJson(R"({"i":5})")), absl::HashOf(exFromJson(R"({"i":5})")) );
		EXPECT_NE( absl::HashOf(exFromJson(R"({"s":"tag"})")), absl::HashOf(exFromJson(R"({"s":"other"})")) );
		EXPECT_NE( absl::HashOf(exFromJson(R"({"i":5})")), absl::HashOf(exFromParams("i", "5", "", 1)) );
		EXPECT_NE( absl::HashOf(exFromJson(R"({"i":5})")), absl::HashOf(exFromParams("i", "5", "urn:test", 0)) );
		EXPECT_NE( absl::HashOf(exFromJson(R"({"ns":2,"i":5})")), absl::HashOf(exFromJson(R"({"ns":3,"i":5})")) );//the namespace index
		EXPECT_NE( absl::HashOf(exFromJson(R"({"ns":2,"s":"tag"})")), absl::HashOf(exFromJson(R"({"s":"tag"})")) );//...including against ns 0.
	}

	//ExNodeId's AbslHashValue covers every field operator< compares (reviews/abseil.md D3):  each identifier kind, alone and with a
	//namespaceUri or serverIndex, differs from every other and equals its copy.  A null and an empty namespaceUri compare equal,
	//so they must hash equal - the representation case absl::VerifyTypeImplementsAbslHashCorrectly would catch, were gmock built.
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
				ASSERT_EQ( ids[i]==copies[j], i==j ) << ids[i].to_string() << " vs " << copies[j].to_string();
				if( i==j )
					EXPECT_EQ( absl::HashOf(ids[i]), absl::HashOf(copies[j]) ) << ids[i].to_string();
				else
					EXPECT_NE( absl::HashOf(ids[i]), absl::HashOf(copies[j]) ) << ids[i].to_string() << " vs " << copies[j].to_string();
			}
		}
		auto emptyUri = exFromJson( R"({"ns":2,"i":5})" );
		emptyUri.namespaceUri = UA_String_fromChars( "" );
		ASSERT_TRUE( emptyUri==ids[0] );
		EXPECT_EQ( absl::HashOf(emptyUri), absl::HashOf(ids[0]) );
	}

	TEST( ExNodeIdTests, InsertParams ){
		ExNodeId x = exFromParams( "s", "tag", "urn:test", 3 );
		x.nodeId.namespaceIndex = 2;
		let plain = x.InsertParams( false );
		EXPECT_EQ( plain.size(), 5u ); //the extended columns are opt-in.

		let extended = x.InsertParams( true );
		ASSERT_EQ( extended.size(), 7u );
		EXPECT_EQ( extended[0].ToUInt(), 2u );
		EXPECT_TRUE( extended[1].is_null() );
		ASSERT_TRUE( extended[2].is_string() );
		EXPECT_EQ( extended[2].get_string(), "tag" );
		ASSERT_TRUE( extended[5].is_string() );
		EXPECT_EQ( extended[5].get_string(), "urn:test" );
		EXPECT_EQ( extended[6].ToUInt(), 3u );

		let noUri = exFromJson( R"({"ns":2,"s":"tag"})" ).InsertParams( true );
		ASSERT_EQ( noUri.size(), 7u );
		EXPECT_TRUE( noUri[5].is_null() ); //an empty namespaceUri is null, not "".
	}

	//L16, the ExNodeId half: its own UA_String_fromChars( ….c_str() ) sites are the flat_map ctor's identifier and the
	//namespaceUri, which is the one field NodeId has no twin for.
	TEST( ExNodeIdTests, EmbeddedNulSurvivesTheIdentifierAndUri ){
		const string identifier{ "a\0b", 3 };
		const string uri{ "urn:\0test", 9 };
		let x = exFromParams( "s", identifier, uri, 3 );
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

		//the two ctors have to agree; exFromParams passes no "ns", so compare it against the ns-less json spelling.
		EXPECT_TRUE( exFromJson(R"({"i":7,"nsu":"urn:test","serverindex":3})")==exFromParams("i", "7", "urn:test", 3) );

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

	//#6 (fixed), the ExNodeId twin.  The extended columns follow the same rule - an absent namespaceUri is null too.
	TEST( ExNodeIdTests, UnusedIdentifierColumnsAreNull ){
		let params = exFromJson( R"({"ns":2,"s":"tag"})" ).InsertParams( false );
		ASSERT_EQ( params.size(), 5u );
		EXPECT_TRUE( params[3].is_null() ) << "guid column of a string id";
		EXPECT_TRUE( params[4].is_null() ) << "bytes column of a string id";

		let numericParams = exFromJson( R"({"ns":2,"i":5002})" ).InsertParams( false );
		EXPECT_TRUE( numericParams[2].is_null() ) << "string column of a numeric id";
		EXPECT_TRUE( numericParams[3].is_null() ) << "guid column of a numeric id";
		EXPECT_TRUE( numericParams[4].is_null() ) << "bytes column of a numeric id";
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
		EXPECT_TRUE( round==original ) << original.to_string();

		//and the rest-params ctor, the other reader of "b".
		flat_map<string,string> params;
		params.emplace( "ns", "2" );
		params.emplace( "b", "3q2+7w==" );
		ExNodeId fromParams{ params };
		EXPECT_TRUE( fromParams==original );
	}
}
