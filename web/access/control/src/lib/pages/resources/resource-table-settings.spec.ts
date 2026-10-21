import { QLListResolver, ViewFieldSettings } from 'jde-framework';
import { resourceTableSettings } from './resource-table-settings';
import { id, required, scalar, schemaOf } from 'jde-framework/testing';

describe( 'resourceTableSettings', ()=>{
	const column = ( name:string )=>resourceTableSettings.columns!.find( c=>typeof c!="string" && c.name==name ) as ViewFieldSettings;

	//#251:  a row opens its resource's page, at 'resources/:schema/:slug' - `logs` is app's and every OpcServer's, so the slug
	//alone (QLList's default link) would not say which.
	it( 'opens a row at its schema and slug', ()=>{
		expect( resourceTableSettings.canNavigate ?? true ).toBe( true );
		expect( resourceTableSettings.rowLink!.segments( {slug: "logs", schemaName: "opc.debug"} ) ).toEqual( ["opc.debug", "logs"] );
	} );
	//reviews-issue/251-resource-page.md #1:  with the Schema column hidden the link read an unqueried field, 'undefined/logs'
	it( 'queries the schema for the link, shown or not', ()=>{
		expect( resourceTableSettings.rowLink!.fields ).toContain( "schemaName" );
	} );
	//reviews-issue/251-resource-page.md #4:  a node row shares its table's schema and slug, so its link opened the table's page
	it( 'gives a criteria row no link', ()=>{
		expect( resourceTableSettings.rowLink!.fields ).toContain( "criteria" );
		expect( resourceTableSettings.rowLink!.segments( {slug: "nodeIds", schemaName: "opc.x", criteria: "ns=2;s=Boiler"} ) ).toBeUndefined();
		expect( resourceTableSettings.rowLink!.segments( {slug: "nodeIds", schemaName: "opc.x", criteria: null} ) ).toEqual( ["opc.x", "nodeIds"] );
	} );

	it( 'orders by schema then name, and shows the schema', ()=>{
		expect( column("schemaName")?.displayName ).toBe( "Schema" );
		expect( resourceTableSettings.sort ).toEqual( [{active:"schemaName", direction:"asc"}, {active:"name", direction:"asc"}] );
	} );

	//`deleted` on a resource is the authorizer's switch, not a trash can:  Authorize::Test skips a deleted resource ("not
	//enabled"), so the row the switch is OFF for is the one nothing checks permissions on.  It has to read as Enforced, or
	//an operator turning enforcement on thinks they are restoring something from the bin.
	it( 'renders deleted as the Enforced switch, with a verb for each direction', ()=>{
		const enforced = column( "deleted" );
		expect( enforced?.displayName ).toBe( "Enforced" );
		expect( enforced?.liveToggle?.enable ).toBe( "Enforce" );
		expect( enforced?.liveToggle?.disable ).toBe( "Stop enforcing" );
		expect( enforced?.liveToggle?.enableMessage ).toMatch( /lose your own access/ );
	} );

	//Neither is grantable on this table - access-meta gives `resources` ops Read, Delete and Subscribe only - so offering either
	//would be offering a button whose mutation the server must refuse.
	it( 'offers neither Add nor Purge', ()=>{
		expect( resourceTableSettings.canAdd ).toBe( false );
		expect( resourceTableSettings.canPurge ).toBe( false );
	} );

	//A node-scoped resource (criteria set) is minted when a role is granted on a node and shares its table's slug, so on a
	//page with no criteria column it reads as a duplicate row.  The page lists the table rows only - one view, no way to the node rows.
	it( 'lists only the criteria-less rows, as a single Tables view', ()=>{
		const schema = schemaOf( "Resource", id(), required("slug"), scalar("schemaName"), scalar("name"), scalar("criteria"), scalar("deleted", "DateTime"), scalar("description") );
		const views = QLListResolver.systemViews( schema, resourceTableSettings );
		expect( views.map(v=>v.name) ).toEqual( ["Tables"] );
		const q = views[0].query( 0 );
		expect( q.text ).toContain( "criteria:$criteria" );
		expect( q.vars["criteria"] ).toEqual( [null] );
	} );
} );