import { TestBed } from '@angular/core/testing';
import { lastValueFrom } from 'rxjs';
import { ISearchProvider, SEARCH_PROVIDERS, SearchResult } from './search-provider';
import { SearchService } from './search-service';
import { silence } from 'jde-spa/testing';

const hit = ( title:string, source:string, rank=0, prefix?:string ):SearchResult=>({ title, route: `/${source}/${title}`, rank, source, prefix });

function provider( name:string, results:SearchResult[]|Error, prefixes?:string[] ){
	return { name, prefixes, search: vi.fn<ISearchProvider["search"]>( async ()=>{ if( results instanceof Error ) throw results; return results; } ) };
}

describe( 'SearchService', ()=>{
	const create = ()=>{
		const routes = provider( 'routes', [hit('Gateways', 'routes', 0), hit('Access', 'routes', 1)] );
		const access = provider( 'access', [hit('alice', 'access', 0, 'user'), {...hit('Gateways', 'routes', 0), source: 'access'}], ['user','role'] );//the second hit duplicates a route hit (same route).
		const broken = provider( 'broken', new Error('down') );
		TestBed.configureTestingModule({ providers: [
			{ provide: SEARCH_PROVIDERS, useValue: routes, multi: true },
			{ provide: SEARCH_PROVIDERS, useValue: access, multi: true },
			{ provide: SEARCH_PROVIDERS, useValue: broken, multi: true },
		]});
		return { service: TestBed.inject( SearchService ), routes, access, broken };
	};

	//the text keeps its case:  the providers match case-blind (searchRank), and a node id's string identifier is exact
	it( 'parses a registered prefix into a scope and leaves other colons, and the case, as typed', ()=>{
		const { service } = create();
		expect( service.parse('user:Al') ).toEqual( {scope: 'user', text: 'Al'} );
		expect( service.parse('  role:  ') ).toEqual( {scope: 'role', text: ''} );
		expect( service.parse('foo:bar') ).toEqual( {text: 'foo:bar'} );
		expect( service.parse('Pump') ).toEqual( {text: 'Pump'} );
		expect( service.parse('ns=2;s=Tank1') ).toEqual( {text: 'ns=2;s=Tank1'} );
	});

	it( 'merges every provider, dedupes on route, ranks then keeps registration order, and survives a failing provider', async ()=>{
		const { service, broken } = create();
		const warn = silence();
		const results = await lastValueFrom( service.search('a') );
		expect( results.map(r=>`${r.source}:${r.title}`) ).toEqual( ['routes:Gateways', 'access:alice', 'routes:Access'] );
		expect( warn ).toHaveBeenCalled();
		expect( broken.search ).toHaveBeenCalledTimes( 1 );
		warn.mockRestore();
	});

	it( 'a scope only asks the providers that list it', async ()=>{
		const { service, routes, access } = create();
		silence();
		const results = await lastValueFrom( service.search('user:al', 5) );
		expect( routes.search ).not.toHaveBeenCalled();
		expect( access.search.mock.calls.map( c=>c.slice(0, 2) ) ).toEqual( [['al', 'user']] );
		expect( results.map(r=>r.title) ).toEqual( ['alice', 'Gateways'] );
	});

	it( 'caps at the limit and returns nothing for blank text', async ()=>{
		const { service } = create();
		silence();
		expect( await lastValueFrom(service.search('a', 1)) ).toHaveLength( 1 );
		expect( await lastValueFrom(service.search('   ')) ).toEqual( [] );
	});
});

//reviews/m3-closing.md #26:  a resource has no page of its own, so every `resource:` hit routes to the one list - and the key
//merge de-duplicated on was the route alone:  `resource:` showed one resource of a dozen, and an unscoped "res" none at all
//behind the Resources page.  A hit is its landing page AND its title;  the same page under the same name from two providers
//is still one row - the RouteStore's children repeat the access provider's users, groups and roles without its prefix.
describe( 'SearchService keys a hit, not just its page', ()=>{
	const resource = ( title:string ):SearchResult=>({ title, route: '/access/resources', prefix: 'resource', rank: 0, source: 'access' });
	const setup = ( ...providers:ISearchProvider[] )=>{
		TestBed.configureTestingModule({ providers: providers.map( p=>({provide: SEARCH_PROVIDERS, useValue: p, multi: true}) ) });
		return TestBed.inject( SearchService );
	};

	it( 'shows every resource, though they all open the resources list', async ()=>{
		const service = setup( provider('access', [resource('acl'), resource('search'), resource('sessions')], ['resource']) );
		expect( (await lastValueFrom(service.search('resource:'))).map(r=>r.title) ).toEqual( ['acl', 'search', 'sessions'] );
	});

	it( 'keeps the resource hits beside the Resources page in an unscoped search', async ()=>{
		const service = setup(
			provider( 'routes', [{title: 'Resources', route: '/access/resources', rank: 0, source: 'routes'}] ),
			provider( 'access', [resource('resources'), resource('roles')], ['resource'] ) );
		expect( (await lastValueFrom(service.search('res'))).map(r=>`${r.prefix ?? ''}:${r.title}`) ).toEqual( [':Resources', 'resource:resources', 'resource:roles'] );
	});

	it( 'still shows a page two providers both name the same way once', async ()=>{
		const service = setup(
			provider( 'routes', [{title: 'Alice', route: '/access/users/alice', rank: 0, source: 'routes'}] ),
			provider( 'access', [{title: 'Alice', route: ['/access', 'users', 'alice'], prefix: 'user', rank: 0, source: 'access'}], ['user'] ) );
		expect( await lastValueFrom(service.search('ali')) ).toHaveLength( 1 );
	});
});