//The subscription fan-out (QL::Subscriptions::OnMutation) recovers the mutated row's id when the mutation result didn't carry
//one.  Review #7: every failure in that lookup escaped to the fan-out's catch _before_ the first OnChange, so one awkward arg
//cost *every* subscriber the notification.  These tests pin the reachable shapes.
//Deletes are the no-id path here: UpdateAwait resumes with a bare rowCount, so `available` is the args alone (an insert on a
//table with an insert proc - every access table - gets its id back from the proc's out row and never reaches the lookup).
//This suite is the schema-backed one closest to QL (sqlite in-memory under ctest), so the coverage lives here rather than in
//Jde.QL.Tests, which opens no data source.
#include "gtest/gtest.h"
#include <jde/access/AccessListener.h>
#include <jde/access/Authorize.h>
#include <jde/access/awaits/EventsSubscribeAwait.h>
#include <jde/access/server/awaits/AuthenticateAwait.h>
#include <jde/access/server/awaits/RoleAwait.h>
#include "../src/accessInternal.h"
#include "globals.h"
#include <jde/fwk/log/MemoryLog.h>
#include <jde/ql/IQL.h>
#include <jde/ql/LocalSubscriptions.h>

#define let const auto

namespace Jde::Access::Tests{
	constexpr sv Schema{ "qlSubTests" }; //not "access": ResourceTests::CheckDefaults counts that schema's resources.

	struct TestListener final : QL::IListener{
		TestListener()ι: QL::IListener{"SubscriptionTests"}{}
		α OnChange( const jvalue& j, QL::SubscriptionId )ε->void override{ Changes.push_back( Json::AsObject(j) ); }
		α Resource( uint i )Ι->const jobject&{ return Json::AsObject( Changes[i].begin()->value() ); } //{"resources":{…}} - the fields the subscription asked for.
		vector<jobject> Changes;
	};

	//mirrors Access::EventsSubscribeAwait's format - "subscription ResourcesDeleted{ resourcesDeleted(subscriptionId:$id){…} }".
	Ω listenTo( sv text )ε->sp<TestListener>{
		auto y = ms<TestListener>();
		auto await = QLPtr()->Subscribe( string{text}, jobject{{"id",7717}}, y, UserPK{UserPK::System} );
		BlockTAwait<vector<QL::SubscriptionId>>( move(*await) );
		return y;
	}
	Ω listen()ε->sp<TestListener>{ return listenTo( "subscription ResourcesDeleted{ resourcesDeleted(subscriptionId:$id){id slug} }" ); }
	Ω createResource( sv slug, sv extraArgs="" )ε->void{
		let ql = Ƒ( R"(mutation createResource( schemaName:"{0}", name:"{1} - name", slug:"{1}", description:"{1} - description"{2} ))", Schema, slug, extraArgs );
		QL().QuerySync<jvalue>( ql, {}, GetRoot() );
	}
	Ω deleteResource( sv slug, sv extraArgs="" )ε->void{
		QL().QuerySync<jvalue>( Ƒ(R"(mutation deleteResource( slug:"{}"{} ))", slug, extraArgs), {}, GetRoot() );
	}
	Ω resourceIds( sv slug )ε->vector<uint32>{
		let ql = Ƒ( R"(resources( schemaName:"{}", slug:"{}" ){{ id slug deleted }})", Schema, slug );
		vector<uint32> y;
		for( let& v : QL().QuerySync<jarray>(ql, {}, GetRoot()) )
			y.push_back( Json::AsNumber<uint32>(Json::AsObject(v), "id") );
		return y;
	}

	//A null arg is a value the client may send ("deleted: null"), and `created` is server-defaulted (insertable:false,
	//default:now), so the old `created is null` term matched no row, ScalerSync threw on the empty result, and the fan-out died
	//before notifying anyone.  Nulls no longer join the lookup, so the other args still find the row.
	//It used to arrive here as an unbound `$missing`, which extrapolated to the same null;  #36 made that its own error, so the
	//shape findId actually has to survive is spelled literally.  It also used to be `created:null`, which CreateDeleteRestore ands
	//into the where clause - `created` is server-defaulted, so that deleted 0 rows and #47 now suppresses its notification.
	TEST( SubscriptionTests, NullArgDoesNotDropTheNotification ){
		constexpr sv slug{ "subNullArg" };
		createResource( slug );
		let ids = resourceIds( slug ); ASSERT_EQ( ids.size(), 1u );

		auto listener = listen();
		deleteResource( slug, ", criteria:null" );//criteria is nullable and this row has none, so the delete matches - #47: a
		                                              //statement that matches nothing no longer notifies, so the null has to be a real one.
		QL::Subscriptions::StopListen( listener );

		ASSERT_EQ( listener->Changes.size(), 1u );
		let& resource = listener->Resource( 0 );
		EXPECT_EQ( Json::AsSV(resource, "slug"), slug );
		EXPECT_EQ( Json::AsNumber<uint32>(resource, "id"), ids[0] ); //recovered from the remaining args, the null skipped.
	}

	//The ordinary shape: the args identify exactly one row.
	TEST( SubscriptionTests, IdRecoveredFromTheArgs ){
		constexpr sv slug{ "subPlain" };
		createResource( slug );
		let ids = resourceIds( slug ); ASSERT_EQ( ids.size(), 1u );

		auto listener = listen();
		deleteResource( slug );
		QL::Subscriptions::StopListen( listener );

		ASSERT_EQ( listener->Changes.size(), 1u );
		EXPECT_EQ( Json::AsNumber<uint32>(listener->Resource(0), "id"), ids[0] );
	}

	//#8: Access::EventsSubscribeAwait subscribed as "permission", which the parser keys ToPlural(FromJson("permission")) =
	//`permissions`, while the UI's updatePermissionRight publishes under `permission_rights` - OnMutation's exact-key lookup
	//missed every time, so AccessListener::PermissionUpdated never fired and a revoked grant stayed in force until restart.
	//Published straight into OnMutation rather than through a real update:  the subject is the key MutationQL derives, and #47
	//means a statement that matched nothing no longer publishes - so a mutation against an id that does not exist would prove
	//nothing here.  jvalue{1} is the row count a real one would carry.
	TEST( SubscriptionTests, PermissionRightUpdateNotifiesItsSubscriber ){
		auto listener = listenTo( "subscription PermissionRightUpdated{ permissionRightUpdated(subscriptionId:$id){ id allowed denied } }" ); //what access subscribes now.
		let m = QL::ParseM( "mutation updatePermissionRight( id:987654321, allowed:1, denied:0 )", {}, Schemas() );
		ASSERT_EQ( m.TableName(), "permission_rights" );//the spelling #8 was about.
		QL::Subscriptions::OnMutation( m, jvalue{1} );
		QL::Subscriptions::StopListen( listener );

		ASSERT_EQ( listener->Changes.size(), 1u );
		let& permission = listener->Resource( 0 );
		EXPECT_EQ( Json::AsNumber<uint>(permission, "id"), 987654321u );
		EXPECT_EQ( Json::AsNumber<uint>(permission, "allowed"), 1u );
	}
	//the old spelling, kept as the control:  it keys a different table, so the same mutation reaches nobody.
	TEST( SubscriptionTests, PermissionUpdateKeysADifferentTable ){
		auto listener = listenTo( "subscription PermissionUpdated{ permissionUpdated(subscriptionId:$id){ id allowed denied } }" );
		QL::Subscriptions::OnMutation( QL::ParseM("mutation updatePermissionRight( id:987654321, allowed:1, denied:0 )", {}, Schemas()), jvalue{1} );
		QL::Subscriptions::StopListen( listener );
		EXPECT_TRUE( listener->Changes.empty() ); //`permissions` != `permission_rights` - this is what #8 was.
	}

	//An ambiguous lookup used to broadcast whichever row the driver returned last; now the notification goes out without an id
	//(and logs a warning) rather than naming a row at random.  `criteria` is part of the natural key, so the two rows differ only there.
	TEST( SubscriptionTests, AmbiguousLookupNotifiesWithoutAnId ){
		constexpr sv slug{ "subAmbiguous" };
		createResource( slug, R"(, criteria:"a")" );
		createResource( slug, R"(, criteria:"b")" );
		ASSERT_EQ( resourceIds(slug).size(), 2u );

		auto listener = listen();
		deleteResource( slug ); //by slug: both rows match, so the id lookup can't pick one.
		QL::Subscriptions::StopListen( listener );

		ASSERT_EQ( listener->Changes.size(), 1u ); //the notification still went out.
		EXPECT_FALSE( listener->Resource(0).contains("id") );
		EXPECT_EQ( Json::AsSV(listener->Resource(0), "slug"), slug );
	}

	//ql-review3 #43: the resources subscriptions prefixed their schema predicate to the *column* list, where LoadTable turned it
	//into the columns '(', 'schema:$schemas' and ')' and it never applied.  It is a predicate on the subscription, so it belongs
	//in the subscription's argument list next to subscriptionId - which is where EventsSubscribeAwait's format now puts it.
	//This is that text, in the shape the await emits it;  the parse itself is the pin, since the guard added with #43 refuses
	//the old spelling outright (Access.Tests would not have started).
	TEST( SubscriptionTests, TheSchemaPredicateIsAnArgumentNotAColumn ){
		let text = R"(subscription ResourcesCreated{ resourcesCreated(subscriptionId:$id, schemaName:$schemas){ id schemaName slug criteria deleted } })";
		auto subs = QL::ParseSubscriptions( string{text}, jobject{{"id",7717},{"schemas",jarray{Schema}}}, Schemas() );
		ASSERT_EQ( subs.size(), 1u );
		let& fields = subs[0].Fields;
		EXPECT_EQ( subs[0].TableName, "resources" );
		EXPECT_EQ( subs[0].Id, 7717u );//subscriptionId is consumed off the args; schemaName is not.
		ASSERT_TRUE( fields.Args.contains("schemaName") ) << serialize( fields.Args );
		let schemaArg = fields.FindPtr<jarray>( "schemaName" );//$schemas is the caller's list, so it extrapolates to an array.
		ASSERT_TRUE( schemaArg ) << serialize( fields.Args );
		ASSERT_EQ( schemaArg->size(), 1u );
		EXPECT_EQ( (*schemaArg)[0].as_string(), Schema );//resolved through Variables - a filter could read it.
		EXPECT_EQ( fields.Columns.size(), 5u );//the five real ones - no '(' or ')' among them.
		for( let& c : fields.Columns )
			EXPECT_EQ( c.JsonName.find_first_of("()"), string::npos ) << c.JsonName;
	}
	//and the shape it replaced is now refused rather than silently mis-parsed.
	TEST( SubscriptionTests, ThePredicateInTheColumnListIsRefused ){
		let text = R"(subscription ResourcesCreated{ resourcesCreated(subscriptionId:$id){ (schemaName:$schemas)id slug } })";
		try{
			QL::ParseSubscriptions( string{text}, jobject{{"id",7717},{"schemas",jarray{Schema}}}, Schemas() );
			ADD_FAILURE() << "the misplaced predicate parsed";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("argument list where a column belongs"), string::npos ) << e.what();
		}
	}

	//ql-review3 #47: OnMutation never looked at the result, and an integer rowCount is not an object - so `available` was the
	//args alone and the id the client sent went straight to the listeners.  A 0-row delete therefore told AccessListener a row
	//had been deleted: Authorize::DeleteUser marks it IsDeleted in memory and every later request by that user is refused with
	//"User is deleted", until restart, with the db row untouched.  CreateDeleteRestore ands every extra column arg into the
	//where clause, which is how a delete matches nothing while still looking like one.
	TEST( SubscriptionTests, AZeroRowDeleteDoesNotNotify ){
		constexpr sv slug{ "subZeroRow" };
		createResource( slug );
		let ids = resourceIds( slug ); ASSERT_EQ( ids.size(), 1u );

		auto listener = listen();
		//`name`, not `schemaName`:  CreateDeleteRestore looks the extra arg up with Table::FindColumn, which takes the sql name, so
		//only args whose json and sql spellings coincide ever reach the where clause.  That is the finding's own `name:"nomatch"`.
		deleteResource( slug, ", name:\"nomatch\"" );//matches no row - and is reported as success.
		QL::Subscriptions::StopListen( listener );

		EXPECT_TRUE( listener->Changes.empty() ) << "a statement that matched nothing was published as an event";
		EXPECT_EQ( resourceIds(slug).size(), 1u ) << "the row really was untouched - that is the point";//soft-deleted rows drop out.
	}
	//the control, on the same row: the delete that does match still notifies.
	TEST( SubscriptionTests, TheSameDeleteWithoutTheExtraPredicateDoesNotify ){
		constexpr sv slug{ "subZeroRowControl" };
		createResource( slug );
		ASSERT_EQ( resourceIds(slug).size(), 1u );

		auto listener = listen();
		deleteResource( slug );
		QL::Subscriptions::StopListen( listener );
		EXPECT_EQ( listener->Changes.size(), 1u );
	}

	//ql-review3 #53: the generic fan-out never evaluated a subscription's own arguments - only SubscribeLog::Write did, column by
	//column - so a client that asked for one schema was delivered every schema's events.  End to end here because the values the
	//predicate is tested against are the mutation's args, which only a real mutation produces.
	TEST( SubscriptionTests, ASubscriptionsOwnArgsScopeIt ){
		constexpr sv slug{ "subFiltered" };
		auto match = listenTo( Ƒ(R"(subscription ResourcesCreated{{ resourcesCreated(subscriptionId:$id, schemaName:"{}"){{ id slug schemaName }} }})", Schema) );
		auto miss = listenTo( R"(subscription ResourcesCreated{ resourcesCreated(subscriptionId:$id, schemaName:"nomatch"){ id slug schemaName } })" );

		createResource( slug );
		QL::Subscriptions::StopListen( match );
		QL::Subscriptions::StopListen( miss );

		ASSERT_EQ( match->Changes.size(), 1u );
		EXPECT_EQ( Json::AsSV(match->Resource(0), "slug"), slug );
		EXPECT_TRUE( miss->Changes.empty() ) << "the subscriber asked for one schema and was given another's event";
	}

	//ql-review3 #55: the fan-out delivers without an id on purpose when the lookup was ambiguous or found nothing (ql-review2 #7),
	//and AccessListener::OnChange read that id with Json::AsNumber, which throws - into the fan-out's catch, which was empty.  So
	//the real listener in this binary skipped those events and its cache went stale with nothing in the log.  Since access-review3
	//#22 a resources event without an id is resolved by slug instead (IdLessResourceDeleteReachesTheCache below); one naming a
	//slug the cache does not hold is refused by name - the mirror of the unknown-pk case - and an event with neither is the
	//one that says the cache is stale and returns.  Called directly rather than through a mutation, because a throw here is
	//invisible from the outside - ql swallows it either way, which is the other half (SubscriptionsTests.AThrowingListenerIsWarnedAbout).
	TEST( SubscriptionTests, AnIdLessNotificationDoesNotThrowOutOfTheListener ){
		auto listener = ms<Access::AccessListener>( QLPtr() );
		let deleted = (QL::SubscriptionId)underlying( ESubscription::Resources|ESubscription::Deleted );
		jvalue nameless{ jobject{ {"resources", jobject{{"description","nothing to find it by"}}} } };
		EXPECT_NO_THROW( listener->OnChange(nameless, deleted) );
		jvalue idLess{ jobject{ {"resources", jobject{{"slug","subStaleCache"}}} } };//exactly what AmbiguousLookupNotifiesWithoutAnId delivers.
		try{
			listener->OnChange( idLess, deleted );
			ADD_FAILURE() << "an id-less event naming a slug the cache does not hold was accepted";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("subStaleCache"), string::npos ) << e.what();//dispatched and refused by name, not skipped.
		}

		//and a payload that does carry one is still dispatched - the tolerance must not swallow the ordinary case.  An id nothing
		//is registered under reaches ResourceChanged and is refused *there*, by pk, which is the proof that it got that far.
		jvalue withId{ jobject{ {"resources", jobject{{"id",987654321},{"slug","subStaleCache"}}} } };
		try{
			listener->OnChange( withId, deleted );
			ADD_FAILURE() << "an unknown resource pk was accepted";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("987654321"), string::npos ) << e.what();//it was dispatched with the id, not skipped.
		}
	}

	//opcserver-review3 #16, the user-snapshot gap:  a user is born on a login (AuthenticateAwait/LoginAwait's stored-proc inserts),
	//outside the mutation path - so the userCreated event every client's AccessListener subscribes to never fired, and a client
	//configured before the login never saw the user (denied on a protected node tree until it restarted).  The subscription is
	//spelled as EventsSubscribeAwait spells it, so this also proves the two shapes match.  A re-login of the same identity inserts
	//nothing and publishes nothing.
	TEST( SubscriptionTests, ALoginBornUserIsPublished ){
		let root = GetRoot();
		const string loginName{ "subLoginBorn" };
		let provider = (ProviderPK)EProviderType::Google;
		if( let previous = SelectUser("Google-"+loginName, root, provider, true); !previous.empty() ) //user_insert_login's slug is <provider>-<login name>.
			PurgeUser( UserPK{GetId(previous)}, root );
		auto listener = listenTo( "subscription UserCreated{ userCreated(subscriptionId:$id){id name} }" );
		let userPK = BlockTAwait<UserPK>( Server::AuthenticateAwait{loginName, provider, {}} );
		ASSERT_EQ( listener->Changes.size(), 1u ) << "the login's insert published no userCreated event";
		EXPECT_EQ( Json::AsNumber<UserPK::Type>(listener->Resource(0), "id"), userPK.Value );
		EXPECT_EQ( Json::AsSV(listener->Resource(0), "name"), loginName ) << "#198: the clients' caches need the name the proc gave it";
		EXPECT_EQ( Authorizer()->UserName(userPK), loginName );
		let again = BlockTAwait<UserPK>( Server::AuthenticateAwait{loginName, provider, {}} );
		EXPECT_EQ( again.Value, userPK.Value );
		EXPECT_EQ( listener->Changes.size(), 1u ) << "an existing identity's login is not a creation";
		QL::Subscriptions::StopListen( listener, {} );
		PurgeUser( userPK, root );
	}

	//#198:  history edits store Authorize::UserName, and the cache only ever took names from its startup snapshot - userCreated
	//cached an empty one, and nothing subscribed to userUpdated.  Through the startup listener, as every client's cache gets them.
	TEST( SubscriptionTests, UserNamesReachTheCache ){
		let root = GetRoot();
		const string slug{ "subUserName" };
		let provider = (ProviderPK)EProviderType::Google;
		if( let previous = SelectUser(slug, root, provider, true); !previous.empty() ) //a previous run's row, renamed.
			PurgeUser( UserPK{GetId(previous)}, root );
		let created = GetUser( slug, root );
		const UserPK user{ GetId(created) };
		EXPECT_EQ( Authorizer()->UserName(user), Json::AsSV(created, "name") ) << "userCreated did not carry the name";

		QL().QuerySync<jvalue>( Ƒ(R"(mutation updateUser( id:{}, name:"subUserName renamed" ))", user.Value), {}, root );
		EXPECT_EQ( Authorizer()->UserName(user), "subUserName renamed" ) << "userUpdated did not reach the cache";
		QL().QuerySync<jvalue>( Ƒ(R"(mutation updateUser( id:{}, description:"subUserName desc" ))", user.Value), {}, root );
		EXPECT_EQ( Authorizer()->UserName(user), "subUserName renamed" ) << "an update that set no name changed it";
		PurgeUser( user, root );
	}

	Ω idWarnings()ι->vector<Logging::Entry>{ return Logging::Find( [](const Logging::Entry& e){ return e.Text.contains("id lookup") || e.Text.contains("carried no id"); } ); }

	//authorize-names review #3:  users extends identities, and the fan-out's id lookup selected from access_users alone with its
	//predicates on access_identities' columns - "no such column" - so every user mutation keyed by name or slug went out
	//without an id.  Nothing subscribed to userUpdated before #198; since it, each such update had the startup listener - and
	//every client's - call its cache stale.  The lookup joins the table an extension extends now, which also gets a by-slug
	//delete to the cache, where it really was stale.
	TEST( SubscriptionTests, AUserMutationKeyedByNameOrSlugCarriesItsId ){
		let root = GetRoot();
		const string slug{ "subIdLessUser" };
		let provider = (ProviderPK)EProviderType::Google;
		if( let previous = SelectUser(slug, root, provider, true); !previous.empty() ) //a previous run's row, deleted.
			PurgeUser( UserPK{GetId(previous)}, root );
		let created = GetUser( slug, root );
		const UserPK user{ GetId(created) };
		auto updated = listenTo( "subscription UserUpdated{ userUpdated(subscriptionId:$id){id name} }" );
		auto deleted = listenTo( "subscription UserDeleted{ userDeleted(subscriptionId:$id){id} }" );
		Logging::ClearMemory();
		QL().QuerySync<jvalue>( Ƒ(R"(mutation updateUser( name:"{}", description:"by name" ))", Json::AsSV(created, "name")), {}, root );
		QL().QuerySync<jvalue>( Ƒ(R"(mutation updateUser( slug:"{}", description:"by slug" ))", slug), {}, root );
		QL().QuerySync<jvalue>( Ƒ(R"(mutation deleteUser( slug:"{}" ))", slug), {}, root );
		QL::Subscriptions::StopListen( updated, {} );
		QL::Subscriptions::StopListen( deleted, {} );
		PurgeUser( user, root );

		let warnings = idWarnings();
		EXPECT_TRUE( warnings.empty() ) << warnings[0].Text;
		ASSERT_EQ( updated->Changes.size(), 2u );
		EXPECT_EQ( Json::FindNumber<UserPK::Type>(updated->Resource(0), "id"), optional{user.Value} ) << "keyed by name";
		EXPECT_EQ( Json::FindNumber<UserPK::Type>(updated->Resource(1), "id"), optional{user.Value} ) << "keyed by slug";
		ASSERT_EQ( deleted->Changes.size(), 1u );
		EXPECT_EQ( Json::FindNumber<UserPK::Type>(deleted->Resource(0), "id"), optional{user.Value} );
	}

	//deleteGroup soft-deletes the group's identity row - UpdateAwait::CreateDeleteRestore updates the table `deleted` lives in -
	//but the id lookup took its key from `groups`, the membership map, whose key is ( identity_id, member_id ).  No single key
	//meant no lookup, so a group deleted by slug went out without an id and stayed active in every access cache.  The key is
	//now the extended table's, where the map has none of its own.
	TEST( SubscriptionTests, AGroupDeletedBySlugCarriesItsId ){
		let root = GetRoot();
		const string slug{ "subGroupBySlug" };
		const GroupPK group{ GetId(GetGroup(slug, root)) };
		auto deleted = listenTo( "subscription GroupDeleted{ groupDeleted(subscriptionId:$id){id} }" );
		auto restored = listenTo( "subscription GroupRestored{ groupRestored(subscriptionId:$id){id} }" );
		Logging::ClearMemory();
		QL().QuerySync<jvalue>( Ƒ(R"(mutation deleteGroup( slug:"{}" ))", slug), {}, root );
		QL().QuerySync<jvalue>( Ƒ(R"(mutation restoreGroup( slug:"{}" ))", slug), {}, root );
		QL::Subscriptions::StopListen( deleted, {} );
		QL::Subscriptions::StopListen( restored, {} );
		PurgeGroup( group, root );

		let warnings = idWarnings();
		EXPECT_TRUE( warnings.empty() ) << warnings[0].Text;
		ASSERT_EQ( deleted->Changes.size(), 1u );
		EXPECT_EQ( Json::FindNumber<GroupPK::Type>(deleted->Resource(0), "id"), optional{group.Value} );
		ASSERT_EQ( restored->Changes.size(), 1u );
		EXPECT_EQ( Json::FindNumber<GroupPK::Type>(restored->Resource(0), "id"), optional{group.Value} );
	}

	//And when the lookup does come back empty - the args match two rows, or none - an update is still no stale cache:  keyed by
	//name or slug it renamed nobody.  Any other user event without an id is one, and has to go on saying so.
	TEST( SubscriptionTests, AnIdLessUserUpdateIsNotCalledAStaleCache ){
		auto listener = ms<Access::AccessListener>( QLPtr() );
		Logging::ClearMemory();
		listener->OnChange( jvalue{jobject{ {"users", jobject{{"name","nobody the cache holds"}}} }}, (QL::SubscriptionId)underlying(ESubscription::User|ESubscription::Updated) );
		EXPECT_TRUE( idWarnings().empty() ) << "an update that renamed nobody was called a stale cache";
		listener->OnChange( jvalue{jobject{ {"users", jobject{}} }}, (QL::SubscriptionId)underlying(ESubscription::User|ESubscription::Deleted) );
		let stale = idWarnings();
		ASSERT_EQ( stale.size(), 1u ) << "an id-less delete does leave the cache stale";
		EXPECT_TRUE( stale[0].Message().contains("event 84") ) << stale[0].Message(); //rebuilt from the stored args - a `{:x}` in the text itself is a Bad Format there.
	}

	//access-review3 #25:  AccessListener::Shutdown unsubscribed through UnsubscribeAwait with IListener::Ids, which nothing ever
	//filled - an empty set short-circuits it - so every subscription, and the listener with it, outlived the app's own handle.
	//It goes through StopListen by listener now, which takes everything when given no ids.  A second listener, subscribed the way
	//Configure subscribes the real one, so the startup listener the rest of this suite relies on is not the one shut down.
	TEST( SubscriptionTests, ShutdownUnsubscribesTheListener ){
		auto listener = ms<Access::AccessListener>( QLPtr() );
		BlockVoidAwait( Access::EventsSubscribeAwait{QLPtr(), {"access"}, UserPK{UserPK::System}, listener} );
		listener->Shutdown( false, SRCE_CUR );
		EXPECT_TRUE( QL::Subscriptions::StopListen(listener, {}).empty() ) << "Shutdown left subscriptions registered";
	}

	//access-review3 #22 (with #23's column):  tolerating the id-less event was half of it - the cache still kept enforcing rows the
	//admin had just deleted.  Resources recover by schemaName+slug, for every row the by-slug delete hit, through the startup
	//listener's own subscription - which asked for a column called `schema` that no table has, so the name could never match.
	//In `access`, where that subscription's schema predicate lets the events through; criteria-scoped, so CheckDefaults is untouched.
	TEST( SubscriptionTests, IdLessResourceDeleteReachesTheCache ){
		let root = GetRoot();
		constexpr sv slug{ "subIdLessDelete" };
		let select = Ƒ( R"(resources( schemaName:"access", slug:"{}" ){{ id }})", slug );
		for( let& v : QL().QuerySync<jarray>(select, {}, root) ) //a previous run's rows.
			Purge( "resource", GetId(Json::AsObject(v)), root );
		for( sv criteria : {"a", "b"} )
			QL().QuerySync<jvalue>( Ƒ(R"(mutation createResource( schemaName:"access", name:"{0}", slug:"{0}", description:"{0}", criteria:"{1}", allowed:255 ))", slug, criteria), {}, root );
		vector<uint32> ids;
		for( let& v : QL().QuerySync<jarray>(select, {}, root) )
			ids.push_back( Json::AsNumber<uint32>(Json::AsObject(v), "id") );
		ASSERT_EQ( ids.size(), 2u );
		const UserPK nobody{ GetId(GetUser("subIdLessNobody", root)) };
		for( let id : ids )
			EXPECT_THROW( Authorizer()->TestAdminResource(ResourcePK{id}, nobody), Exception ) << "active in the cache, so enforced";

		deleteResource( slug ); //by slug:  both rows, and a notification with no id.
		for( let id : ids )
			EXPECT_NO_THROW( Authorizer()->TestAdminResource(ResourcePK{id}, nobody) ) << "deleted in the cache as well - a deleted resource fail-opens";

		for( let id : ids )
			Purge( "resource", id, root );
		PurgeUser( nobody, root );
	}

	//appserver-review3 #13:  the server's Configure subscribes with no schema predicate now - its Authorize gates and answers every
	//schema's grants (TestSchemaAdmin, and the flat rule behind TestAdminGrant), so it has to see every schema's rows change.  This
	//suite's schema is not "access":  before, the startup listener's resourcesCreated(schemaName:["access"]) dropped the event and
	//the row never reached the cache, so a nobody passed TestAdminResource on it.
	TEST( SubscriptionTests, OtherSchemasResourcesReachTheStartupCache ){
		let root = GetRoot();
		constexpr sv slug{ "subAllSchemas" };
		for( let id : resourceIds(slug) )//a previous run's row.
			Purge( "resource", id, root );
		createResource( slug, ", allowed:255" );
		let ids = resourceIds( slug ); ASSERT_EQ( ids.size(), 1u );
		const UserPK nobody{ GetId(GetUser("subAllSchemasNobody", root)) };
		EXPECT_THROW( Authorizer()->TestAdminResource(ResourcePK{ids[0]}, nobody), Exception ) << "created in the cache, so enforced";
		deleteResource( slug );
		EXPECT_NO_THROW( Authorizer()->TestAdminResource(ResourcePK{ids[0]}, nobody) ) << "deleted in the cache as well";
		Purge( "resource", ids[0], root );
		PurgeUser( nobody, root );
	}

	//...and role grants:  RoleMAwait::AddPermission delivers roleAdded only to subscribers whose resource(schemaName:…) names the
	//permission's schema - the server's listener names none now - so a grant on another schema reaches the cache:  a holder of the
	//role administers the resource, which it did not before the grant.  The row is created first (the previous pin) so the grant's
	//admin check has something to enforce - System grants, as root holds nothing on a new row.
	TEST( SubscriptionTests, OtherSchemasRoleGrantsReachTheStartupCache ){
		let root = GetRoot();
		constexpr sv slug{ "subAllSchemasRole" };
		for( let id : resourceIds(slug) )//a previous run's row.
			Purge( "resource", id, root );
		createResource( slug, ", allowed:255" );
		let ids = resourceIds( slug ); ASSERT_EQ( ids.size(), 1u );
		let resourcePK = ResourcePK{ids[0]};
		const RolePK rolePK{ GetId(Get("role", "subAllSchemasRole", root)) };
		const UserPK holder{ GetId(GetUser("subAllSchemasHolder", root)) };
		QL().QuerySync<jvalue>( Ƒ("mutation createAcl( identity:{{id:{}}}, role:{{id:{}}} )", holder.Value, rolePK.Value), {}, root );
		EXPECT_THROW( Authorizer()->TestAdminResource(resourcePK, holder), Exception ) << "no grant yet";
		let grant = Ƒ( R"(addRole( id:{}, permissionRight:{{ allowed:{}, denied:0, resource:{{ schemaName:"{}", slug:"{}" }} }} ))", rolePK.Value, underlying(ERights::Administer), Schema, slug );
		let added = BlockTAwait<jvalue>( Server::RoleMAwait{QL::ParseM(grant, {}, Schemas()), UserPK{UserPK::System}} ).as_object();
		EXPECT_NO_THROW( Authorizer()->TestAdminResource(resourcePK, holder) ) << "roleAdded did not reach the cache - the holder's role does not carry the grant";

		BlockTAwait<jvalue>( Server::RoleMAwait{QL::ParseM(Ƒ("mutation removeRole( id:{}, permissionRight:{{id:{}}} )", rolePK.Value, Json::AsNumber<PermissionPK::Type>(added, "permissionRight/id")), {}, Schemas()), UserPK{UserPK::System}} );
		QL().QuerySync<jvalue>( Ƒ("purgeAcl( identity:{{ id:{} }}, role:{{ id:{} }} )", holder.Value, rolePK.Value), {}, root );
		Purge( "role", rolePK, root );
		Purge( "resource", resourcePK, root );
		PurgeUser( holder, root );
	}

	//access-refactor A7:  EventTypeSubscribeAwait loops over the event bits - each bit asked for is one subscription, its id entity | event.
	TEST( SubscriptionTests, SubscribesEveryEventAskedFor ){
		using enum ESubscription;
		auto listener = ms<AccessListener>( QLPtr() );
		BlockVoidAwait( EventsSubscribeAwait{QLPtr(), {}, UserPK{UserPK::System}, listener} );
		vector<uint> ids;
		for( let& id : QL::Subscriptions::StopListen(listener) )
			ids.push_back( id.to_number<uint>() );
		vector<uint> expected;
		for( let e : {User|Created, User|Updated, User|Deleted, User|Restored, User|Purged, Group|Deleted, Group|Restored, Group|Purged, Group|Added, Group|Removed,
			Role|Deleted, Role|Restored, Role|Purged, Role|Added, Role|Removed, Resources|Created, Resources|Deleted, Resources|Restored, Permission|Updated, Acl|Created, Acl|Purged} )
			expected.push_back( underlying(e) );
		std::ranges::sort( ids );
		std::ranges::sort( expected );
		EXPECT_EQ( ids, expected );
	}
}
