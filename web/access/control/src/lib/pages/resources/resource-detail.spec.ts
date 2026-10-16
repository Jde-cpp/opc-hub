if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { HttpErrorResponse } from '@angular/common/http';
import { ActivatedRoute, provideRouter } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { vi } from 'vitest';
import { ComponentPageTitle, RouteItem } from 'jde-spa';
import { Rights } from '../../model/permission';
import { Resource } from '../../model/resource';
import { ResourceHolders } from '../../model/resource-holder';
import { AccessService } from '../../services/access-service';
import { ResourcePageData } from '../../services/resource-resolver';
import { ResourceDetail } from './resource-detail';

//#251:  a resource's page - who holds rights on it, from the server.
const resource = new Resource( {id: 4, schemaName: "access", slug: "resources", name: "resources", allowed: Rights.Read|Rights.Delete|Rights.Administer|Rights.Subscribe} );
const holders = new ResourceHolders( {
	resource: {id: 4, deleted: null},
	roles: [{id: 11, allowed: Rights.Read, denied: 0, sources: [{permissionId: 5, allowed: Rights.Read, denied: 0, path: []}]}],
	groups: [],
	users: [{id: 31, allowed: Rights.Read, denied: 0, effective: Rights.Read, sources: [{permissionId: 5, allowed: Rights.Read, denied: 0, path: [{id: 11, type: "role"}]}]}]
}, {roles: [{id: 11, name: "Viewer", slug: "viewer"}], groups: [], users: [{id: 31, name: "alice", slug: "alice"}]} );
const pageData:ResourcePageData = { resource, routing: new RouteItem({path: "access/resources", title: "resources"}), accessUrl: "/access" };

const create = async ( answer:( id:number, lists?:unknown )=>Promise<ResourceHolders> )=>{
	TestBed.resetTestingModule();
	TestBed.configureTestingModule({ providers: [
		provideRouter( [] ),
		{ provide: ActivatedRoute, useValue: {data: new BehaviorSubject( {pageData} )} },
		{ provide: AccessService, useValue: {resourceRights: answer} },
		{ provide: ComponentPageTitle, useValue: {} }
	]});
	const fixture = TestBed.createComponent( ResourceDetail );
	fixture.componentRef.setInput( 'sideNav', new RouteItem() );
	fixture.detectChanges();
	await fixture.whenStable();
	fixture.detectChanges();
	return fixture;
};

describe( 'ResourceDetail', ()=>{
	it( 'counts the holders on their tabs and says the resource is enforced', async ()=>{
		const fixture = await create( async ()=>holders );
		const text = fixture.nativeElement.textContent as string;
		expect( text ).toContain( "Roles (1)" );
		expect( text ).toContain( "Groups (0)" );
		expect( text ).toContain( "Users (1)" );
		expect( text ).toContain( "Enforced." );
		expect( fixture.componentInstance.sideNav().path ).toBe( "access/resources" );
	} );

	it( 'offers only the rights the resource does', async ()=>{
		const fixture = await create( async ()=>holders );
		expect( fixture.componentInstance.rights().map( kv=>kv.value ) ).toEqual( ["Read", "Delete", "Administer", "Subscribe"] );
	} );
	//reviews-issue/251-resource-page.md #5:  a right granted outside the resource's offer was enforced and never shown
	it( 'adds a column for a right a grant holds beyond the offer', async ()=>{
		const beyond = new ResourceHolders( {
			resource: {id: 4, deleted: null},
			roles: [{id: 11, allowed: Rights.Read|Rights.Update, denied: Rights.Execute, sources: [{permissionId: 5, allowed: Rights.Read|Rights.Update, denied: Rights.Execute, path: []}]}],
			groups: [],
			users: []
		}, {roles: [{id: 11, name: "Viewer", slug: "viewer"}], groups: [], users: []} );
		const fixture = await create( async ()=>beyond );
		expect( fixture.componentInstance.rights().map( kv=>kv.value ) ).toEqual( ["Read", "Update", "Delete", "Administer", "Subscribe", "Execute"] );
	} );

	//reviews-issue/251-resource-page.md #9:  each resource the sidenav opened read the three full lists again
	it( "hands the next resource the lists the last one read", async ()=>{
		const answer = vi.fn( async ( _id:number, _lists?:unknown )=>holders );
		const fixture = await create( answer );
		const other = new Resource( {id: 5, schemaName: "access", slug: "roles", name: "roles", allowed: Rights.Read} );
		(TestBed.inject( ActivatedRoute ).data as BehaviorSubject<unknown>).next( {pageData: {...pageData, resource: other}} );
		await fixture.whenStable();
		expect( answer.mock.calls.map( c=>[c[0], c[1]] ) ).toEqual( [[4, undefined], [5, holders.lists]] );
	} );

	it( 'says why when the grants may not be read', async ()=>{
		const fixture = await create( async ()=>{ throw new HttpErrorResponse({status: 403, error: "[bob]User does not have 'Read' access to 'acl'."}); } );
		const text = fixture.nativeElement.textContent as string;
		expect( text ).toContain( "No access to who holds rights here." );
		expect( text ).toContain( "Read on acl" );
		expect( text ).not.toContain( "Roles (" );
	} );
} );