import { TestBed } from '@angular/core/testing';
import { AccessService } from './access-service';
import { AccessSearchProvider } from './access-search-provider';

const rows:Record<string,{id:number,name:string,slug:string,deleted?:string}[]> = {
	users: [ {id:1, name:"John Duffy", slug:"Google-johnmduffy@gmail.com"}, {id:2, name:"alice", slug:"alice"}, {id:6, name:"johnny", slug:"johnny", deleted:"2026-10-01T00:00:00Z"} ],
	groups: [ {id:3, name:"ops", slug:"ops"} ],
	roles: [ {id:4, name:"admin", slug:"admin"} ]
};
const access = {
	queryArray: ( ql:string )=>{//the server returns deleted rows unless asked for deleted:null
		const [, collection, args=''] = /^(\w+)(?:\((.*)\))?\s*\{/.exec( ql.trim() )!;
		return Promise.resolve( (rows[collection] ?? []).filter( r=>!args.includes('deleted:null') || r.deleted==null ) );
	},
	loadResources: ()=>Promise.resolve( [ {id:5, name:"nodes", slug:"nodeIds", schema:"opc.nodes"} ] )
};

describe( 'AccessSearchProvider', ()=>{
	let provider:AccessSearchProvider;
	beforeEach( ()=>{
		TestBed.configureTestingModule({ providers: [ {provide: AccessService, useValue: access} ] });
		provider = TestBed.inject( AccessSearchProvider );
		provider.invalidate();
	} );

	//angular-review3 #7: the route was pre-encoded and then handed to router.navigate, which encodes each command again -
	//'@' went out as '%2540', paramMap yielded '…%40…', and no Google-provisioned user resolved.  Raw segments, as
	//NodeSearchProvider has always done.
	it( 'hands the router raw segments, not a pre-encoded string', async ()=>{
		const [hit] = await provider.search( 'john', 'user', 10 );//SearchService lowercases the query; searchRank only lowercases the title
		expect( hit.route ).toEqual( ['/access', 'users', 'Google-johnmduffy@gmail.com'] );
	} );

	it( 'leaves deleted users out', async ()=>{
		expect( (await provider.search('john', 'user', 10)).map(h=>h.title) ).toEqual( ["John Duffy"] );
	} );

	it( 'routes groups and roles the same way', async ()=>{
		expect( (await provider.search('ops', 'group', 10))[0].route ).toEqual( ['/access', 'groups', 'ops'] );
		expect( (await provider.search('admin', 'role', 10))[0].route ).toEqual( ['/access', 'roles', 'admin'] );
	} );

	//resources have no detail page - the hit opens the list, and that url carries no user data.
	it( 'sends a resource hit to the list as a plain url', async ()=>{
		expect( (await provider.search('nodes', 'resource', 10))[0].route ).toBe( '/access/resources' );
	} );
} );
