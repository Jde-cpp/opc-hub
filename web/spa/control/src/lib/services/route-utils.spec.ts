import { isNotFound, matchConfig, matchLiterals, segmentDisplay } from './route-utils';

//The breadcrumb fallback for a segment nothing names.  "gateways" sat lowercase between "Applications" and the instance.
describe( 'segmentDisplay', ()=>{
	it( 'title-cases a lowercase segment', ()=>{
		expect( segmentDisplay('gateways') ).toBe( 'Gateways' );
	} );
	it( 'splits a camelCase segment into words', ()=>{
		expect( segmentDisplay('appServers') ).toBe( 'App Servers' );
	} );
	it( 'leaves anything that is not a plain identifier alone', ()=>{
		expect( segmentDisplay('OpcHub.debug') ).toBe( 'OpcHub.debug' );
		expect( segmentDisplay('my-target') ).toBe( 'my-target' );
		expect( segmentDisplay('42') ).toBe( '42' );
		expect( segmentDisplay('Already') ).toBe( 'Already' );
	} );
	it( 'decodes before deciding', ()=>{
		expect( segmentDisplay('a%20b') ).toBe( 'a b' );
	} );
} );

//reviews/m3-closing.md #38:  the site's catch-all not-found route matches every url, so a reader of the route table that took it
//for a page - a crumb, AppResolver's card link, the search's scoring - would find one everywhere.  The node browser's own '**'
//(inside gateways/:gateway/:connection) still matches.
describe( 'matchConfig and matchLiterals skip the not-found catch-all', ()=>{
	const routes = [
		{ path: 'apps' },
		{ path: 'apps/gateways/:instance' },
		{ path: 'gateways/:gateway/:connection', children: [{ path: '**' }] },
		{ path: '**', data: {notFound: true} }
	];
	it( 'recognizes the route by its data', ()=>{
		expect( isNotFound(routes[3]) ).toBe( true );
		expect( isNotFound(routes[2].children![0]) ).toBe( false );
	} );
	it( 'finds no page where only the catch-all would match', ()=>{
		expect( matchConfig(routes, ['apps', 'gateways']) ).toBeUndefined();
		expect( matchConfig(routes, ['nonsense']) ).toBeUndefined();
		expect( matchLiterals(routes, ['nonsense']) ).toBe( -1 );
	} );
	it( "still matches a node browse path through the connection's own '**'", ()=>{
		expect( matchConfig(routes, ['gateways', 'gw', 'cn', '5~pump1']) ).toBe( routes[2].children![0] );
		expect( matchLiterals(routes, ['gateways', 'gw', 'cn', '5~pump1']) ).toBe( 1 );
	} );
} );
