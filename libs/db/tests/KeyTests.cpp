#include <gtest/gtest.h>
#include <jde/db/Key.h>

#define let const auto

namespace Jde::DB::Tests{
	//How access reads a resource, {id:N} or {slug:"s"}: AclQLAwait::InsertPermission and RoleMAwait::AddPermission.  It moved out
	//of fwk's json.h, where a bad id threw boost's system_error with no value and no source location.
	TEST( KeyTests, AsKey ){
		let byId = AsKey( jobject{{"id", 42}, {"slug", "ignored"}} );
		ASSERT_TRUE( byId.IsPK() ) << "the id wins when both are present";
		EXPECT_EQ( byId.PK(), 42u );
		let bySlug = AsKey( jobject{{"slug", "nodeIds"}} );
		ASSERT_FALSE( bySlug.IsPK() );
		EXPECT_EQ( bySlug.NK(), "nodeIds" );

		EXPECT_THROW( AsKey(jobject{{"name", "x"}}), Exception );
		EXPECT_THROW( AsKey(jobject{{"id", "42"}}), CodeException ) << "a wrong kind is the house conversion error";
		EXPECT_THROW( AsKey(jobject{{"id", -1}}), CodeException );
		EXPECT_THROW( AsKey(jobject{{"slug", 7}}), CodeException );
	}
}