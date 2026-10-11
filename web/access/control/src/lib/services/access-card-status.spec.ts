import { TestBed } from '@angular/core/testing';
import { AccessService } from './access-service';
import { AccessCardStatus, AccessCollectionStatus } from './access-card-status';

describe( 'Access card statuses', ()=>{
	//the rows each list query answers - groups as the server answered an empty `groups{ id }` before the GroupAwait fix.  The
	//server returns deleted rows unless the query says deleted:null, so each list holds one:  a deleted user, and an unenforced
	//(deleted) table beside the enforced one and the node row a per-node grant mints live (access_role_add), which
	///access/resources does not list.
	const deleted = '2026-10-01T00:00:00Z';
	const rows:Record<string,unknown> = { users: [{id: 1}, {id: 2}, {id: 3}, {id: 4, deleted}], groups: {}, roles: [{id: 1}, {id: 2, deleted}], resources: [{id: 1, criteria: null}, {id: 2, criteria: 'ns=2;i=10'}, {id: 3, criteria: null, deleted}] };
	beforeEach( ()=>TestBed.configureTestingModule( {providers: [
		{provide: AccessService, useValue: {query: async ( ql:string )=>{
			const [, c, args=''] = /^(\w+)(?:\((.*)\))?\s*\{/.exec( ql.trim() )!;
			const filters = args.split( ',' ).map( a=>a.replace(/\s/g, '') );
			const answer = rows[c];
			return {[c]: Array.isArray(answer) ? answer.filter( r=>filters.every( f=>!f.endsWith(':null') || r[f.slice(0, -5)]==null ) ) : answer};//the server's filter
		}}}
	]} ) );

	//reviews/m3-closing.md #27:  the card counted the node rows too, so a per-node grant read "1 enforced" over a page whose
	//every switch was off.
	it( 'counts each list for its card, resources as the enforced tables the page lists', async ()=>{
		const status = TestBed.inject( AccessCollectionStatus );
		const cards = await Promise.all( ['users', 'groups', 'roles', 'resources'].map(c=>status.status(`/access/${c}`)) );
		expect( cards.map(c=>`${c.figure} ${c.label}`) ).toEqual( ['3 users', '0 groups', '1 role', '1 enforced'] );
	});

	it( 'sums the page up for its header and its home tile', async ()=>{
		expect( await TestBed.inject(AccessCardStatus).status() ).toEqual( {label: 'Users', figure: 3, detail: '0 groups · 1 role', summary: '3 users · 0 groups · 1 role'} );
	});
});
