//review3 #8:  a subscription is keyed by the parser as ToPlural(FromJson(name minus its verb suffix)), while a mutation is
//keyed by MutationQL::TableName() = the resolved DBTable's name.  OnMutation looks the pair up exactly, so the two spellings
//have to agree - `permissionUpdated` keyed `permissions` while `updatePermissionRight` publishes `permission_rights`, and the
//access cache never saw a revocation.  provider_types is the same multi-word shape.  Schema-only, no data source.
#include <gtest/gtest.h>
#include <jde/tests/Capture.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/Parser.h>
#include "UnitSchema.h"

#define let const auto

namespace Jde::QL::Tests{
	Ω subscription( string text )ε->Subscription{
		let schema = schemas();
		auto subs = QL::ParseSubscriptions( move(text), {}, schema );
		return move( subs.front() );
	}
	Ω mutationTableName( string text )ε->string{
		let schema = schemas();
		return QL::ParseM( move(text), {}, schema ).TableName();
	}

	//the invariant the access subscription broke:  the two spellings of the same table have to meet.
	TEST( SubscriptionKeyTests, MultiWordKeyMatchesTheMutationsTableName ){
		let sub = subscription( "subscription ProviderTypeUpdated{ providerTypeUpdated(subscriptionId:1){ id name } }" );
		EXPECT_EQ( sub.TableName, "provider_types" );
		EXPECT_EQ( sub.TableName, mutationTableName("mutation updateProviderType( id:1, name:\"x\" )") );
		EXPECT_EQ( sub.Type, EMutationQL::Update );
	}
	//the singular/plural half of the same round trip.
	TEST( SubscriptionKeyTests, SingleWordKeyMatchesTheMutationsTableName ){
		let sub = subscription( "subscription ProviderCreated{ providerCreated(subscriptionId:1){ id name } }" );
		EXPECT_EQ( sub.TableName, "providers" );
		EXPECT_EQ( sub.TableName, mutationTableName("mutation createProvider( name:\"x\" )") );
		EXPECT_EQ( sub.Type, EMutationQL::Create );
	}

	//What would have caught #8 at subscribe time:  a column the keyed table does not have is now warned about rather than
	//silently never delivered.  The subscription is still accepted - refusing it would break a running deployment.
	TEST( SubscriptionKeyTests, UnknownColumnIsWarnedAndStillSubscribes ){
		Logging::Capture capture{ ELogLevel::Warning };

		let sub = subscription( "subscription ProviderTypeUpdated{ providerTypeUpdated(subscriptionId:1){ id allowed denied } }" );
		EXPECT_EQ( sub.TableName, "provider_types" );
		EXPECT_EQ( sub.Fields.Columns.size(), 3u ); //warned, not dropped - refusing would break a running deployment.
		let warnings = capture.Find( [](let& entry){ return entry.Message().find("which the table does not have")!=string::npos; } );
		EXPECT_EQ( warnings.size(), 2u ); //allowed and denied, exactly what #8's subscription asked `permissions` for.
	}
	//and the enum stem addColumn resolves through <name>_id must not be warned about.
	TEST( SubscriptionKeyTests, EnumStemIsNotAnUnknownColumn ){
		let sub = subscription( "subscription ProviderUpdated{ providerUpdated(subscriptionId:1){ id providerType } }" );
		EXPECT_EQ( sub.Fields.Columns.size(), 2u );
	}
}
