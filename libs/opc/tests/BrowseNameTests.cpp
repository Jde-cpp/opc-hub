//BrowseName wraps UA_QualifiedName and owns the name buffer.  The "2~Tag" fully-qualified form used in config feeds it.
#include <cstddef>
#include <new>
#include <gtest/gtest.h>
#include <jde/opc/uatypes/BrowseName.h>

#define let const auto

namespace Jde::Opc::Tests{
	TEST( BrowseNameTests, FullyQualifiedForm ){
		BrowseName qualified{ "2~Tag", 0 };
		EXPECT_EQ( qualified.namespaceIndex, 2 );
		EXPECT_EQ( ToSV(qualified.name), "Tag" );

		BrowseName defaulted{ "Tag", 3 }; //no '~': the default namespace applies.
		EXPECT_EQ( defaulted.namespaceIndex, 3 );
		EXPECT_EQ( ToSV(defaulted.name), "Tag" );
	}

	TEST( BrowseNameTests, ToJson ){
		BrowseName b{ "Tag", 2 };
		let j = b.ToJson();
		EXPECT_EQ( j.at("ns").to_number<int>(), 2 );
		EXPECT_EQ( j.at("name").as_string(), "Tag" );
		EXPECT_EQ( b.ToString(), R"({"ns":2,"name":"Tag"})" );

		BrowseName nameless{ "", 0 };
		EXPECT_TRUE( nameless.ToJson().empty() ); //an empty name drops the whole object, ns included.
	}

	TEST( BrowseNameTests, CopyIsDeepMoveTransfers ){
		BrowseName src{ "Tag", 2 };
		BrowseName copy{ src };
		EXPECT_EQ( ToSV(copy.name), "Tag" );
		EXPECT_NE( copy.name.data, src.name.data );

		BrowseName moved{ move(src) };
		EXPECT_EQ( ToSV(moved.name), "Tag" );
		EXPECT_EQ( src.name.data, nullptr );
	}

	TEST( BrowseNameTests, Assignment ){
		BrowseName a{ "a", 2 };
		let b = BrowseName{ "b", 2 };
		a = b;
		EXPECT_EQ( ToSV(a.name), "b" );
		EXPECT_NE( a.name.data, b.name.data );

		BrowseName c{ "c", 2 };
		a = move( c );
		EXPECT_EQ( ToSV(a.name), "c" );
		EXPECT_EQ( c.name.data, nullptr );

		auto& alias = a; //through a reference: `a = a` is -Wself-assign-overloaded, and the guard is what is under test.
		a = alias;
		EXPECT_EQ( ToSV(a.name), "c" );
	}

	TEST( BrowseNameTests, Equality ){
		BrowseName a{ "Tag", 2 };
		BrowseName same{ "2~Tag", 0 };
		EXPECT_TRUE( a==same );   //the plain spelling, which is the point of the fix.
		EXPECT_FALSE( a!=same );  //!= is the rewritten form of the same operator, so it comes along for free.

		BrowseName otherNamespace{ "Tag", 3 };
		EXPECT_FALSE( a==otherNamespace );

		BrowseName otherName{ "Other", 2 };
		EXPECT_FALSE( a==otherName );

		EXPECT_TRUE( BrowseName{}==BrowseName{} );//both empty - namespace 0 and a null name.
	}

	//#5 (fixed): `BrowseName()ι = default` left the UA_QualifiedName base indeterminate under
	//default-initialization - every sibling wrapper (NodeId(), ExNodeId(), LocalizedText()) zeroes its base.  OpcServer
	//declares `BrowseName Browse;` with no initializer and three Node ctors omit it from their init lists, so on recycled
	//heap ~BrowseName freed a garbage name.data (and assigning to it first was just as fatal - operator= clears the
	//target before copying).  Placement-new over poisoned storage is the only way to observe this deterministically; the
	//ASSERTs come before the explicit destructor call so a failing run does not free a wild pointer.
	TEST( BrowseNameTests, DefaultConstructorZeroesTheBase ){
		alignas(BrowseName) std::byte storage[sizeof(BrowseName)];
		::memset( storage, 0xCD, sizeof(storage) );
		auto* p = ::new (static_cast<void*>(storage)) BrowseName;
		ASSERT_EQ( p->name.length, 0u );
		ASSERT_EQ( p->name.data, nullptr );
		EXPECT_EQ( p->namespaceIndex, 0 );

		//the other half of #5: operator= clears the target before copying, so assigning over a default-constructed
		//BrowseName - what OpcServer does with `node.Browse = GetUAServer().GetBrowse(...)` - has to be safe too.
		*p = BrowseName{ "Tag", 2 };
		EXPECT_EQ( ToSV(p->name), "Tag" );

		p->~BrowseName();
	}
}
