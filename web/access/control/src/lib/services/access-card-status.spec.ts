import { TestBed } from '@angular/core/testing';
import { AccessService } from './access-service';
import { AccessCardStatus, AccessCollectionStatus } from './access-card-status';

describe( 'Access card statuses', ()=>{
	//the rows each list query answers - groups as the server answered an empty `groups{ id }` before the GroupAwait fix
	const rows:Record<string,unknown> = { users: [{id: 1}, {id: 2}, {id: 3}], groups: {}, roles: [{id: 1}], resources: [] };
	beforeEach( ()=>TestBed.configureTestingModule( {providers: [
		{provide: AccessService, useValue: {query: async ( ql:string )=>{ const c = ql.split('{')[0].trim(); return {[c]: rows[c]}; }}}
	]} ) );

	it( 'counts each list for its card, resources as the ones enforced', async ()=>{
		const status = TestBed.inject( AccessCollectionStatus );
		const cards = await Promise.all( ['users', 'groups', 'roles', 'resources'].map(c=>status.status(`/access/${c}`)) );
		expect( cards.map(c=>`${c.figure} ${c.label}`) ).toEqual( ['3 users', '0 groups', '1 role', '0 enforced'] );
	});

	it( 'sums the page up for its header and its home tile', async ()=>{
		expect( await TestBed.inject(AccessCardStatus).status() ).toEqual( {label: 'Users', figure: 3, detail: '0 groups · 1 role', summary: '3 users · 0 groups · 1 role'} );
	});
});
